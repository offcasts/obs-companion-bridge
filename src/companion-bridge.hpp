#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <obs-module.h>

struct WebSocketCredentials {
	std::string host;
	int port;
	std::string password;
	bool authEnabled;
};

struct CompanionStatus {
	bool discovered;
	std::string companionHost;
	int companionPort;
	std::string moduleLabel;
	std::string lastSeen;
};

class CompanionBridge {
public:
	CompanionBridge();
	~CompanionBridge();

	bool Initialize();
	void OnOBSLoaded();
	void Shutdown();

	WebSocketCredentials GetCurrentCredentials();

	void OnCompanionConnected(const std::string &host, int port,
				  const std::string &moduleLabel);
	void OnCompanionDisconnected();

	void HandleVendorRequest(const std::string &requestType,
				 obs_data_t *requestData,
				 obs_data_t *responseData);

	CompanionStatus GetStatus() const;

private:
	void StartHttpServer();
	void StopHttpServer();
	void StartMdnsAdvertiser();
	void StopMdnsAdvertiser();
	void RegisterObsWebSocketVendor();
	void UnregisterObsWebSocketVendor();
	void ReadWebSocketSettings();
	void UpdateDockStatus();
	void SettingsMonitorThread();

	WebSocketCredentials m_credentials;
	CompanionStatus m_status;

	mutable std::mutex m_credentialsMutex;
	mutable std::mutex m_statusMutex;

	std::atomic<bool> m_running{false};
	std::thread m_settingsMonitorThread;

	struct Impl;
	Impl *m_impl;
};
