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
const int SCREEN_WIDTH = 1280;
const int SCREEN_HEIGHT = 720;

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
    std::cout << "[INFO] Connecting to " << server_ip << ":" << PORT << std::endl;

#ifdef _WIN32
    // Initialize Winsock (Windows only)
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "[ERROR] WSAStartup failed" << std::endl;
        return 1;
    }
#endif

    // Create socket
    SOCKET client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client_socket == INVALID_SOCKET) {
        std::cerr << "[ERROR] Failed to create socket" << std::endl;
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    // Enable TCP_NODELAY
    int flag = 1;
    setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));

    // Connect
    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
#ifdef _WIN32
        std::cerr << "[ERROR] Connection failed: " << WSAGetLastError() << std::endl;
#else
        std::cerr << "[ERROR] Connection failed: " << strerror(errno) << std::endl;
#endif
        close(client_socket);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::cout << "[INFO] Connected to server!" << std::endl;

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "[ERROR] SDL_Init failed: " << SDL_GetError() << std::endl;
        close(client_socket);
#ifdef _WIN32
        WSACleanup();
#endif
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
        close(client_socket);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    // Create renderer (hardware accelerated)
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "[ERROR] Failed to create renderer: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        close(client_socket);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    // Create texture for streaming (BGR format to match server)
    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_BGR24,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH,
        SCREEN_HEIGHT
    );

    if (!texture) {
        std::cerr << "[ERROR] Failed to create texture: " << SDL_GetError() << std::endl;
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        close(client_socket);
#ifdef _WIN32
        WSACleanup();
#endif
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

        // Receive Header: [MAGIC (4)] [Size (8)] [Width (4)] [Height (4)]
        char header[20];
        if (!RecvAll(client_socket, header, 20)) {
            std::cerr << "[ERROR] Failed to receive header" << std::endl;
            break;
        }

        // 1. Verify Magic
        uint32_t magic_net;
        memcpy(&magic_net, header, 4);
        if (ntohl(magic_net) != 0xDEADBEEF) {
            std::cerr << "[FATAL] PROTOCOL MISMATCH!" << std::endl;
            std::cerr << "Server is sending invalid data. Please recompiling SERVER and CLIENT." << std::endl;
            break;
        }

        // 2. Parse Size
        uint64_t frame_size_net;
        memcpy(&frame_size_net, header + 4, 8);
        uint64_t frame_size = ntohll(frame_size_net);

        // 3. Parse Resolution
        uint32_t width_net, height_net;
        memcpy(&width_net, header + 12, 4);
        memcpy(&height_net, header + 16, 4);
        int frame_width = ntohl(width_net);
        int frame_height = ntohl(height_net);
        
        // Sanity check
        if (frame_size > 50 * 1024 * 1024) { // Max 50MB
            std::cerr << "[ERROR] Invalid frame size: " << frame_size << std::endl;
            break;
        }

        // Check if texture needs update
        int tex_w, tex_h;
        SDL_QueryTexture(texture, nullptr, nullptr, &tex_w, &tex_h);
        if (tex_w != frame_width || tex_h != frame_height) {
            std::cout << "[INFO] Resizing Texture to: " << frame_width << "x" << frame_height << std::endl;
            SDL_DestroyTexture(texture);
            texture = SDL_CreateTexture(
                renderer,
                SDL_PIXELFORMAT_BGR24,
                SDL_TEXTUREACCESS_STREAMING,
                frame_width,
                frame_height
            );
            
            // Allow window to be resized if not fullscreen?
            // For now, keep fullscreen but scale aspect ratio
        }

        // Receive frame data
        frame_buffer.resize(frame_size);
        if (!RecvAll(client_socket, (char*)frame_buffer.data(), frame_size)) {
            std::cerr << "[ERROR] Failed to receive frame data" << std::endl;
            break;
        }

        // Update texture (this assumes raw RGB data - modify if using JPEG)
        SDL_UpdateTexture(texture, nullptr, frame_buffer.data(), frame_width * 3);

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
    close(client_socket);
#ifdef _WIN32
    WSACleanup();
#endif

    std::cout << "[INFO] Client closed" << std::endl;
    return 0;
}