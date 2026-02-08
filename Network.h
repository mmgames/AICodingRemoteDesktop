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

#define DEFAULT_BUFLEN 512

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
    
    function sendInput(type, x, y, button) {
        if (!img.complete) return;
        fetch(`/input?type=${type}&x=${x}&y=${y}&btn=${button}`, {method: 'POST'});
    }

    const isMobile = /Android|webOS|iPhone|iPad|iPod|BlackBerry|IEMobile|Opera Mini/i.test(navigator.userAgent) || (navigator.userAgent.includes("Mac") && "ontouchend" in document);

    if (!isMobile) {
        // PC Mode (Mouse Absolute Position)
        let lastMove = 0;
        img.addEventListener('mousemove', e => {
            const now = Date.now();
            if (now - lastMove < 30) return; // Throttle
            lastMove = now;
            
            const rect = img.getBoundingClientRect();
            // Calculate normalized coordinates (0.0 to 1.0)
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
            // Windows WHEEL_DELTA is 120. Invert deltaY for correct direction.
            // Scale multiplier can be adjusted if too slow/fast.
            let amount = -e.deltaY; 
            fetch(`/input?type=wheel&amount=${amount}&x=0&y=0&btn=0`, {method: 'POST'});
        }, {passive: false});

        document.addEventListener('keydown', e => {
            e.preventDefault();
            fetch(`/input?type=keydown&key=${e.keyCode}&x=0&y=0&btn=0`, {method: 'POST'});
        });
        document.addEventListener('keyup', e => {
            e.preventDefault();
            fetch(`/input?type=keyup&key=${e.keyCode}&x=0&y=0&btn=0`, {method: 'POST'});
        });
    } else {
        // Mobile Mode (Trackpad Relative Position)
        let touchStartX = 0, touchStartY = 0;
        let lastTouchX = 0, lastTouchY = 0;
        let touchStartTime = 0;
        let lastMove = 0;
        let hasMoved = false;
        let touchCount = 0; // distinct from e.touches.length to some extent

        img.addEventListener('touchstart', e => {
            e.preventDefault();
            touchCount = e.touches.length;
            if (touchCount === 1) {
                touchStartX = lastTouchX = e.touches[0].clientX;
                touchStartY = lastTouchY = e.touches[0].clientY;
                touchStartTime = Date.now();
                hasMoved = false;
            } else if (touchCount === 2) {
                // Prepare for 2-finger tap check
                touchStartTime = Date.now();
                hasMoved = false; 
            }
        }, {passive: false});

        img.addEventListener('touchmove', e => {
            e.preventDefault(); // Prevent scrolling

            if (e.touches.length === 1) {
                const now = Date.now();
                if (now - lastMove < 15) return; 
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
                    fetch(`/input?type=moveRel&x=${dx * sensitivity}&y=${dy * sensitivity}&btn=0`, {method: 'POST'});
                }
                
                lastTouchX = curX;
                lastTouchY = curY;
            } else if (e.touches.length === 2) {
                // If moving with 2 fingers, cancel tap
                hasMoved = true;
            }
        }, {passive: false});

        img.addEventListener('touchend', e => {
             // e.touches is remaining touches. Check original intention via touchCount
            const duration = Date.now() - touchStartTime;
            
            if (!hasMoved && duration < 300) {
                 if (touchCount === 1) {
                     fetch(`/input?type=tap&x=0&y=0&btn=0`, {method: 'POST'});
                 } else if (touchCount === 2) {
                     fetch(`/input?type=tap&x=0&y=0&btn=2`, {method: 'POST'});
                 }
            }
            touchCount = e.touches.length; // Update count
        });
    }

    // Keyboard support can be added similarly
</script>
</body>
</html>
)";

	std::vector<std::string> connectedClients;
    std::mutex clientListMutex;

    bool CheckAuth(std::string request) {
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

	void HandleClient(SOCKET ClientSocket, std::string clientIP) {
        {
            std::lock_guard<std::mutex> lock(clientListMutex);
            connectedClients.push_back(clientIP);
        }

		char recvbuf[DEFAULT_BUFLEN];
		int iResult;

		// Read request (basic)
		iResult = recv(ClientSocket, recvbuf, DEFAULT_BUFLEN, 0);
		if (iResult > 0) {
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

                // Wait 1 seconds before starting stream to let connection stabilize
                std::this_thread::sleep_for(std::chrono::seconds(1));

				while (running) {
					std::vector<BYTE> imgData;
					if (capturer.CaptureScreen(imgData)) {
                        std::string contentType = useBmp ? "image/bmp" : "image/jpeg";
						std::string chunkHeader = "--boundary\r\nContent-Type: " + contentType + "\r\nContent-Length: "
							+ std::to_string(imgData.size()) + "\r\n\r\n";

						if (send(ClientSocket, chunkHeader.c_str(), (int)chunkHeader.length(), 0) == SOCKET_ERROR) break;
						if (send(ClientSocket, (const char*)imgData.data(), (int)imgData.size(), 0) == SOCKET_ERROR) break;
						if (send(ClientSocket, "\r\n", 2, 0) == SOCKET_ERROR) break;
					}
				}
				closesocket(ClientSocket);
			}
			else if (request.find("POST /input") != std::string::npos || request.find("GET /input") != std::string::npos) {
				// Parse Input
				// Query string parsing simplified
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

	void ProcessInput(std::string query) {
		// query format: type=move&x=0.5&y=0.5&btn=0
		std::string type;
		float x = 0, y = 0;
		int btn = 0;
        int amount = 0;

		// Very basic parsing
		size_t p;
		if ((p = query.find("type=")) != std::string::npos) type = query.substr(p + 5, query.find("&", p) - p - 5);
		if ((p = query.find("x=")) != std::string::npos) x = std::stof(query.substr(p + 2, query.find("&", p) - p - 2));
		if ((p = query.find("y=")) != std::string::npos) y = std::stof(query.substr(p + 2, query.find("&", p) - p - 2));
		if ((p = query.find("btn=")) != std::string::npos) btn = std::stoi(query.substr(p + 4));
		if ((p = query.find("btn=")) != std::string::npos) btn = std::stoi(query.substr(p + 4));
        if ((p = query.find("amount=")) != std::string::npos) amount = std::stoi(query.substr(p + 7));
        int key = 0;
        if ((p = query.find("key=")) != std::string::npos) key = std::stoi(query.substr(p + 4));

		int screenW = GetSystemMetrics(SM_CXSCREEN);
		int screenH = GetSystemMetrics(SM_CYSCREEN);

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
