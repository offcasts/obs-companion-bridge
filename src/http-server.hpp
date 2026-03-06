#pragma once
/*
 * http-server.hpp — POSIX socket implementation for macOS/Linux.
 * Replaces the WinSock2 version used on Windows.
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <string>
#include <unordered_map>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <sstream>

struct HttpRequest {
	std::string method, path, body, remoteAddr;
	std::unordered_map<std::string, std::string> headers;
};
struct HttpResponse {
	int status = 200;
	std::string body;
	std::unordered_map<std::string, std::string> headers;
};
using RouteHandler = std::function<void(const HttpRequest &, HttpResponse &)>;

class HttpServer {
public:
	explicit HttpServer(int port) : m_port(port) {}
	~HttpServer() { Stop(); }

	void AddRoute(const std::string &method, const std::string &path, RouteHandler h)
	{
		std::lock_guard<std::mutex> lock(m_routesMutex);
		m_routes[method + ":" + path] = h;
	}

	bool Start()
	{
		m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
		if (m_listenFd < 0) return false;

		int opt = 1;
		setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_port = htons((uint16_t)m_port);

		if (bind(m_listenFd, (sockaddr *)&addr, sizeof(addr)) < 0) return false;
		if (listen(m_listenFd, SOMAXCONN) < 0) return false;

		m_running = true;
		m_acceptThread = std::thread([this]() { AcceptLoop(); });
		return true;
	}

	void Stop()
	{
		m_running = false;
		if (m_listenFd >= 0) { close(m_listenFd); m_listenFd = -1; }
		if (m_acceptThread.joinable()) m_acceptThread.join();
	}

private:
	void AcceptLoop()
	{
		while (m_running) {
			sockaddr_in ca{};
			socklen_t al = sizeof(ca);
			int cs = accept(m_listenFd, (sockaddr *)&ca, &al);
			if (cs < 0) break;
			char ip[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &ca.sin_addr, ip, sizeof(ip));
			std::thread([this, cs, ip = std::string(ip)]() {
				HandleConnection(cs, ip);
			}).detach();
		}
	}

	void HandleConnection(int fd, const std::string &remoteAddr)
	{
		struct timeval tv{ .tv_sec = 2, .tv_usec = 0 };
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

		std::string raw;
		char buf[4096];
		ssize_t r;
		while ((r = recv(fd, buf, sizeof(buf) - 1, 0)) > 0) {
			buf[r] = '\0';
			raw += buf;
			if (raw.find("\r\n\r\n") != std::string::npos) break;
		}

		HttpRequest req;
		HttpResponse res;
		req.remoteAddr = remoteAddr;

		if (!ParseRequest(raw, req)) { SendResponse(fd, 400, {}, "Bad Request"); close(fd); return; }

		res.headers["Access-Control-Allow-Origin"] = "*";
		res.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS";

		if (req.method == "OPTIONS") { res.status = 204; } else { DispatchRoute(req, res); }
		SendResponse(fd, res.status, res.headers, res.body);
		close(fd);
	}

	bool ParseRequest(const std::string &raw, HttpRequest &req)
	{
		if (raw.empty()) return false;
		size_t fl = raw.find("\r\n");
		if (fl == std::string::npos) return false;
		std::string line = raw.substr(0, fl);
		size_t s1 = line.find(' '), s2 = line.rfind(' ');
		if (s1 == std::string::npos || s2 == s1) return false;
		req.method = line.substr(0, s1);
		req.path = line.substr(s1 + 1, s2 - s1 - 1);
		size_t q = req.path.find('?');
		if (q != std::string::npos) req.path = req.path.substr(0, q);
		size_t bs = raw.find("\r\n\r\n");
		if (bs != std::string::npos) {
			std::string hs = raw.substr(fl + 2, bs - fl - 2);
			std::istringstream ss(hs);
			std::string hl;
			while (std::getline(ss, hl)) {
				if (!hl.empty() && hl.back() == '\r') hl.pop_back();
				size_t c = hl.find(':');
				if (c != std::string::npos) req.headers[hl.substr(0, c)] = hl.substr(c + 2);
			}
			req.body = raw.substr(bs + 4);
		}
		return true;
	}

	void DispatchRoute(const HttpRequest &req, HttpResponse &res)
	{
		std::lock_guard<std::mutex> lock(m_routesMutex);
		auto it = m_routes.find(req.method + ":" + req.path);
		if (it != m_routes.end()) { it->second(req, res); }
		else { res.status = 404; res.headers["Content-Type"] = "application/json"; res.body = R"({"error":"not found"})"; }
	}

	void SendResponse(int fd, int status,
			  const std::unordered_map<std::string, std::string> &headers,
			  const std::string &body)
	{
		std::string st = status == 200 ? "OK" : status == 204 ? "No Content" :
				 status == 400 ? "Bad Request" : status == 404 ? "Not Found" : "Error";
		std::string r = "HTTP/1.1 " + std::to_string(status) + " " + st + "\r\n";
		r += "Content-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n";
		for (auto &[k, v] : headers) r += k + ": " + v + "\r\n";
		r += "\r\n" + body;
		send(fd, r.c_str(), r.size(), 0);
	}

	int m_port;
	int m_listenFd = -1;
	std::atomic<bool> m_running{false};
	std::thread m_acceptThread;
	std::unordered_map<std::string, RouteHandler> m_routes;
	std::mutex m_routesMutex;
};
