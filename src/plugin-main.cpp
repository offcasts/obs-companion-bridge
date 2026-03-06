#include "plugin-main.hpp"
#include "companion-bridge.hpp"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-companion-bridge", "en-US")

static CompanionBridge *g_bridge = nullptr;

static void on_frontend_event(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		if (g_bridge) g_bridge->OnOBSLoaded();
		break;
	case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN:
	case OBS_FRONTEND_EVENT_EXIT:
		if (g_bridge) g_bridge->Shutdown();
		break;
	default:
		break;
	}
}

bool obs_module_load()
{
	blog(LOG_INFO, "[obs-companion-bridge] Loading plugin v%s", PLUGIN_VERSION);
	g_bridge = new CompanionBridge();
	if (!g_bridge->Initialize()) {
		blog(LOG_ERROR, "[obs-companion-bridge] Failed to initialize bridge");
		delete g_bridge;
		g_bridge = nullptr;
		return false;
	}
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	blog(LOG_INFO, "[obs-companion-bridge] Plugin loaded successfully");
	return true;
}

void obs_module_unload()
{
	blog(LOG_INFO, "[obs-companion-bridge] Unloading plugin");
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	if (g_bridge) {
		g_bridge->Shutdown();
		delete g_bridge;
		g_bridge = nullptr;
	}
	blog(LOG_INFO, "[obs-companion-bridge] Plugin unloaded");
}

MODULE_EXPORT const char *obs_module_description()
{
	return "Bitfocus Companion Bridge - auto-discovers OBS WebSocket "
	       "credentials for Companion without manual configuration.";
}

MODULE_EXPORT const char *obs_module_name()
{
	return "OBS Companion Bridge";
}
