#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <thread>
#include <vector>
#include <sstream>
#include <wincrypt.h>
#include "Capture.h"

#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Crypt32.lib")
#pragma comment (lib, "Advapi32.lib")

#define DEFAULT_BUFLEN 1024

class HTTPServer {
private:
	SOCKET ListenSocket = INVALID_SOCKET;
	ScreenCapturer capturer;
	bool running = false;
	std::thread serverThread;
    std::string port = "8090";
    std::string password = "";
    bool useBmp = false;
    bool useGray = false;

	const std::string html_content = R"(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<style>
    body, html { margin: 0; padding: 0; width: 100%; height: 100%; overflow: hidden; background: #000; }
    #container { width: 100%; height: 100%; display: flex; justify-content: center; align-items: center; }
    #screen { 
        width: 100%; height: 100%; object-fit: contain; cursor: none; touch-action: none;
        -webkit-touch-callout: none; -webkit-user-select: none; user-select: none;
    }
    #wait-overlay {
        position: absolute; top: 0; left: 0; width: 100%; height: 100%;
        background: rgba(0,0,0,0.85); color: white; display: flex;
        justify-content: center; align-items: center; font-family: sans-serif;
        font-size: 24px; z-index: 1000;
    }
</style>
</head>
<body>
<div id="wait-overlay">Waiting for connection...</div>
<div id="container">
    <img id="screen" src="/stream" />
</div>
<script>
    const img = document.getElementById('screen');
    const overlay = document.getElementById('wait-overlay');
    
    img.onload = () => {
        overlay.style.display = 'none';
    };

    img.addEventListener('contextmenu', e => e.preventDefault());
    
    // Low-latency WebSocket connection for inputs
    let ws = null;
    function connectWebSocket() {
        const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
        ws = new WebSocket(`${proto}//${location.host}/ws`);
        ws.onclose = () => {
            setTimeout(connectWebSocket, 1000); // Auto reconnect
        };
        ws.onerror = () => {
            if (ws) ws.close();
        };
    }
    connectWebSocket();

    function sendInput(type, x, y, button, extra) {
        if (!img.complete) return;
        let q = `type=${type}&x=${x}&y=${y}&btn=${button}`;
        if (extra) q += `&${extra}`;
        
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send(q);
        } else {
            // Fallback to HTTP POST if WebSocket is not yet connected
            fetch(`/input?${q}`, {method: 'POST'});
        }
    }

    const isMobile = /Android|webOS|iPhone|iPad|iPod|BlackBerry|IEMobile|Opera Mini/i.test(navigator.userAgent) || (navigator.userAgent.includes("Mac") && "ontouchend" in document);

    if (!isMobile) {
        // PC Mode (Mouse Absolute Position)
        let lastMove = 0;
        img.addEventListener('mousemove', e => {
            const now = Date.now();
            if (now - lastMove < 10) return; // High responsiveness (up to ~100Hz)
            lastMove = now;
            
            const rect = img.getBoundingClientRect();
            let nx = (e.clientX - rect.left) / rect.width;
            let ny = (e.clientY - rect.top) / rect.height;
            sendInput('move', nx, ny, 0);
        });

        img.addEventListener('mousedown', e => {
            const rect = img.getBoundingClientRect();
            let nx = (e.clientX - rect.left) / rect.width;
            let ny = (e.clientY - rect.top) / rect.height;
            sendInput('down', nx, ny, e.button);
        });

        img.addEventListener('mouseup', e => {
            const rect = img.getBoundingClientRect();
            let nx = (e.clientX - rect.left) / rect.width;
            let ny = (e.clientY - rect.top) / rect.height;
            sendInput('up', nx, ny, e.button);
        });

        img.addEventListener('wheel', e => {
            e.preventDefault();
            let amount = -e.deltaY; 
            sendInput('wheel', 0, 0, 0, `amount=${amount}`);
        }, {passive: false});

        document.addEventListener('keydown', e => {
            e.preventDefault();
            sendInput('keydown', 0, 0, 0, `key=${e.keyCode}`);
        });
        document.addEventListener('keyup', e => {
            e.preventDefault();
            sendInput('keyup', 0, 0, 0, `key=${e.keyCode}`);
        });
    } else {
        // Mobile Mode (Trackpad Relative Position)
        let touchStartX = 0, touchStartY = 0;
        let lastTouchX = 0, lastTouchY = 0;
        let touchStartTime = 0;
        let lastMove = 0;
        let hasMoved = false;
        let touchCount = 0;

        img.addEventListener('touchstart', e => {
            e.preventDefault();
            touchCount = e.touches.length;
            if (touchCount === 1) {
                touchStartX = lastTouchX = e.touches[0].clientX;
                touchStartY = lastTouchY = e.touches[0].clientY;
                touchStartTime = Date.now();
                hasMoved = false;
            } else if (touchCount === 2) {
                touchStartTime = Date.now();
                hasMoved = false; 
            }
        }, {passive: false});

        img.addEventListener('touchmove', e => {
            e.preventDefault();

            if (e.touches.length === 1) {
                const now = Date.now();
                if (now - lastMove < 8) return; // High responsiveness for touch (up to 120Hz)
                lastMove = now;

                const curX = e.touches[0].clientX;
                const curY = e.touches[0].clientY;
                
                const dx = curX - lastTouchX;
                const dy = curY - lastTouchY;
                
                const sensitivity = 2.0;

                if (Math.abs(curX - touchStartX) > 5 || Math.abs(curY - touchStartY) > 5) {
                    hasMoved = true;
                }

                if (Math.abs(dx) > 0 || Math.abs(dy) > 0) {
                    sendInput('moveRel', dx * sensitivity, dy * sensitivity, 0);
                }
                
                lastTouchX = curX;
                lastTouchY = curY;
            } else if (e.touches.length === 2) {
                hasMoved = true;
            }
        }, {passive: false});

        img.addEventListener('touchend', e => {
            const duration = Date.now() - touchStartTime;
            
            if (!hasMoved && duration < 300) {
                 if (touchCount === 1) {
                     sendInput('tap', 0, 0, 0);
                 } else if (touchCount === 2) {
                     sendInput('tap', 0, 0, 2);
                 }
            }
            touchCount = e.touches.length;
        });
    }
</script>
</body>
</html>
)";

	std::vector<std::string> connectedClients;
    std::mutex clientListMutex;

    bool CheckAuth(const std::string& request) {
        if (password.empty()) return true;

        std::string authPrefix = "Authorization: Basic ";
        size_t authPos = request.find(authPrefix);
        if (authPos == std::string::npos) return false;

        size_t start = authPos + authPrefix.length();
        size_t end = request.find("\r\n", start);
        if (end == std::string::npos) return false;

        std::string encoded = request.substr(start, end - start);
        
        DWORD decodedLen = 0;
        CryptStringToBinaryA(encoded.c_str(), 0, CRYPT_STRING_BASE64, NULL, &decodedLen, NULL, NULL);
        
        std::vector<BYTE> decoded(decodedLen);
        CryptStringToBinaryA(encoded.c_str(), 0, CRYPT_STRING_BASE64, decoded.data(), &decodedLen, NULL, NULL);

        std::string userPass((char*)decoded.data(), decodedLen);
        size_t colPos = userPass.find(":");
        if (colPos == std::string::npos) return false;

        std::string pass = userPass.substr(colPos + 1);
        return pass == password;
    }

    // Compute RFC 6455 Sec-WebSocket-Accept key using Windows Crypto API
    static std::string ComputeWebSocketAccept(const std::string& clientKey) {
        std::string combined = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        
        HCRYPTPROV hProv = 0;
        HCRYPTHASH hHash = 0;
        BYTE sha1Hash[20];
        DWORD hashLen = sizeof(sha1Hash);

        if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
            if (CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHash)) {
                CryptHashData(hHash, (const BYTE*)combined.data(), (DWORD)combined.size(), 0);
                CryptGetHashParam(hHash, HP_HASHVAL, sha1Hash, &hashLen, 0);
                CryptDestroyHash(hHash);
            }
            CryptReleaseContext(hProv, 0);
        }

        DWORD base64Len = 0;
        CryptBinaryToStringA(sha1Hash, hashLen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &base64Len);
        std::string acceptKey(base64Len, '\0');
        CryptBinaryToStringA(sha1Hash, hashLen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &acceptKey[0], &base64Len);
        while (!acceptKey.empty() && (acceptKey.back() == '\0' || acceptKey.back() == '\r' || acceptKey.back() == '\n')) {
            acceptKey.pop_back();
        }
        return acceptKey;
    }

    static bool RecvExact(SOCKET sock, char* buf, int len) {
        int total = 0;
        while (total < len) {
            int r = recv(sock, buf + total, len - total, 0);
            if (r <= 0) return false;
            total += r;
        }
        return true;
    }

    // Handles incoming WebSocket frames on a single persistent thread
    void HandleWebSocket(SOCKET ClientSocket) {
        // Enable TCP_NODELAY
        int nodelayFlag = 1;
        setsockopt(ClientSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelayFlag, sizeof(nodelayFlag));

        while (running) {
            char header[2];
            if (!RecvExact(ClientSocket, header, 2)) break;

            uint8_t b0 = (uint8_t)header[0];
            uint8_t b1 = (uint8_t)header[1];
            uint8_t opcode = b0 & 0x0F;
            bool masked = (b1 & 0x80) != 0;
            uint64_t payloadLen = b1 & 0x7F;

            if (opcode == 0x8) { // Close connection
                break;
            }

            if (payloadLen == 126) {
                uint16_t extLen = 0;
                if (!RecvExact(ClientSocket, (char*)&extLen, 2)) break;
                payloadLen = ntohs(extLen);
            } else if (payloadLen == 127) {
                uint64_t extLen = 0;
                if (!RecvExact(ClientSocket, (char*)&extLen, 8)) break;
                payloadLen = _byteswap_uint64(extLen);
            }

            uint8_t mask[4] = { 0 };
            if (masked) {
                if (!RecvExact(ClientSocket, (char*)mask, 4)) break;
            }

            if (payloadLen > 1024 * 64) {
                // Input commands never exceed 64KB
                break;
            }

            std::string payload((size_t)payloadLen, '\0');
            if (payloadLen > 0) {
                if (!RecvExact(ClientSocket, &payload[0], (int)payloadLen)) break;
                if (masked) {
                    for (size_t i = 0; i < payloadLen; ++i) {
                        payload[i] ^= mask[i % 4];
                    }
                }
            }

            if (opcode == 0x9) { // Ping -> send Pong
                char pong[2] = { (char)0x8A, 0x00 };
                send(ClientSocket, pong, 2, 0);
            } else if (opcode == 0x1 || opcode == 0x2) { // Text or Binary payload
                ProcessInput(payload);
            }
        }
        closesocket(ClientSocket);
    }

	void HandleClient(SOCKET ClientSocket, std::string clientIP) {
        {
            std::lock_guard<std::mutex> lock(clientListMutex);
            connectedClients.push_back(clientIP);
        }

		char recvbuf[DEFAULT_BUFLEN];
		int iResult;

		// Read request (basic)
		iResult = recv(ClientSocket, recvbuf, DEFAULT_BUFLEN - 1, 0);
		if (iResult > 0) {
            recvbuf[iResult] = '\0';
			std::string request(recvbuf, iResult);
            
            if (!CheckAuth(request)) {
                std::string response = "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"RemoteDesktop\"\r\nContent-Length: 0\r\n\r\n";
                send(ClientSocket, response.c_str(), (int)response.length(), 0);
                closesocket(ClientSocket);
                
                {
                    std::lock_guard<std::mutex> lock(clientListMutex);
                    auto it = std::find(connectedClients.begin(), connectedClients.end(), clientIP);
                    if (it != connectedClients.end()) connectedClients.erase(it);
                }
                return;
            }

            // WebSocket Upgrade Request
            if (request.find("Upgrade: websocket") != std::string::npos || request.find("GET /ws") != std::string::npos) {
                std::string keyHeader = "Sec-WebSocket-Key: ";
                size_t kPos = request.find(keyHeader);
                if (kPos != std::string::npos) {
                    size_t start = kPos + keyHeader.length();
                    size_t end = request.find("\r\n", start);
                    std::string clientKey = request.substr(start, end - start);
                    std::string acceptKey = ComputeWebSocketAccept(clientKey);

                    std::string response = 
                        "HTTP/1.1 101 Switching Protocols\r\n"
                        "Upgrade: websocket\r\n"
                        "Connection: Upgrade\r\n"
                        "Sec-WebSocket-Accept: " + acceptKey + "\r\n\r\n";
                    send(ClientSocket, response.c_str(), (int)response.length(), 0);

                    // Transition to persistent WebSocket loop (reusing this thread)
                    HandleWebSocket(ClientSocket);

                    {
                        std::lock_guard<std::mutex> lock(clientListMutex);
                        auto it = std::find(connectedClients.begin(), connectedClients.end(), clientIP);
                        if (it != connectedClients.end()) connectedClients.erase(it);
                    }
                    return;
                }
            }

			if (request.find("GET / ") != std::string::npos || request.find("GET /index.html") != std::string::npos) {
				// Send HTML
				std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: "
					+ std::to_string(html_content.length()) + "\r\n\r\n" + html_content;
				send(ClientSocket, response.c_str(), (int)response.length(), 0);
				closesocket(ClientSocket);
			}
			else if (request.find("GET /stream") != std::string::npos) {
				// Send MJPEG Stream
				std::string header = "HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=boundary\r\n\r\n";
				send(ClientSocket, header.c_str(), (int)header.length(), 0);

                // Enable TCP_NODELAY and expand send buffer to 2MB for lowest streaming latency
                int nodelayFlag = 1;
                setsockopt(ClientSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelayFlag, sizeof(nodelayFlag));
                int sndBufSize = 2 * 1024 * 1024;
                setsockopt(ClientSocket, SOL_SOCKET, SO_SNDBUF, (const char*)&sndBufSize, sizeof(sndBufSize));

                // Wait 1 seconds before starting stream to let connection stabilize
                std::this_thread::sleep_for(std::chrono::seconds(1));

                std::vector<BYTE> imgData;
                imgData.reserve(512 * 1024); // Pre-allocate to prevent repeated reallocations
                uint64_t clientSequence = 0; // Sequence number tracked individually per client connection
                char crlf[] = "\r\n";

                while (running) {
                    // Fully event-driven: blocks until a new frame arrives or 1000ms timeout
                    if (capturer.GetFrame(imgData, clientSequence, 1000)) {
                        std::string contentType = useBmp ? "image/bmp" : "image/jpeg";
                        std::string chunkHeader = "--boundary\r\nContent-Type: " + contentType + "\r\nContent-Length: "
                            + std::to_string(imgData.size()) + "\r\n\r\n";

                        // Single scatter/gather WSASend call to combine header + data + CRLF into one packet
                        WSABUF bufs[3];
                        bufs[0].buf = (char*)chunkHeader.data();
                        bufs[0].len = (ULONG)chunkHeader.size();
                        bufs[1].buf = (char*)imgData.data();
                        bufs[1].len = (ULONG)imgData.size();
                        bufs[2].buf = crlf;
                        bufs[2].len = 2;

                        DWORD bytesSent = 0;
                        if (WSASend(ClientSocket, bufs, 3, &bytesSent, 0, NULL, NULL) == SOCKET_ERROR) break;
                    }
                }
                closesocket(ClientSocket);
			}
			else if (request.find("POST /input") != std::string::npos || request.find("GET /input") != std::string::npos) {
				// Parse Input (HTTP fallback)
				size_t qPos = request.find("?");
				size_t endPos = request.find(" HTTP");
				if (qPos != std::string::npos && endPos != std::string::npos) {
					std::string query = request.substr(qPos + 1, endPos - qPos - 1);
					ProcessInput(query);
				}
				std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
				send(ClientSocket, response.c_str(), (int)response.length(), 0);
				closesocket(ClientSocket);
			}
			else {
				std::string response = "HTTP/1.1 404 Not Found\r\n\r\n";
				send(ClientSocket, response.c_str(), (int)response.length(), 0);
				closesocket(ClientSocket);
			}
		}
		else {
			closesocket(ClientSocket);
		}

        {
            std::lock_guard<std::mutex> lock(clientListMutex);
            auto it = std::find(connectedClients.begin(), connectedClients.end(), clientIP);
            if (it != connectedClients.end()) {
                connectedClients.erase(it);
            }
        }
	}

	void ProcessInput(const std::string& query) {
		// query format: type=move&x=0.5&y=0.5&btn=0&key=13&amount=120
		std::string type;
		float x = 0, y = 0;
		int btn = 0;
        int amount = 0;
        int key = 0;

		size_t p;
		if ((p = query.find("type=")) != std::string::npos) type = query.substr(p + 5, query.find("&", p) - p - 5);
		if ((p = query.find("x=")) != std::string::npos) x = std::stof(query.substr(p + 2, query.find("&", p) - p - 2));
		if ((p = query.find("y=")) != std::string::npos) y = std::stof(query.substr(p + 2, query.find("&", p) - p - 2));
		if ((p = query.find("btn=")) != std::string::npos) btn = std::stoi(query.substr(p + 4, query.find("&", p) - p - 4));
        if ((p = query.find("amount=")) != std::string::npos) amount = std::stoi(query.substr(p + 7, query.find("&", p) - p - 7));
        if ((p = query.find("key=")) != std::string::npos) key = std::stoi(query.substr(p + 4, query.find("&", p) - p - 4));

		INPUT input = { 0 };
		input.type = INPUT_MOUSE;

        if (type == "moveRel") {
            input.mi.dx = (LONG)x;
            input.mi.dy = (LONG)y;
            input.mi.dwFlags = MOUSEEVENTF_MOVE;
            SendInput(1, &input, sizeof(INPUT));
            return;
        }
        else if (type == "tap") {
            DWORD downFlag = (btn == 2) ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
            DWORD upFlag = (btn == 2) ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;

            input.mi.dwFlags = downFlag;
            SendInput(1, &input, sizeof(INPUT));
            input.mi.dwFlags = upFlag;
            SendInput(1, &input, sizeof(INPUT));
            return;
        }
        else if (type == "wheel") {
            input.mi.dwFlags = MOUSEEVENTF_WHEEL;
            input.mi.mouseData = (DWORD)amount;
            SendInput(1, &input, sizeof(INPUT));
            return;
        }
        else if (type == "keydown") {
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = (WORD)key;
            SendInput(1, &input, sizeof(INPUT));
            return;
        }
        else if (type == "keyup") {
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = (WORD)key;
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
            return;
        }

		int absX = (int)(x * 65535);
		int absY = (int)(y * 65535);

		input.mi.dx = absX;
		input.mi.dy = absY;
		input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;

		if (type == "down") {
			if (btn == 0) input.mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
			if (btn == 2) input.mi.dwFlags |= MOUSEEVENTF_RIGHTDOWN;
		}
		else if (type == "up") {
			if (btn == 0) input.mi.dwFlags |= MOUSEEVENTF_LEFTUP;
			if (btn == 2) input.mi.dwFlags |= MOUSEEVENTF_RIGHTUP;
		}

		SendInput(1, &input, sizeof(INPUT));
	}

	void ServerLoop() {
		winrt::init_apartment(); // Initialize MTA for this thread
		capturer.Start();
		
		WSADATA wsaData;
		WSAStartup(MAKEWORD(2, 2), &wsaData);

		struct addrinfo* result = NULL, hints;

		ZeroMemory(&hints, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		hints.ai_flags = AI_PASSIVE;

		getaddrinfo(NULL, port.c_str(), &hints, &result);
		ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
		bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
		freeaddrinfo(result);

		listen(ListenSocket, SOMAXCONN);

		while (running) {
            sockaddr_in clientAddr;
            int clientAddrLen = sizeof(clientAddr);
			SOCKET ClientSocket = accept(ListenSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
			if (ClientSocket != INVALID_SOCKET) {
                char clientIP[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);
                std::string ip(clientIP);

                if (ip == "127.0.0.1" || ip.find("192.168.") == 0) {
				    std::thread(&HTTPServer::HandleClient, this, ClientSocket, ip).detach();
                } else {
                    closesocket(ClientSocket);
                }
			}
		}

		closesocket(ListenSocket);
		WSACleanup();
	}

public:
    void Configure(std::string p, float scale, float quality, std::string pass, bool bmp, bool gray) {
        port = p;
        password = pass;
        useBmp = bmp;
        useGray = gray;
        capturer.SetConfiguration(scale, quality, bmp, gray);
    }

	void Start() {
		running = true;
		serverThread = std::thread(&HTTPServer::ServerLoop, this);
	}

	void Stop() {
		running = false;
		if (ListenSocket != INVALID_SOCKET) {
			closesocket(ListenSocket); // Break accept
		}
		if (serverThread.joinable()) {
			serverThread.join();
		}
        capturer.Stop();
	}

    long long GetLastCaptureDuration() {
        return capturer.GetLastDuration();
    }

    std::string GetPort() {
        return port;
    }

    std::string GetLocalIPAddress() {
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR) return "Unknown";

        struct addrinfo hints = { 0 }, * res = NULL;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        if (getaddrinfo(hostname, NULL, &hints, &res) != 0) return "Unknown";

        std::string bestIP = "";
        for (struct addrinfo* ptr = res; ptr != NULL; ptr = ptr->ai_next) {
            struct sockaddr_in* sockaddr_ipv4 = (struct sockaddr_in*)ptr->ai_addr;
            char ipStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(sockaddr_ipv4->sin_addr), ipStr, INET_ADDRSTRLEN);
            std::string ip(ipStr);

            if (bestIP.empty()) bestIP = ip;
            if (ip.find("192.168.") == 0) {
                bestIP = ip;
                break; // Found preferred IP
            }
        }

        freeaddrinfo(res);
        return bestIP;
    }

    std::string GetConnectedClientInfo() {
        std::lock_guard<std::mutex> lock(clientListMutex);
        if (connectedClients.empty()) {
            return "None";
        }
        std::string info = connectedClients[0];
        if (connectedClients.size() > 1) {
            info += " (+" + std::to_string(connectedClients.size() - 1) + ")";
        }
        return info;
    }
};
