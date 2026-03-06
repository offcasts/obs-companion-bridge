#include "companion-bridge.hpp"
#include "plugin-main.hpp"
#include "http-server.hpp"
#include "mdns-advertiser.hpp"

#include <obs-module.h>
#include <util/platform.h>

#ifdef ENABLE_FRONTEND_API
#include <obs-frontend-api.h>
#include <util/config-file.h>
#endif

#include <nlohmann/json.hpp>
#include <chrono>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

struct CompanionBridge::Impl {
	HttpServer *httpServer = nullptr;
	MdnsAdvertiser *mdnsAdvertiser = nullptr;
};

static CompanionBridge *g_bridgeInstance = nullptr;

static void vendor_request_cb(obs_data_t *request_data,
			       obs_data_t *response_data, void *priv_data)
{
	auto *bridge = static_cast<CompanionBridge *>(priv_data);
	const char *type =
		obs_data_get_string(request_data, "requestType");
	bridge->HandleVendorRequest(type ? type : "", request_data,
				    response_data);
}

CompanionBridge::CompanionBridge() : m_impl(new Impl())
{
	g_bridgeInstance = this;
	m_status.discovered = false;
	m_status.companionPort = 0;
}

CompanionBridge::~CompanionBridge()
{
	g_bridgeInstance = nullptr;
	delete m_impl;
}

bool CompanionBridge::Initialize()
{
	m_running = true;
	ReadWebSocketSettings();
	StartHttpServer();
	StartMdnsAdvertiser();
	m_settingsMonitorThread =
		std::thread([this]() { SettingsMonitorThread(); });
	return true;
}

void CompanionBridge::OnOBSLoaded()
{
	RegisterObsWebSocketVendor();
	UpdateDockStatus();
	blog(LOG_INFO,
	     "[obs-companion-bridge] OBS loaded, advertising on port %d",
	     BRIDGE_HTTP_PORT);
}

void CompanionBridge::Shutdown()
{
	m_running = false;
	if (m_settingsMonitorThread.joinable())
		m_settingsMonitorThread.join();
	StopMdnsAdvertiser();
	StopHttpServer();
	UnregisterObsWebSocketVendor();
	blog(LOG_INFO, "[obs-companion-bridge] Shutdown complete");
}

void CompanionBridge::StartHttpServer()
{
	m_impl->httpServer = new HttpServer(BRIDGE_HTTP_PORT);

	m_impl->httpServer->AddRoute(
		"GET", "/api/v1/connection-info",
		[this](const HttpRequest &, HttpResponse &res) {
			WebSocketCredentials creds =
				GetCurrentCredentials();
			json body;
			body["host"] = creds.host;
			body["port"] = creds.port;
			body["password"] = creds.password;
			body["authEnabled"] = creds.authEnabled;
			body["pluginVersion"] = PLUGIN_VERSION;
			body["vendorName"] = VENDOR_NAME;
			res.status = 200;
			res.headers["Content-Type"] = "application/json";
			res.body = body.dump();
		});

	m_impl->httpServer->AddRoute(
		"POST", "/api/v1/companion-connected",
		[this](const HttpRequest &req, HttpResponse &res) {
			try {
				auto body = json::parse(req.body);
				std::string host = body.value(
					"companionHost", "127.0.0.1");
				int port = body.value("companionPort",
						      COMPANION_DEFAULT_PORT);
				std::string label =
					body.value("moduleLabel", "obs");
				OnCompanionConnected(host, port, label);
				json resp;
				resp["status"] = "ok";
				res.status = 200;
				res.headers["Content-Type"] =
					"application/json";
				res.body = resp.dump();
			} catch (...) {
				res.status = 400;
				res.body = R"({"error":"invalid body"})";
			}
		});

	m_impl->httpServer->AddRoute(
		"GET", "/api/v1/status",
		[](const HttpRequest &, HttpResponse &res) {
			json body;
			body["status"] = "ok";
			body["plugin"] = PLUGIN_NAME;
			body["version"] = PLUGIN_VERSION;
			res.status = 200;
			res.headers["Content-Type"] = "application/json";
			res.body = body.dump();
		});

	if (!m_impl->httpServer->Start()) {
		blog(LOG_ERROR,
		     "[obs-companion-bridge] Failed to start HTTP server on port %d",
		     BRIDGE_HTTP_PORT);
	} else {
		blog(LOG_INFO,
		     "[obs-companion-bridge] HTTP server listening on port %d",
		     BRIDGE_HTTP_PORT);
	}
}

void CompanionBridge::StopHttpServer()
{
	if (m_impl->httpServer) {
		m_impl->httpServer->Stop();
		delete m_impl->httpServer;
		m_impl->httpServer = nullptr;
	}
}

void CompanionBridge::StartMdnsAdvertiser()
{
	m_impl->mdnsAdvertiser = new MdnsAdvertiser();
	MdnsServiceInfo info;
	info.serviceType = MDNS_SERVICE_TYPE;
	info.serviceName = MDNS_SERVICE_NAME;
	info.port = BRIDGE_HTTP_PORT;
	info.txtRecords["apiVersion"] = "1";
	info.txtRecords["pluginVersion"] = PLUGIN_VERSION;

	if (!m_impl->mdnsAdvertiser->Register(info)) {
		blog(LOG_WARNING,
		     "[obs-companion-bridge] mDNS registration failed");
	} else {
		blog(LOG_INFO,
		     "[obs-companion-bridge] Advertising as '%s' via mDNS",
		     MDNS_SERVICE_NAME);
	}
}

void CompanionBridge::StopMdnsAdvertiser()
{
	if (m_impl->mdnsAdvertiser) {
		m_impl->mdnsAdvertiser->Unregister();
		delete m_impl->mdnsAdvertiser;
		m_impl->mdnsAdvertiser = nullptr;
	}
}

void CompanionBridge::RegisterObsWebSocketVendor()
{
	auto vendor = obs_websocket_register_vendor(VENDOR_NAME);
	if (!vendor) {
		blog(LOG_WARNING,
		     "[obs-companion-bridge] obs-websocket vendor registration failed");
		return;
	}
	obs_websocket_vendor_register_request(
		vendor, "GetConnectionInfo", vendor_request_cb, this);
	obs_websocket_vendor_register_request(vendor, "GetStatus",
					      vendor_request_cb, this);
	blog(LOG_INFO,
	     "[obs-companion-bridge] Registered obs-websocket vendor '%s'",
	     VENDOR_NAME);
}

void CompanionBridge::UnregisterObsWebSocketVendor() {}

void CompanionBridge::HandleVendorRequest(const std::string &requestType,
					  obs_data_t *requestData,
					  obs_data_t *responseData)
{
	if (requestType == "GetConnectionInfo") {
		WebSocketCredentials creds = GetCurrentCredentials();
		obs_data_set_string(responseData, "host",
				    creds.host.c_str());
		obs_data_set_int(responseData, "port", creds.port);
		obs_data_set_bool(responseData, "authEnabled",
				  creds.authEnabled);
		obs_data_set_string(responseData, "pluginVersion",
				    PLUGIN_VERSION);
	} else if (requestType == "GetStatus") {
		CompanionStatus status = GetStatus();
		obs_data_set_bool(responseData, "companionDiscovered",
				  status.discovered);
		obs_data_set_string(responseData, "companionHost",
				    status.companionHost.c_str());
		obs_data_set_int(responseData, "companionPort",
				 status.companionPort);
		obs_data_set_string(responseData, "moduleLabel",
				    status.moduleLabel.c_str());
	}
}

void CompanionBridge::ReadWebSocketSettings()
{
	std::lock_guard<std::mutex> lock(m_credentialsMutex);

	// Default fallback values
	m_credentials.host = "127.0.0.1";
	m_credentials.port = 4455;
	m_credentials.authEnabled = false;
	m_credentials.password = "";

#ifdef ENABLE_FRONTEND_API
	config_t *globalConfig = obs_frontend_get_global_config();
	if (!globalConfig)
		return;

	int port = (int)config_get_int(globalConfig, "OBSWebSocket",
				       "ServerPort");
	if (port > 0)
		m_credentials.port = port;

	m_credentials.authEnabled = config_get_bool(
		globalConfig, "OBSWebSocket", "AuthRequired");

	if (m_credentials.authEnabled) {
		const char *pw = config_get_string(
			globalConfig, "OBSWebSocket", "ServerPassword");
		m_credentials.password = pw ? pw : "";
	}
#endif
}

WebSocketCredentials CompanionBridge::GetCurrentCredentials()
{
	std::lock_guard<std::mutex> lock(m_credentialsMutex);
	return m_credentials;
}

void CompanionBridge::OnCompanionConnected(
	const std::string &host, int port, const std::string &moduleLabel)
{
	{
		std::lock_guard<std::mutex> lock(m_statusMutex);
		m_status.discovered = true;
		m_status.companionHost = host;
		m_status.companionPort = port;
		m_status.moduleLabel = moduleLabel;
		auto now = std::chrono::system_clock::now();
		auto t = std::chrono::system_clock::to_time_t(now);
		std::ostringstream oss;
		oss << std::put_time(std::gmtime(&t), "%FT%TZ");
		m_status.lastSeen = oss.str();
	}
	blog(LOG_INFO,
	     "[obs-companion-bridge] Companion connected from %s:%d (label: %s)",
	     host.c_str(), port, moduleLabel.c_str());
	UpdateDockStatus();
}

void CompanionBridge::OnCompanionDisconnected()
{
	{
		std::lock_guard<std::mutex> lock(m_statusMutex);
		m_status.discovered = false;
	}
	blog(LOG_INFO, "[obs-companion-bridge] Companion disconnected");
	UpdateDockStatus();
}

CompanionStatus CompanionBridge::GetStatus() const
{
	std::lock_guard<std::mutex> lock(m_statusMutex);
	return m_status;
}

void CompanionBridge::SettingsMonitorThread()
{
	while (m_running) {
		for (int i = 0; i < 50 && m_running; ++i)
			os_sleep_ms(100);
		if (!m_running)
			break;
		WebSocketCredentials before = GetCurrentCredentials();
		ReadWebSocketSettings();
		WebSocketCredentials after = GetCurrentCredentials();
		if (before.port != after.port ||
		    before.password != after.password ||
		    before.authEnabled != after.authEnabled) {
			blog(LOG_INFO,
			     "[obs-companion-bridge] WebSocket settings changed");
		}
	}
}

void CompanionBridge::UpdateDockStatus()
{
	obs_data_t *eventData = obs_data_create();
	CompanionStatus status = GetStatus();
	obs_data_set_bool(eventData, "companionConnected",
			  status.discovered);
	obs_data_set_string(eventData, "companionHost",
			    status.companionHost.c_str());
	obs_data_set_int(eventData, "companionPort",
			 status.companionPort);
	obs_data_set_string(eventData, "pluginVersion", PLUGIN_VERSION);
	obs_websocket_vendor_emit_event(
		obs_websocket_register_vendor(VENDOR_NAME), "StatusChanged",
		eventData);
	obs_data_release(eventData);
}
