/* Copyright 2021 Aristocratos (jakob@qvantnet.com)

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

indent = tab
tab-size = 4
*/

#define NOMINMAX

#include "btop_websocket.hpp"
#include "btop_shared.hpp"
#include "btop_tools.hpp"
#include "btop_config.hpp"

#include <iostream>
#include <algorithm>
#include <regex>
#include <sstream>
#include <iomanip>
#include <wincrypt.h>

#pragma comment(lib, "ws2_32.lib")

#undef max
#undef min

using std::string, std::vector, std::thread, std::atomic, std::cout, std::endl;
using namespace Tools;

namespace WebSocket {
	
	atomic<bool> server_running{false};
	atomic<bool> should_stop{false};
	int port = 8080;
	thread server_thread;
	vector<Client> clients;
	VT::Renderer vt_renderer(120, 30); // Default terminal size
	SOCKET server_socket = INVALID_SOCKET;
	
	// WebSocket GUID for handshake
	const string WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	
	bool init(int listen_port) {
		port = listen_port;
		
		// Initialize Winsock
		WSADATA wsaData;
		int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (result != 0) {
			Logger::error("WSAStartup failed: " + std::to_string(result));
			return false;
		}
		
		Logger::info("WebSocket server initialized on port " + std::to_string(port));
		return true;
	}
	
	void start() {
		if (server_running) return;
		
		should_stop = false;
		server_thread = thread(serverLoop);
		Logger::info("WebSocket server thread started");
	}
	
	void stop() {
		should_stop = true;
		
		if (server_socket != INVALID_SOCKET) {
			closesocket(server_socket);
			server_socket = INVALID_SOCKET;
		}
		
		// Close all client connections
		for (auto& client : clients) {
			if (client.connected) {
				closesocket(client.socket);
				client.connected = false;
			}
		}
		clients.clear();
		
		if (server_thread.joinable()) {
			server_thread.join();
		}
		
		WSACleanup();
		server_running = false;
		Logger::info("WebSocket server stopped");
	}
	
	void serverLoop() {
		// Prefer IPv6 dual-stack (accepts IPv6 and IPv4) but fall back to IPv4-only
		bool bound = false;
		
		// Try IPv6 socket first
		server_socket = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
		if (server_socket != INVALID_SOCKET) {
			int opt = 1;
			setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
			// Enable dual-stack (IPv4-mapped IPv6 addresses)
			int v6only = 0;
			setsockopt(server_socket, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&v6only, sizeof(v6only));
			
			sockaddr_in6 addr6{};
			addr6.sin6_family = AF_INET6;
			addr6.sin6_addr = in6addr_any;
			addr6.sin6_port = htons(port);
			
			if (bind(server_socket, (sockaddr*)&addr6, sizeof(addr6)) == 0) {
				bound = true;
			}
			else {
				// Close and fall back to IPv4
				closesocket(server_socket);
				server_socket = INVALID_SOCKET;
			}
		}
		
		// Fallback to IPv4-only if IPv6 bind failed
		if (!bound) {
			server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (server_socket == INVALID_SOCKET) {
				Logger::error("Failed to create server socket");
				return;
			}
			int opt = 1;
			setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
			
			sockaddr_in addr4{};
			addr4.sin_family = AF_INET;
			addr4.sin_addr.s_addr = INADDR_ANY;
			addr4.sin_port = htons(port);
			
			if (bind(server_socket, (sockaddr*)&addr4, sizeof(addr4)) != 0) {
				Logger::error("Failed to bind socket: " + std::to_string(WSAGetLastError()));
				closesocket(server_socket);
				return;
			}
		}
		
		// Listen for connections
		if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
			Logger::error("Failed to listen on socket: " + std::to_string(WSAGetLastError()));
			closesocket(server_socket);
			return;
		}
		
		server_running = true;
		Logger::info("WebSocket server listening on port " + std::to_string(port));
		
		while (!should_stop) {
			fd_set read_fds;
			FD_ZERO(&read_fds);
			FD_SET(server_socket, &read_fds);
			
			timeval timeout;
			timeout.tv_sec = 1;
			timeout.tv_usec = 0;
			
			int activity = select(0, &read_fds, nullptr, nullptr, &timeout);
			
			if (activity == SOCKET_ERROR) {
				if (!should_stop) {
					Logger::error("Select failed: " + std::to_string(WSAGetLastError()));
				}
				break;
			}
			
			if (activity > 0 && FD_ISSET(server_socket, &read_fds)) {
				sockaddr_storage client_addr;
				int client_len = sizeof(client_addr);
				SOCKET client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_len);
				
				if (client_socket != INVALID_SOCKET) {
					Logger::info("New WebSocket client connected");
					thread client_thread(handleClient, client_socket);
					client_thread.detach();
				}
			}
			
			cleanupClients();
		}
		
		closesocket(server_socket);
		server_socket = INVALID_SOCKET;
		server_running = false;
	}
	
	void handleClient(SOCKET client_socket) {
		char buffer[4096];
		int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
		
		if (bytes_received <= 0) {
			closesocket(client_socket);
			return;
		}
		
		buffer[bytes_received] = '\0';
		string request(buffer);
		
		if (!performHandshake(client_socket, request)) {
			closesocket(client_socket);
			return;
		}
		
		// Add client to the list
		clients.emplace_back(client_socket);
		
		// Keep connection alive and handle incoming messages
		while (!should_stop) {
			fd_set read_fds;
			FD_ZERO(&read_fds);
			FD_SET(client_socket, &read_fds);
			
			timeval timeout;
			timeout.tv_sec = 1;
			timeout.tv_usec = 0;
			
			int activity = select(0, &read_fds, nullptr, nullptr, &timeout);
			
			if (activity == SOCKET_ERROR || activity == 0) {
				continue;
			}
			
			if (FD_ISSET(client_socket, &read_fds)) {
				bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
				if (bytes_received <= 0) {
					break; // Client disconnected
				}
				
				buffer[bytes_received] = '\0';
				string frame_data = parseFrame(string(buffer, bytes_received));
				
				// Handle ping/pong or other WebSocket frames if needed
				if (!frame_data.empty()) {
					Logger::debug("Received WebSocket message: " + frame_data);
				}
			}
		}
		
		// Remove client from list and close connection
		auto it = std::find_if(clients.begin(), clients.end(), 
			[client_socket](const Client& c) { return c.socket == client_socket; });
		if (it != clients.end()) {
			it->connected = false;
		}
		
		closesocket(client_socket);
		Logger::info("WebSocket client disconnected");
	}
	
	bool performHandshake(SOCKET client_socket, const string& request) {
		string key = extractHeader(request, "Sec-WebSocket-Key");
		if (key.empty()) {
			Logger::error("Missing WebSocket key in handshake");
			return false;
		}
		
		string accept_key = generateAcceptKey(key);
		
		string response = 
			"HTTP/1.1 101 Switching Protocols\r\n"
			"Upgrade: websocket\r\n"
			"Connection: Upgrade\r\n"
			"Sec-WebSocket-Accept: " + accept_key + "\r\n"
			"\r\n";
		
		int bytes_sent = send(client_socket, response.c_str(), (int)response.length(), 0);
		if (bytes_sent != (int)response.length()) {
			Logger::error("Failed to send WebSocket handshake response");
			return false;
		}
		
		return true;
	}
	
	string generateAcceptKey(const string& client_key) {
		string combined = client_key + WS_GUID;
		string hash = sha1Hash(combined);
		return base64Encode(hash);
	}
	
	bool sendFrame(SOCKET client_socket, const string& data) {
		vector<uint8_t> frame;
		frame.push_back(0x81); // FIN=1, opcode=1 (text frame)
		
		size_t payload_len = data.length();
		if (payload_len < 126) {
			frame.push_back((uint8_t)payload_len);
		} else if (payload_len < 65536) {
			frame.push_back(126);
			frame.push_back((uint8_t)(payload_len >> 8));
			frame.push_back((uint8_t)(payload_len & 0xFF));
		} else {
			frame.push_back(127);
			for (int i = 7; i >= 0; i--) {
				frame.push_back((uint8_t)(payload_len >> (i * 8)));
			}
		}
		
		// Add payload
		for (char c : data) {
			frame.push_back((uint8_t)c);
		}
		
		int bytes_sent = send(client_socket, (char*)frame.data(), (int)frame.size(), 0);
		return bytes_sent == (int)frame.size();
	}
	
	string parseFrame(const string& data) {
		if (data.length() < 2) return "";
		
		uint8_t first_byte = data[0];
		uint8_t second_byte = data[1];
		
		bool fin = (first_byte & 0x80) != 0;
		uint8_t opcode = first_byte & 0x0F;
		bool masked = (second_byte & 0x80) != 0;
		uint64_t payload_len = second_byte & 0x7F;
		
		size_t offset = 2;
		
		if (payload_len == 126) {
			if (data.length() < offset + 2) return "";
			payload_len = ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
			offset += 2;
		} else if (payload_len == 127) {
			if (data.length() < offset + 8) return "";
			payload_len = 0;
			for (int i = 0; i < 8; i++) {
				payload_len = (payload_len << 8) | (uint8_t)data[offset + i];
			}
			offset += 8;
		}
		
		if (masked) {
			if (data.length() < offset + 4) return "";
			offset += 4; // Skip mask key for now
		}
		
		if (data.length() < offset + payload_len) return "";
		
		return data.substr(offset, payload_len);
	}
	
    void broadcast(const string& data) {
		if (!server_running) return;
		
        // Data is already processed HTML from processToResoniteHTML
        const string& html_data = data;
		
		auto it = clients.begin();
		while (it != clients.end()) {
			if (!it->connected) {
				it = clients.erase(it);
				continue;
			}
			
			if (!sendFrame(it->socket, html_data)) {
				closesocket(it->socket);
				it->connected = false;
				it = clients.erase(it);
			} else {
				++it;
			}
		}
	}
	
    // Helpers for color conversion placed before parser
    namespace {
        static inline string hex2(int v) {
            static const char* digits = "0123456789abcdef";
            v = std::clamp(v, 0, 255);
            string s(2, '0');
            s[0] = digits[(v >> 4) & 0xF];
            s[1] = digits[v & 0xF];
            return s;
        }
        static inline string rgbToHex(int r, int g, int b) {
            return "#" + hex2(r) + hex2(g) + hex2(b);
        }
        static inline string ansi256ToHex(int n) {
            if (n < 0) n = 0; if (n > 255) n = 255;
            if (n < 16) {
                static const char* table[16] = {
                    "#000000","#800000","#008000","#808000",
                    "#000080","#800080","#008080","#c0c0c0",
                    "#808080","#ff0000","#00ff00","#ffff00",
                    "#0000ff","#ff00ff","#00ffff","#ffffff"
                };
                return table[n];
            }
            if (n >= 232) {
                int v = 8 + 10 * (n - 232);
                return rgbToHex(v, v, v);
            }
            n -= 16;
            int r = n / 36; n %= 36;
            int g = n / 6; int b = n % 6;
            auto map = [](int x){ return x == 0 ? 0 : 55 + 40 * x; };
            return rgbToHex(map(r), map(g), map(b));
        }
    }



	string processToResoniteHTML(const string& ansi_output) {
		// Update VT renderer size to match current terminal size
		if (vt_renderer.getWidth() != Term::width || vt_renderer.getHeight() != Term::height) {
			vt_renderer.resize(Term::width, Term::height);
		}
		
		// Check if this frame contains explicit clear commands
		bool has_clear = (ansi_output.find("\033[2J") != string::npos || 
		                  ansi_output.find("\033[0J") != string::npos ||
		                  ansi_output.find("\033[1J") != string::npos);
		
		// Only clear if we see explicit clear commands or if this looks like a full redraw
		// (starts with cursor positioning to 1,1)
		bool starts_with_home = (ansi_output.find("\033[1;1") == 0 || 
		                         ansi_output.find("\033[0;0") == 0 ||
		                         ansi_output.find("\033[;") == 0);
		
		if (has_clear || starts_with_home) {
			vt_renderer.clear();
		}
		
		// Process the ANSI sequence
		vt_renderer.processSequence(ansi_output);
		
		// Render to Resonite HTML format
		return vt_renderer.renderToResoniteHTML();
	}
	
	void cleanupClients() {
		auto it = clients.begin();
		while (it != clients.end()) {
			if (!it->connected) {
				it = clients.erase(it);
			} else {
				++it;
			}
		}
	}
	
	string extractHeader(const string& request, const string& header_name) {
		size_t pos = request.find(header_name + ": ");
		if (pos == string::npos) return "";
		
		size_t start = pos + header_name.length() + 2;
		size_t end = request.find("\r\n", start);
		if (end == string::npos) return "";
		
		return request.substr(start, end - start);
	}
	
	string base64Encode(const string& input) {
		const string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		string result;
		int val = 0, valb = -6;
		
		for (unsigned char c : input) {
			val = (val << 8) + c;
			valb += 8;
			while (valb >= 0) {
				result.push_back(chars[(val >> valb) & 0x3F]);
				valb -= 6;
			}
		}
		
		if (valb > -6) result.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
		while (result.size() % 4) result.push_back('=');
		
		return result;
	}
	
	string sha1Hash(const string& input) {
		HCRYPTPROV hProv = 0;
		HCRYPTHASH hHash = 0;
		BYTE rgbHash[20];
		DWORD cbHash = sizeof(rgbHash);

		if (!CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
			Logger::error("CryptAcquireContext failed: " + std::to_string(GetLastError()));
			return {};
		}
		if (!CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHash)) {
			Logger::error("CryptCreateHash failed: " + std::to_string(GetLastError()));
			CryptReleaseContext(hProv, 0);
			return {};
		}
		if (!CryptHashData(hHash, reinterpret_cast<const BYTE*>(input.data()), (DWORD)input.size(), 0)) {
			Logger::error("CryptHashData failed: " + std::to_string(GetLastError()));
			CryptDestroyHash(hHash);
			CryptReleaseContext(hProv, 0);
			return {};
		}
		if (!CryptGetHashParam(hHash, HP_HASHVAL, rgbHash, &cbHash, 0)) {
			Logger::error("CryptGetHashParam failed: " + std::to_string(GetLastError()));
			CryptDestroyHash(hHash);
			CryptReleaseContext(hProv, 0);
			return {};
		}
		CryptDestroyHash(hHash);
		CryptReleaseContext(hProv, 0);

		return string(reinterpret_cast<char*>(rgbHash), reinterpret_cast<char*>(rgbHash) + cbHash);
	}
	
}
