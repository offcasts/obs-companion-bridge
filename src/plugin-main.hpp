#pragma once

#define PLUGIN_VERSION "1.0.0"
#define PLUGIN_NAME "obs-companion-bridge"

// Default HTTP server port for credential discovery.
#define BRIDGE_HTTP_PORT 28195

// mDNS service type advertised to the local network
#define MDNS_SERVICE_TYPE "_obs-companion._tcp"
#define MDNS_SERVICE_NAME "OBS Companion Bridge"

// obs-websocket vendor name for this plugin
#define VENDOR_NAME "obs-companion-bridge"

// Companion's default HTTP admin port
#define COMPANION_DEFAULT_PORT 8000
