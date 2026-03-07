#pragma once
/*
 * mdns-advertiser.hpp — cross-platform mDNS advertisement
 *
 * Platform routing:
 *   Windows → DnsServiceRegister (dnsapi, built-in since Win10 1809)
 *   macOS   → dns_sd / DNSServiceRegister (Bonjour, built into macOS)
 *   Linux   → Avahi dns_sd compatibility layer (libavahi-compat-libdnssd-dev)
 *             Falls back to a no-op stub if Avahi is not installed.
 */

#include <string>
#include <unordered_map>
#include <vector>
#include <cstring>

struct MdnsServiceInfo {
	std::string serviceType;
	std::string serviceName;
	int port;
	std::unordered_map<std::string, std::string> txtRecords;
};

// ─────────────────────────────────────────────────────────────────────────────
// Windows — DnsServiceRegister (dnsapi.lib)
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
		m_instanceName =
			info.serviceName + "." + info.serviceType + ".local";
		m_wInstanceName = std::wstring(m_instanceName.begin(),
					       m_instanceName.end());

		// Build TXT key/value arrays (must outlive the Register call)
		for (auto &[k, v] : info.txtRecords) {
			m_keys.push_back(
				std::wstring(k.begin(), k.end()));
			m_vals.push_back(
				std::wstring(v.begin(), v.end()));
		}
		for (size_t i = 0; i < m_keys.size(); ++i) {
			m_kptrs.push_back(
				const_cast<PWSTR>(m_keys[i].c_str()));
			m_vptrs.push_back(
				const_cast<PWSTR>(m_vals[i].c_str()));
		}

		// Service instance (storage lives in this object)
		m_instance = {};
		m_instance.pszInstanceName =
			const_cast<PWSTR>(m_wInstanceName.c_str());
		m_instance.wPort = (WORD)info.port;
		m_instance.dwPropertyCount = (DWORD)m_keys.size();
		m_instance.keys =
			m_kptrs.empty() ? nullptr : m_kptrs.data();
		m_instance.values =
			m_vptrs.empty() ? nullptr : m_vptrs.data();

		// Register request — we keep this so Unregister can pass it
		// to DnsServiceDeRegister (which requires the original request,
		// not the cancel handle).
		m_request = {};
		m_request.Version = DNS_QUERY_REQUEST_VERSION1;
		m_request.pServiceInstance = &m_instance;
		m_request.pRegisterCompletionCallback = RegisterCb;
		m_request.pQueryContext = this;
		m_request.unicastEnabled = FALSE;

		DWORD result = DnsServiceRegister(&m_request, &m_cancel);
		m_registered =
			(result == DNS_REQUEST_PENDING ||
			 result == ERROR_SUCCESS);
		return m_registered;
	}

	void Unregister()
	{
		if (!m_registered)
			return;
		m_registered = false;
		// DnsServiceDeRegister takes PDNS_SERVICE_REGISTER_REQUEST,
		// NOT PDNS_SERVICE_CANCEL — we pass the original request.
		DnsServiceDeRegister(&m_request, nullptr);
	}

private:
	static VOID WINAPI RegisterCb(DWORD, PVOID,
				      PDNS_SERVICE_INSTANCE) {}

	std::string m_instanceName;
	std::wstring m_wInstanceName;
	std::vector<std::wstring> m_keys, m_vals;
	std::vector<PWSTR> m_kptrs, m_vptrs;

	DNS_SERVICE_INSTANCE m_instance = {};
	DNS_SERVICE_REGISTER_REQUEST m_request = {};
	DNS_SERVICE_CANCEL m_cancel = {};
	bool m_registered = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// macOS + Linux — dns_sd / Bonjour / Avahi compat
// ─────────────────────────────────────────────────────────────────────────────
#elif defined(__APPLE__) || defined(__linux__)

#if defined(__APPLE__)
#include <dns_sd.h>
#define HAVE_DNS_SD 1
#elif defined(__linux__)
#if __has_include(<avahi-compat-libdns_sd/dns_sd.h>)
#include <avahi-compat-libdns_sd/dns_sd.h>
#define HAVE_DNS_SD 1
#elif __has_include(<dns_sd.h>)
#include <dns_sd.h>
#define HAVE_DNS_SD 1
#else
#define HAVE_DNS_SD 0
#endif
#endif

#if HAVE_DNS_SD

class MdnsAdvertiser {
public:
	MdnsAdvertiser() = default;
	~MdnsAdvertiser() { Unregister(); }

	bool Register(const MdnsServiceInfo &info)
	{
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
			nullptr,
			nullptr,
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

	DNSServiceRef m_sdRef = nullptr;
	bool m_registered = false;
};

#else // No mDNS library — stub

#include <obs-module.h>

class MdnsAdvertiser {
public:
	bool Register(const MdnsServiceInfo &)
	{
		blog(LOG_WARNING,
		     "[obs-companion-bridge] mDNS unavailable (install "
		     "libavahi-compat-libdnssd-dev). "
		     "Companion will need manual IP configuration.");
		return false;
	}
	void Unregister() {}
};

#endif // HAVE_DNS_SD

#else
// Unknown platform — no-op stub
class MdnsAdvertiser {
public:
	bool Register(const MdnsServiceInfo &) { return false; }
	void Unregister() {}
};
#endif
