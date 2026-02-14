// client.cpp - High-performance monitor extender client with SDL2
// Compile: g++ -std=c++17 client.cpp -o client -lws2_32 -lSDL2 -lSDL2main -O3
// Or: g++ -std=c++17 client.cpp -o client -lws2_32 -I<SDL2_include> -L<SDL2_lib> -lSDL2 -O3

#include <winsock2.h>
#include <ws2tcpip.h>
#include <SDL2/SDL.h>
#include <iostream>
#include <vector>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "SDL2.lib")
#pragma comment(lib, "SDL2main.lib")

const int PORT = 9999;
const int SCREEN_WIDTH = 960;
const int SCREEN_HEIGHT = 540;

// Byte order conversion for 64-bit integers
inline uint64_t ntohll(uint64_t value) {
    static const int num = 42;
    if (*(char*)&num == 42) {
        uint32_t high_part = ntohl((uint32_t)(value >> 32));
        uint32_t low_part = ntohl((uint32_t)(value & 0xFFFFFFFFLL));
        return ((uint64_t)low_part << 32) | high_part;
    }
    return value;
}

// Helper to receive exact amount of bytes
bool RecvAll(SOCKET sock, char* buffer, int length) {
    int total_received = 0;
    while (total_received < length) {
        int received = recv(sock, buffer + total_received, length - total_received, 0);
        if (received <= 0) {
            return false; // Connection closed or error
        }
        total_received += received;
    }
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <server_ip>" << std::endl;
        return 1;
    }

    const char* server_ip = argv[1];

    std::cout << "[INFO] Monitor Extender Client (C++)" << std::endl;
    std::cout << "[INFO] Connecting to " << server_ip << ":" << PORT << std::endl;

    // Initialize Winsock
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "[ERROR] WSAStartup failed" << std::endl;
        return 1;
    }

    // Create socket
    SOCKET client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client_socket == INVALID_SOCKET) {
        std::cerr << "[ERROR] Failed to create socket" << std::endl;
        WSACleanup();
        return 1;
    }

    // Enable TCP_NODELAY
    int flag = 1;
    setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));

    // Connect
    sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    if (connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "[ERROR] Connection failed: " << WSAGetLastError() << std::endl;
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "[INFO] Connected to server!" << std::endl;

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "[ERROR] SDL_Init failed: " << SDL_GetError() << std::endl;
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    // Create fullscreen window
    SDL_Window* window = SDL_CreateWindow(
        "Monitor Extender",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP
    );

    if (!window) {
        std::cerr << "[ERROR] Failed to create window: " << SDL_GetError() << std::endl;
        SDL_Quit();
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    // Create renderer (hardware accelerated)
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "[ERROR] Failed to create renderer: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    // Create texture for streaming
    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH,
        SCREEN_HEIGHT
    );

    if (!texture) {
        std::cerr << "[ERROR] Failed to create texture: " << SDL_GetError() << std::endl;
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "[INFO] Display initialized. Press ESC to quit." << std::endl;

    // Main loop
    bool running = true;
    std::vector<uint8_t> frame_buffer;
    Uint32 frame_count = 0;
    Uint32 fps_start = SDL_GetTicks();

    while (running) {
        // Handle SDL events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            }
        }

        // Receive frame size (8 bytes, network byte order)
        uint64_t frame_size_net;
        if (!RecvAll(client_socket, (char*)&frame_size_net, sizeof(frame_size_net))) {
            std::cerr << "[ERROR] Failed to receive frame size" << std::endl;
            break;
        }

        uint64_t frame_size = ntohll(frame_size_net);
        
        if (frame_size > 10 * 1024 * 1024) { // Sanity check: max 10MB
            std::cerr << "[ERROR] Invalid frame size: " << frame_size << std::endl;
            break;
        }

        // Receive frame data
        frame_buffer.resize(frame_size);
        if (!RecvAll(client_socket, (char*)frame_buffer.data(), frame_size)) {
            std::cerr << "[ERROR] Failed to receive frame data" << std::endl;
            break;
        }

        // Update texture (this assumes raw RGB data - modify if using JPEG)
        // For JPEG, you'd decode first using libjpeg-turbo
        SDL_UpdateTexture(texture, nullptr, frame_buffer.data(), SCREEN_WIDTH * 3);

        // Render
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        // FPS counter
        frame_count++;
        Uint32 now = SDL_GetTicks();
        if (now - fps_start >= 5000) {
            float fps = frame_count / ((now - fps_start) / 1000.0f);
            std::cout << "[INFO] FPS: " << fps << " | Frame size: " << frame_size / 1024 << " KB" << std::endl;
            frame_count = 0;
            fps_start = now;
        }
    }

    // Cleanup
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    closesocket(client_socket);
    WSACleanup();

    std::cout << "[INFO] Client closed" << std::endl;
    return 0;
}