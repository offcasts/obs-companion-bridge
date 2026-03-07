#pragma once
/*
 * http-server.hpp — minimal single-threaded HTTP/1.1 server
 *
 * Platform routing:
 *   Windows → WinSock2 (ws2_32.lib, already linked in CMakeLists)
 *   macOS/Linux → POSIX BSD sockets
 */

#include <string>
#include <unordered_map>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>

// ─── Platform socket includes ─────────────────────────────────────────────────
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
static const socket_t INVALID_SOCK = INVALID_SOCKET;
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int socket_t;
static const socket_t INVALID_SOCK = -1;
#endif

// ─── Portable close / error helpers ──────────────────────────────────────────
#if defined(_WIN32)
static inline int sock_close(socket_t s) { return closesocket(s); }
static inline int sock_error() { return WSAGetLastError(); }
#else
static inline int sock_close(socket_t s) { return close(s); }
static inline int sock_error() { return errno; }
#endif

struct HttpRequest {
	std::string method;
	std::string path;
	std::unordered_map<std::string, std::string> headers;
	std::string body;
};

struct HttpResponse {
	int status = 200;
	std::unordered_map<std::string, std::string> headers;
	std::string body;
};

using HttpHandler =
	std::function<void(const HttpRequest &, HttpResponse &)>;

class HttpServer {
public:
	explicit HttpServer(int port) : m_port(port) {}

	~HttpServer() { Stop(); }

	void AddRoute(const std::string &method, const std::string &path,
		      HttpHandler handler)
	{
		m_routes.push_back({method, path, std::move(handler)});
	}

	bool Start()
	{
#if defined(_WIN32)
		WSADATA wsa;
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
			return false;
#endif
		m_serverSock = socket(AF_INET, SOCK_STREAM, 0);
		if (m_serverSock == INVALID_SOCK)
			return false;

		int opt = 1;
#if defined(_WIN32)
		setsockopt(m_serverSock, SOL_SOCKET, SO_REUSEADDR,
			   (const char *)&opt, sizeof(opt));
#else
		setsockopt(m_serverSock, SOL_SOCKET, SO_REUSEADDR, &opt,
			   sizeof(opt));
#endif

		struct sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons((uint16_t)m_port);

		if (bind(m_serverSock, (struct sockaddr *)&addr,
			 sizeof(addr)) < 0) {
			sock_close(m_serverSock);
			m_serverSock = INVALID_SOCK;
			return false;
		}

		if (listen(m_serverSock, 10) < 0) {
			sock_close(m_serverSock);
			m_serverSock = INVALID_SOCK;
			return false;
		}

		m_running = true;
		m_acceptThread =
			std::thread([this]() { AcceptLoop(); });
		return true;
	}

	void Stop()
	{
		m_running = false;
		if (m_serverSock != INVALID_SOCK) {
			sock_close(m_serverSock);
			m_serverSock = INVALID_SOCK;
		}
		if (m_acceptThread.joinable())
			m_acceptThread.join();
#if defined(_WIN32)
		WSACleanup();
#endif
	}

private:
	struct Route {
		std::string method;
		std::string path;
		HttpHandler handler;
	};

	void AcceptLoop()
	{
		while (m_running) {
			struct sockaddr_in clientAddr = {};
#if defined(_WIN32)
			int addrLen = sizeof(clientAddr);
#else
			socklen_t addrLen = sizeof(clientAddr);
#endif
			socket_t clientSock =
				accept(m_serverSock,
				       (struct sockaddr *)&clientAddr,
				       &addrLen);
			if (clientSock == INVALID_SOCK)
				continue;
			HandleClient(clientSock);
		}
	}

	void HandleClient(socket_t sock)
	{
		// Read request (simplified — no chunked encoding)
		std::string raw;
		char buf[4096];
		while (true) {
#if defined(_WIN32)
			int n = recv(sock, buf, sizeof(buf) - 1, 0);
#else
			ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
#endif
			if (n <= 0)
				break;
			buf[n] = '\0';
			raw += buf;
			// Stop when we have full headers (and for POST, a body)
			if (raw.find("\r\n\r\n") != std::string::npos)
				break;
		}

		HttpRequest req = ParseRequest(raw);
		HttpResponse res;

		bool matched = false;
		for (auto &route : m_routes) {
			if (route.method == req.method &&
			    route.path == req.path) {
				route.handler(req, res);
				matched = true;
				break;
			}
		}

		if (!matched) {
			res.status = 404;
			res.body = R"({"error":"not found"})";
			res.headers["Content-Type"] = "application/json";
		}

		SendResponse(sock, res);
		sock_close(sock);
	}

	static HttpRequest ParseRequest(const std::string &raw)
	{
		HttpRequest req;
		size_t lineEnd = raw.find("\r\n");
		if (lineEnd == std::string::npos)
			return req;

		std::string requestLine = raw.substr(0, lineEnd);
		size_t sp1 = requestLine.find(' ');
		size_t sp2 = requestLine.find(' ', sp1 + 1);
		if (sp1 == std::string::npos || sp2 == std::string::npos)
			return req;

		req.method = requestLine.substr(0, sp1);
		req.path = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);

		size_t pos = lineEnd + 2;
		while (pos < raw.size()) {
			size_t end = raw.find("\r\n", pos);
			if (end == std::string::npos || end == pos)
				break;
			std::string header = raw.substr(pos, end - pos);
			size_t colon = header.find(':');
			if (colon != std::string::npos) {
				std::string key = header.substr(0, colon);
				std::string val = header.substr(colon + 1);
				while (!val.empty() && val[0] == ' ')
					val = val.substr(1);
				req.headers[key] = val;
			}
			pos = end + 2;
		}

		size_t bodyStart = raw.find("\r\n\r\n");
		if (bodyStart != std::string::npos)
			req.body = raw.substr(bodyStart + 4);

		return req;
	}

	static void SendResponse(socket_t sock, const HttpResponse &res)
	{
		std::string statusText = "OK";
		if (res.status == 400)
			statusText = "Bad Request";
		else if (res.status == 404)
			statusText = "Not Found";
		else if (res.status == 500)
			statusText = "Internal Server Error";

		std::string response = "HTTP/1.1 " +
				       std::to_string(res.status) + " " +
				       statusText + "\r\n";
		response += "Content-Length: " +
			    std::to_string(res.body.size()) + "\r\n";
		response += "Connection: close\r\n";
		response += "Access-Control-Allow-Origin: *\r\n";

		for (auto &[k, v] : res.headers)
			response += k + ": " + v + "\r\n";

		response += "\r\n";
		response += res.body;

		send(sock, response.c_str(), (int)response.size(), 0);
	}

	int m_port;
	socket_t m_serverSock = INVALID_SOCK;
	std::atomic<bool> m_running{false};
	std::thread m_acceptThread;
	std::vector<Route> m_routes;
};
