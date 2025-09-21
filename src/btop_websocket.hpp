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

#pragma once

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include "vt_renderer.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

using std::string, std::vector, std::thread, std::atomic;

namespace WebSocket {
	
	extern atomic<bool> server_running;
	extern atomic<bool> should_stop;
	extern int port;
	extern thread server_thread;
	
	struct Client {
		SOCKET socket;
		bool connected;
		string buffer;
		
		Client(SOCKET s) : socket(s), connected(true) {}
	};
	
	extern vector<Client> clients;
	extern VT::Renderer vt_renderer;
	
	//* Initialize WebSocket server
	bool init(int listen_port = 8080);
	
	//* Start WebSocket server in separate thread
	void start();
	
	//* Stop WebSocket server
	void stop();
	
	//* Send data to all connected clients
	void broadcast(const string& data);
	
	//* Process ANSI output through VT renderer and convert to Resonite HTML
	string processToResoniteHTML(const string& ansi_output);
	
	
	//* Handle WebSocket handshake
	bool performHandshake(SOCKET client_socket, const string& request);
	
	//* Generate WebSocket accept key from client key
	string generateAcceptKey(const string& client_key);
	
	//* Send WebSocket frame to client
	bool sendFrame(SOCKET client_socket, const string& data);
	
	//* Parse WebSocket frame from client
	string parseFrame(const string& data);
	
	//* Main server loop function
	void serverLoop();
	
	//* Handle individual client connection
	void handleClient(SOCKET client_socket);
	
	//* Remove disconnected clients from clients vector
	void cleanupClients();
	
	//* Utility function to parse HTTP headers
	string extractHeader(const string& request, const string& header_name);
	
	//* Base64 encode function for WebSocket handshake
	string base64Encode(const string& input);
	
	//* SHA1 hash function for WebSocket handshake
	string sha1Hash(const string& input);
	
}
