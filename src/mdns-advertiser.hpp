#pragma once
/*
 * mdns-advertiser.hpp — cross-platform mDNS advertisement
 *
 * Platform routing:
 *   Windows → DnsServiceRegister (dnsapi, built-in since Win10 1809)
 *   macOS   → dns_sd / DNSServiceRegister (Bonjour, built into macOS)
 *   Linux   → Avahi dns_sd compatibility layer (libavahi-compat-libdnssd-dev)
 *             Falls back to a no-op stub if Avahi is not installed,
 *             so the plugin still loads — mDNS just won't advertise.
 */

#include <string>
#include <unordered_map>
#include <vector>
#include <cstring>

struct MdnsServiceInfo {
	std::string serviceType;  // e.g. "_obs-companion._tcp"
	std::string serviceName;  // e.g. "OBS Companion Bridge"
	int port;
	std::unordered_map<std::string, std::string> txtRecords;
};

// ─────────────────────────────────────────────────────────────────────────────
// Windows implementation — DnsServiceRegister (dnsapi.lib)
// ─────────────────────────────────────────────────────────────────────────────
#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windns.h>
#pragma comment(lib, "dnsapi.lib")

class MdnsAdvertiser {
public:
	MdnsAdvertiser() = default;
	~MdnsAdvertiser() { Unregister(); }

	bool Register(const MdnsServiceInfo &info)
	{
		m_info = info;
		m_instanceName =
			info.serviceName + "." + info.serviceType + ".local";
		std::wstring wName(m_instanceName.begin(),
				   m_instanceName.end());

		std::vector<std::wstring> keys, vals;
		std::vector<LPWSTR> kptrs, vptrs;
		for (auto &[k, v] : info.txtRecords) {
			keys.push_back(std::wstring(k.begin(), k.end()));
			vals.push_back(std::wstring(v.begin(), v.end()));
		}
		for (size_t i = 0; i < keys.size(); i++) {
			kptrs.push_back(
				const_cast<LPWSTR>(keys[i].c_str()));
			vptrs.push_back(
				const_cast<LPWSTR>(vals[i].c_str()));
		}

		DNS_SERVICE_INSTANCE si = {};
		si.pszInstanceName =
			const_cast<LPWSTR>(wName.c_str());
		si.wPort = (WORD)info.port;
		si.dwPropertyCount = (DWORD)info.txtRecords.size();
		si.keys = si.dwPropertyCount > 0 ? kptrs.data() : nullptr;
		si.values =
			si.dwPropertyCount > 0 ? vptrs.data() : nullptr;

		DNS_SERVICE_REGISTER_REQUEST req = {};
		req.Version = DNS_QUERY_REQUEST_VERSION1;
		req.pServiceInstance = &si;
		req.pRegisterCompletionCallback = RegisterCb;
		req.pQueryContext = this;
		req.unicastEnabled = FALSE;

		DWORD result = DnsServiceRegister(&req, &m_cancel);
		m_registered =
			(result == DNS_REQUEST_PENDING ||
			 result == ERROR_SUCCESS);
		return m_registered;
	}

	void Unregister()
	{
		if (m_registered) {
			m_registered = false;
			DnsServiceDeRegister(&m_cancel, nullptr);
		}
	}

private:
	static VOID WINAPI RegisterCb(DWORD, PVOID,
				      PDNS_SERVICE_INSTANCE) {}

	MdnsServiceInfo m_info;
	std::string m_instanceName;
	DNS_SERVICE_CANCEL m_cancel = {};
	bool m_registered = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// macOS + Linux implementation — dns_sd / Bonjour / Avahi compat
// ─────────────────────────────────────────────────────────────────────────────
#elif defined(__APPLE__) || defined(__linux__)

// On Linux, install: sudo apt install libavahi-compat-libdnssd-dev
// The header provides the same DNSServiceRegister API as Apple's Bonjour.
// If not available we fall through to the stub below via the #else.
#if defined(__APPLE__)
#include <dns_sd.h>
#define HAVE_DNS_SD 1
#elif defined(__linux__)
// Avahi ships a dns_sd compatibility header at this path
#if __has_include(<avahi-compat-libdns_sd/dns_sd.h>)
#include <avahi-compat-libdns_sd/dns_sd.h>
#define HAVE_DNS_SD 1
#elif __has_include(<dns_sd.h>)
#include <dns_sd.h>
#define HAVE_DNS_SD 1
#else
#define HAVE_DNS_SD 0
#endif
#endif // __APPLE__ / __linux__

#if HAVE_DNS_SD

class MdnsAdvertiser {
public:
	MdnsAdvertiser() = default;
	~MdnsAdvertiser() { Unregister(); }

	bool Register(const MdnsServiceInfo &info)
	{
		m_info = info;

		// Build TXT record wire format: <len><key=value>...
		std::vector<uint8_t> txt;
		for (auto &[k, v] : info.txtRecords) {
			std::string entry = k + "=" + v;
			if (entry.size() > 255)
				entry = entry.substr(0, 255);
			txt.push_back((uint8_t)entry.size());
			txt.insert(txt.end(), entry.begin(), entry.end());
		}

		DNSServiceErrorType err = DNSServiceRegister(
			&m_sdRef,
			0,
			kDNSServiceInterfaceIndexAny,
			info.serviceName.c_str(),
			info.serviceType.c_str(),
			nullptr, // domain (.local)
			nullptr, // host (local hostname)
			htons((uint16_t)info.port),
			(uint16_t)txt.size(),
			txt.empty() ? nullptr : txt.data(),
			RegisterCallback,
			this);

		m_registered = (err == kDNSServiceErr_NoError);
		return m_registered;
	}

	void Unregister()
	{
		if (m_registered && m_sdRef) {
			DNSServiceRefDeallocate(m_sdRef);
			m_sdRef = nullptr;
			m_registered = false;
		}
	}

private:
	static void DNSSD_API
	RegisterCallback(DNSServiceRef, DNSServiceFlags,
			 DNSServiceErrorType, const char *, const char *,
			 const char *, void *) {}

	MdnsServiceInfo m_info;
	DNSServiceRef m_sdRef = nullptr;
	bool m_registered = false;
};

#else // HAVE_DNS_SD == 0 — no mDNS library available, use stub

#include <obs-module.h>

class MdnsAdvertiser {
public:
	MdnsAdvertiser() = default;
	~MdnsAdvertiser() = default;

	bool Register(const MdnsServiceInfo &)
	{
		blog(LOG_WARNING,
		     "[obs-companion-bridge] mDNS unavailable on this "
		     "Linux build (install libavahi-compat-libdnssd-dev). "
		     "Companion will need manual IP configuration.");
		return false;
	}

	void Unregister() {}
};

#endif // HAVE_DNS_SD

#else
// ─────────────────────────────────────────────────────────────────────────────
// Unknown platform — no-op stub so the plugin still compiles
// ─────────────────────────────────────────────────────────────────────────────
class MdnsAdvertiser {
public:
	bool Register(const MdnsServiceInfo &) { return false; }
	void Unregister() {}
};

#endif // platform
