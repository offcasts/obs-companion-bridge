#pragma once
/*
 * mdns-advertiser.hpp — macOS implementation using dns_sd (Bonjour).
 * Bonjour is built into macOS — no extra dependencies needed.
 *
 * On Windows this file uses DnsServiceRegister instead.
 * On Linux it would use Avahi.
 */
#include <dns_sd.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstring>

struct MdnsServiceInfo {
	std::string serviceType;   // e.g. "_obs-companion._tcp"
	std::string serviceName;   // e.g. "OBS Companion Bridge"
	int port;
	std::unordered_map<std::string, std::string> txtRecords;
};

class MdnsAdvertiser {
public:
	MdnsAdvertiser() = default;
	~MdnsAdvertiser() { Unregister(); }

	bool Register(const MdnsServiceInfo &info)
	{
		m_info = info;

		// Build TXT record in DNS-SD wire format
		// Each entry: <length><key=value>
		std::vector<uint8_t> txt;
		for (auto &[k, v] : info.txtRecords) {
			std::string entry = k + "=" + v;
			if (entry.size() > 255) entry = entry.substr(0, 255);
			txt.push_back((uint8_t)entry.size());
			txt.insert(txt.end(), entry.begin(), entry.end());
		}

		DNSServiceErrorType err = DNSServiceRegister(
			&m_sdRef,
			0,                          // flags
			kDNSServiceInterfaceIndexAny,
			info.serviceName.c_str(),   // instance name
			info.serviceType.c_str(),   // service type  e.g. _obs-companion._tcp
			nullptr,                    // domain (default .local)
			nullptr,                    // host (default = local hostname)
			htons((uint16_t)info.port),
			(uint16_t)txt.size(),
			txt.empty() ? nullptr : txt.data(),
			RegisterCallback,
			this
		);

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
	static void DNSSD_API RegisterCallback(DNSServiceRef /*ref*/,
					       DNSServiceFlags /*flags*/,
					       DNSServiceErrorType /*err*/,
					       const char * /*name*/,
					       const char * /*regtype*/,
					       const char * /*domain*/,
					       void * /*context*/) {}

	MdnsServiceInfo m_info;
	DNSServiceRef m_sdRef = nullptr;
	bool m_registered = false;
};
