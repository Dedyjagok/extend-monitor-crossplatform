// client.cpp - Cross-platform monitor extender client with SDL2
// Windows: cl /std:c++17 /O2 /EHsc client.cpp SDL2.lib SDL2main.lib ws2_32.lib
// Linux: g++ -std=c++17 -O3 client.cpp -o client $(pkg-config --cflags --libs sdl2) -lpthread

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "SDL2.lib")
    #pragma comment(lib, "SDL2main.lib")
    typedef int socklen_t;
    #define close closesocket
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <netinet/tcp.h>
    #include <unistd.h>
    #include <errno.h>
    #include <cstring>
    typedef int SOCKET;
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

#ifdef _MSC_VER
    #include <SDL.h>
#else
    #include <SDL2/SDL.h>
#endif

#include <iostream>
#include <vector>

const char* PROTOCOL_VERSION = "v2.0-Magic";
const int PORT = 9999;
const int SCREEN_WIDTH = 1366;
const int SCREEN_HEIGHT = 768;

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
            return false;
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
    std::cout << "[INFO] Protocol Version: " << PROTOCOL_VERSION << std::endl;
    std::cout << "[INFO] Server: " << server_ip << ":" << PORT << std::endl;

#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "[ERROR] WSAStartup failed" << std::endl;
        return 1;
    }
#endif

    // ── SDL init once ──────────────────────────────────────────────────────
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "[ERROR] SDL_Init failed: " << SDL_GetError() << std::endl;
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Monitor Extender",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        SCREEN_WIDTH, SCREEN_HEIGHT,
        SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP
    );
    if (!window) {
        std::cerr << "[ERROR] Failed to create window: " << SDL_GetError() << std::endl;
        SDL_Quit();
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "[ERROR] Failed to create renderer: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    SDL_ShowCursor(SDL_ENABLE);

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!texture) {
        std::cerr << "[ERROR] Failed to create texture: " << SDL_GetError() << std::endl;
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::cout << "[INFO] Window ready. Press ESC to quit." << std::endl;

    // ── Reconnect loop ─────────────────────────────────────────────────────
    bool app_running = true;
    std::vector<uint8_t> frame_buffer;

    while (app_running) {
        // Show black screen while connecting
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);

        // Check for quit events while reconnecting
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) { app_running = false; break; }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                app_running = false; break;
            }
        }
        if (!app_running) break;

        // Create socket and connect
        SOCKET client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (client_socket == INVALID_SOCKET) {
            std::cerr << "[ERROR] Failed to create socket, retrying..." << std::endl;
            SDL_Delay(2000);
            continue;
        }

        int flag = 1;
        setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));

        struct sockaddr_in server_addr = {};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(PORT);
        inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

        if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
            std::cout << "[INFO] Waiting for server..." << std::endl;
            close(client_socket);
            SDL_Delay(2000);
            continue;
        }

        std::cout << "[INFO] Connected!" << std::endl;

        // ── Streaming loop (runs until server disconnects) ─────────────────
        bool connected = true;
        Uint32 frame_count = 0;
        Uint32 fps_start = SDL_GetTicks();
        bool first_frame = true;

        while (connected && app_running) {
            // SDL events
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) { app_running = false; connected = false; }
                if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                    app_running = false; connected = false;
                }
            }
            if (!connected) break;

            // Receive header [MAGIC(4)][SIZE(8)][W(4)][H(4)]
            char header[20];
            if (!RecvAll(client_socket, header, 20)) {
                std::cout << "[INFO] Server disconnected. Reconnecting in 2s..." << std::endl;
                connected = false; break;
            }

            // Verify magic
            uint32_t magic_net;
            memcpy(&magic_net, header, 4);
            if (ntohl(magic_net) != 0xDEADBEEF) {
                std::cerr << "[FATAL] Protocol mismatch — recompile server and client!" << std::endl;
                app_running = false; break;
            }

            // Parse size
            uint64_t frame_size_net;
            memcpy(&frame_size_net, header + 4, 8);
            uint64_t frame_size = ntohll(frame_size_net);

            // Parse resolution
            uint32_t width_net, height_net;
            memcpy(&width_net, header + 12, 4);
            memcpy(&height_net, header + 16, 4);
            int frame_width = ntohl(width_net);
            int frame_height = ntohl(height_net);

            if (first_frame) {
                std::cout << "[INFO] Stream: " << frame_width << "x" << frame_height
                          << " (" << frame_size << " bytes/frame)" << std::endl;
                first_frame = false;
            }

            // Resize texture if resolution changed
            int tex_w, tex_h;
            SDL_QueryTexture(texture, nullptr, nullptr, &tex_w, &tex_h);
            if (tex_w != frame_width || tex_h != frame_height) {
                SDL_DestroyTexture(texture);
                texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,
                    SDL_TEXTUREACCESS_STREAMING, frame_width, frame_height);
            }

            // Receive frame data
            frame_buffer.resize(frame_size);
            if (!RecvAll(client_socket, (char*)frame_buffer.data(), frame_size)) {
                std::cout << "[INFO] Server disconnected. Reconnecting in 2s..." << std::endl;
                connected = false; break;
            }

            // Update texture row-by-row (respects SDL internal pitch to avoid scramble)
            void* pixels; int pitch;
            if (SDL_LockTexture(texture, nullptr, &pixels, &pitch) == 0) {
                uint8_t* dst = (uint8_t*)pixels;
                uint8_t* src = frame_buffer.data();
                int row_bytes = frame_width * 3;
                for (int y = 0; y < frame_height; y++)
                    memcpy(dst + y * pitch, src + y * row_bytes, row_bytes);
                SDL_UnlockTexture(texture);
            }

            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, nullptr, nullptr);
            SDL_RenderPresent(renderer);

            // FPS counter
            frame_count++;
            Uint32 now = SDL_GetTicks();
            if (now - fps_start >= 5000) {
                float fps = frame_count / ((now - fps_start) / 1000.0f);
                std::cout << "[INFO] FPS: " << fps
                          << " | " << frame_size / 1024 << " KB/frame" << std::endl;
                frame_count = 0;
                fps_start = now;
            }
        }

        close(client_socket);

        // Brief pause before reconnect attempt, but keep polling SDL events
        if (app_running) {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            SDL_RenderPresent(renderer);
            SDL_Delay(2000);
        }
    }

    // ── Cleanup ────────────────────────────────────────────────────────────
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
#ifdef _WIN32
    WSACleanup();
#endif
    std::cout << "[INFO] Client closed." << std::endl;
    return 0;
}