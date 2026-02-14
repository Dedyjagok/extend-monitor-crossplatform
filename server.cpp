// server.cpp - High-performance screen capture server using DirectX Desktop Duplication
// Compile: g++ -std=c++17 server.cpp -o server -ld3d11 -ldxgi -lgdi32 -lws2_32 -O3
// Or with MSVC: cl /std:c++17 /O2 server.cpp d3d11.lib dxgi.lib gdi32.lib ws2_32.lib

#include <winsock2.h>
#include <ws2tcpip.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <iostream>
#include <vector>
#include <memory>
#include <chrono>
#include <cstring>

// Link libraries
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// Configuration
const char* PROTOCOL_VERSION = "v2.0-Magic";
const int PORT = 9999;
const int TARGET_WIDTH = 1280;
const int TARGET_HEIGHT = 720;
const int JPEG_QUALITY = 30;
const int TARGET_MONITOR = 0;

// Byte order conversion for 64-bit integers
inline uint64_t htonll(uint64_t value) {
    // Check if system is little-endian
    static const int num = 42;
    if (*(char*)&num == 42) {
        // Little-endian: need to swap bytes
        uint32_t high_part = htonl((uint32_t)(value >> 32));
        uint32_t low_part = htonl((uint32_t)(value & 0xFFFFFFFFLL));
        return ((uint64_t)low_part << 32) | high_part;
    }
    return value; // Big-endian: no swap needed
}

inline uint64_t ntohll(uint64_t value) {
    return htonll(value); // Same operation for conversion in both directions
} // 0 = primary, 1 = secondary

// DirectX Desktop Duplication Class
class DesktopDuplicator {
private:
    ID3D11Device* d3d_device = nullptr;
    ID3D11DeviceContext* d3d_context = nullptr;
    IDXGIOutputDuplication* duplication = nullptr;
    ID3D11Texture2D* staging_texture = nullptr;
    DXGI_OUTDUPL_DESC dupl_desc;
    int monitor_width = 0;
    int monitor_height = 0;

public:
    ~DesktopDuplicator() {
        Cleanup();
    }

    bool Initialize(int target_monitor_index) {
        HRESULT hr;
        
        // 1. Create DXGI Factory to enumerate adapters
        IDXGIFactory1* factory = nullptr;
        hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory);
        if (FAILED(hr)) {
            std::cerr << "[ERROR] Failed to create DXGI Factory" << std::endl;
            return false;
        }

        int global_monitor_count = 0;
        IDXGIAdapter1* adapter = nullptr;
        IDXGIOutput* output = nullptr;
        bool found = false;

        // 2. Iterate Adapters
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 adapter_desc;
            adapter->GetDesc1(&adapter_desc);
            // std::wcout << L"[INFO] Checking Adapter: " << adapter_desc.Description << std::endl;

            // 3. Iterate Outputs on this Adapter
            for (UINT j = 0; adapter->EnumOutputs(j, &output) != DXGI_ERROR_NOT_FOUND; ++j) {
                if (global_monitor_count == target_monitor_index) {
                    // FOUND IT!
                    DXGI_OUTPUT_DESC output_desc;
                    output->GetDesc(&output_desc);
                    std::wcout << L"[SUCCESS] Found Monitor " << target_monitor_index << L" on " << adapter_desc.Description << std::endl;
                    
                    // 4. Create D3D11 Device for THIS Adapter
                    D3D_FEATURE_LEVEL feature_level;
                    hr = D3D11CreateDevice(
                        adapter, // IMPORTANT: Use specific adapter
                        D3D_DRIVER_TYPE_UNKNOWN, // Must be UNKNOWN when adapter is specified
                        nullptr,
                        0,
                        nullptr,
                        0,
                        D3D11_SDK_VERSION,
                        &d3d_device,
                        &feature_level,
                        &d3d_context
                    );

                    if (FAILED(hr)) {
                        std::cerr << "[ERROR] Failed to create D3D11 Device for adapter: 0x" << std::hex << hr << std::endl;
                        return false;
                    }

                    // 5. Duplicate Output
                    IDXGIOutput1* output1 = nullptr;
                    hr = output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);
                    if (SUCCEEDED(hr)) {
                        hr = output1->DuplicateOutput(d3d_device, &duplication);
                        output1->Release();
                        if (SUCCEEDED(hr)) {
                            duplication->GetDesc(&dupl_desc);
                            monitor_width = dupl_desc.ModeDesc.Width;
                            monitor_height = dupl_desc.ModeDesc.Height;
                            
                            // Capture Top-Left coordinates
                            monitor_left = output_desc.DesktopCoordinates.left;
                            monitor_top = output_desc.DesktopCoordinates.top;
                            
                            found = true;
                        } else {
                             std::cerr << "[ERROR] DuplicateOutput failed: 0x" << std::hex << hr << std::endl;
                             // E_ACCESSDENIED often means another app is capturing
                        }
                    }
                    
                    break; // Break output loop
                }
                global_monitor_count++;
                output->Release();
                output = nullptr;
            }
            
            if (found) {
                output->Release(); // Release the successful output
                adapter->Release(); 
                break; // Break adapter loop
            }
            adapter->Release();
            adapter = nullptr;
        }
        factory->Release();

        if (!found) {
             std::cerr << "[ERROR] Monitor Index " << target_monitor_index << " not found!" << std::endl;
             std::cerr << "[INFO] Total monitors found: " << global_monitor_count << std::endl;
             return false;
        }
        
        std::cout << "[SUCCESS] GPU Capture initialized: " << monitor_width << "x" << monitor_height << std::endl;

        // Create staging texture for CPU readback
        D3D11_TEXTURE2D_DESC staging_desc = {};
        staging_desc.Width = monitor_width;
        staging_desc.Height = monitor_height;
        staging_desc.MipLevels = 1;
        staging_desc.ArraySize = 1;
        staging_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        staging_desc.SampleDesc.Count = 1;
        staging_desc.Usage = D3D11_USAGE_STAGING;
        staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        hr = d3d_device->CreateTexture2D(&staging_desc, nullptr, &staging_texture);
        if (FAILED(hr)) {
            std::cerr << "[ERROR] Failed to create staging texture" << std::endl;
            return false;
        }

        return true;
    }

    bool CaptureFrame(std::vector<uint8_t>& bgra_buffer) {
        if (!duplication) return false;

        IDXGIResource* desktop_resource = nullptr;
        DXGI_OUTDUPL_FRAME_INFO frame_info;

        // Try to acquire next frame (10ms timeout)
        HRESULT hr = duplication->AcquireNextFrame(10, &frame_info, &desktop_resource);
        
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            return false; // No new frame
        }
        
        if (FAILED(hr)) {
            if (hr == DXGI_ERROR_ACCESS_LOST) {
                std::cerr << "[WARN] Desktop duplication access lost, reinitializing..." << std::endl;
                Cleanup();
                Initialize(TARGET_MONITOR);
            }
            return false;
        }

        // Get texture from resource
        ID3D11Texture2D* acquired_texture = nullptr;
        hr = desktop_resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&acquired_texture);
        desktop_resource->Release();
        
        if (FAILED(hr)) {
            duplication->ReleaseFrame();
            return false;
        }

        // Copy to staging texture (GPU -> CPU accessible)
        d3d_context->CopyResource(staging_texture, acquired_texture);
        acquired_texture->Release();

        // Release frame immediately (important!)
        duplication->ReleaseFrame();

        // Map staging texture to read pixels
        D3D11_MAPPED_SUBRESOURCE mapped_resource;
        hr = d3d_context->Map(staging_texture, 0, D3D11_MAP_READ, 0, &mapped_resource);
        if (FAILED(hr)) {
            return false;
        }

        // Copy pixels to buffer
        bgra_buffer.resize(monitor_width * monitor_height * 4);
        uint8_t* src = (uint8_t*)mapped_resource.pData;
        uint8_t* dst = bgra_buffer.data();

        for (int y = 0; y < monitor_height; y++) {
            memcpy(dst + y * monitor_width * 4, src + y * mapped_resource.RowPitch, monitor_width * 4);
        }

        d3d_context->Unmap(staging_texture, 0);
        return true;
    }

    void Cleanup() {
        if (staging_texture) staging_texture->Release();
        if (duplication) duplication->Release();
        if (d3d_context) d3d_context->Release();
        if (d3d_device) d3d_device->Release();
    }

    int GetWidth() const { return monitor_width; }
    int GetHeight() const { return monitor_height; }
    int GetLeft() const { return monitor_left; }
    int GetTop() const { return monitor_top; }

private:
    int monitor_left = 0;
    int monitor_top = 0;
};

// Simple JPEG encoder stub (you'll need a real library like libjpeg-turbo)
// For now, this will send raw RGB data - integrate libjpeg-turbo for production
bool EncodeJPEG(const std::vector<uint8_t>& bgra_data, int width, int height, 
                std::vector<uint8_t>& jpeg_data, int quality) {
    // PLACEHOLDER: This is where you'd use libjpeg-turbo
    // For demonstration, we'll send raw RGB (convert BGRA to RGB)
    jpeg_data.resize(width * height * 3);
    
    for (int i = 0; i < width * height; i++) {
        jpeg_data[i * 3 + 0] = bgra_data[i * 4 + 2]; // R
        jpeg_data[i * 3 + 1] = bgra_data[i * 4 + 1]; // G
        jpeg_data[i * 3 + 2] = bgra_data[i * 4 + 0]; // B
    }
    
    return true;
}

// Draw a simple cursor (Red Square 10x10)
void DrawCursor(std::vector<uint8_t>& buffer, int width, int height, int offset_x, int offset_y) {
    CURSORINFO ci = { 0 };
    ci.cbSize = sizeof(ci);
    if (GetCursorInfo(&ci)) {
        if (ci.flags == CURSOR_SHOWING) {
            int cx = ci.ptScreenPos.x - offset_x;
            int cy = ci.ptScreenPos.y - offset_y;
            
            // Draw 10x10 red square
            int size = 10;
            for (int y = cy - size/2; y < cy + size/2; y++) {
                for (int x = cx - size/2; x < cx + size/2; x++) {
                    if (x >= 0 && x < width && y >= 0 && y < height) {
                        int idx = (y * width + x) * 4;
                        buffer[idx + 0] = 0;   // B
                        buffer[idx + 1] = 0;   // G
                        buffer[idx + 2] = 255; // R
                        buffer[idx + 3] = 255; // A
                    }
                }
            }
        }
    }
}

// Simple bilinear downscale
void ResizeBGRA(const std::vector<uint8_t>& src, int src_w, int src_h,
                std::vector<uint8_t>& dst, int dst_w, int dst_h) {
    dst.resize(dst_w * dst_h * 4);
    
    float x_ratio = (float)src_w / dst_w;
    float y_ratio = (float)src_h / dst_h;
    
    for (int y = 0; y < dst_h; y++) {
        for (int x = 0; x < dst_w; x++) {
            int src_x = (int)(x * x_ratio);
            int src_y = (int)(y * y_ratio);
            
            int src_idx = (src_y * src_w + src_x) * 4;
            int dst_idx = (y * dst_w + x) * 4;
            
            dst[dst_idx + 0] = src[src_idx + 0]; // B
            dst[dst_idx + 1] = src[src_idx + 1]; // G
            dst[dst_idx + 2] = src[src_idx + 2]; // R
            dst[dst_idx + 3] = src[src_idx + 3]; // A
        }
    }
}

int main(int argc, char* argv[]) {
    std::cout << "[INFO] High-Performance Monitor Extender Server (C++)" << std::endl;
    std::cout << "[INFO] Protocol Version: " << PROTOCOL_VERSION << std::endl;
    
    int selected_monitor = 1; // Default to 1 (Extended)
    if (argc > 1) {
        selected_monitor = std::atoi(argv[1]);
        std::cout << "[INFO] Target Monitor Override: " << selected_monitor << std::endl;
    } else {
        std::cout << "[INFO] Default Target: Monitor 1 (Extended)" << std::endl;
    }

    // Initialize Winsock
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "[ERROR] WSAStartup failed" << std::endl;
        return 1;
    }

    // Initialize Desktop Duplication
    // Initialize Desktop Duplication
    DesktopDuplicator duplicator;
    
    if (duplicator.Initialize(selected_monitor)) {
        std::cout << "[SUCCESS] Initialized Capture on Monitor " << selected_monitor << " (Extended)" << std::endl;
    } else {
        std::cerr << "[WARN] Failed to initialize Monitor " << selected_monitor << ". Falling back to 0 (Main)..." << std::endl;
        selected_monitor = 0;
        if (duplicator.Initialize(selected_monitor)) {
            std::cout << "[SUCCESS] Initialized Capture on Monitor " << selected_monitor << " (Main)" << std::endl;
        } else {
            std::cerr << "[FATAL] Failed to initialize desktop capture on ANY monitor" << std::endl;
            WSACleanup();
            return 1;
        }
    }

    // Create TCP socket
    SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == INVALID_SOCKET) {
        std::cerr << "[ERROR] Failed to create socket" << std::endl;
        WSACleanup();
        return 1;
    }

    // Enable TCP_NODELAY (low latency)
    int flag = 1;
    setsockopt(listen_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));

    // Bind
    sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(listen_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "[ERROR] Bind failed: " << WSAGetLastError() << std::endl;
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    if (listen(listen_socket, 1) == SOCKET_ERROR) {
        std::cerr << "[ERROR] Listen failed" << std::endl;
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "[INFO] Server listening on port " << PORT << std::endl;
    std::cout << "[INFO] Output resolution: " << TARGET_WIDTH << "x" << TARGET_HEIGHT << std::endl;

    // Accept loop
    while (true) {
        std::cout << "[INFO] Waiting for client..." << std::endl;
        
        SOCKET client_socket = accept(listen_socket, nullptr, nullptr);
        if (client_socket == INVALID_SOCKET) {
            std::cerr << "[ERROR] Accept failed" << std::endl;
            continue;
        }

        setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));
        std::cout << "[INFO] Client connected!" << std::endl;

        // Streaming loop
        std::vector<uint8_t> frame_bgra;
        std::vector<uint8_t> resized_bgra;
        std::vector<uint8_t> jpeg_data;
        
        auto fps_start = std::chrono::steady_clock::now();
        int frame_count = 0;

        while (true) {
            // Capture frame
            if (!duplicator.CaptureFrame(frame_bgra)) {
                continue; // No new frame, try again
            }

            // Draw Cursor
            DrawCursor(frame_bgra, duplicator.GetWidth(), duplicator.GetHeight(), 
                       duplicator.GetLeft(), duplicator.GetTop());

            // Resize
            ResizeBGRA(frame_bgra, duplicator.GetWidth(), duplicator.GetHeight(),
                      resized_bgra, TARGET_WIDTH, TARGET_HEIGHT);

            // Encode (placeholder - integrate libjpeg-turbo for real JPEG)
            EncodeJPEG(resized_bgra, TARGET_WIDTH, TARGET_HEIGHT, jpeg_data, JPEG_QUALITY);

            // Send Header: [MAGIC (4B)] [Size (8B)] [Width (4B)] [Height (4B)]
            uint32_t magic_net = htonl(0xDEADBEEF);
            uint64_t size = jpeg_data.size();
            uint64_t size_net = htonll(size);
            uint32_t width_net = htonl(TARGET_WIDTH);
            uint32_t height_net = htonl(TARGET_HEIGHT);
            
            // buffer for header (20 bytes)
            char header[20];
            memcpy(header, &magic_net, 4);
            memcpy(header + 4, &size_net, 8);
            memcpy(header + 12, &width_net, 4);
            memcpy(header + 16, &height_net, 4);

            if (send(client_socket, header, 20, 0) == SOCKET_ERROR) {
                std::cerr << "[INFO] Client disconnected (send header failed)" << std::endl;
                break;
            }

            // Send frame data
            int total_sent = 0;
            while (total_sent < (int)size) {
                int sent = send(client_socket, (char*)jpeg_data.data() + total_sent, 
                               size - total_sent, 0);
                if (sent == SOCKET_ERROR) {
                    std::cerr << "[INFO] Client disconnected (send data failed)" << std::endl;
                    goto client_disconnect;
                }
                total_sent += sent;
            }

            // FPS counter
            frame_count++;
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - fps_start).count();
            if (elapsed >= 5) {
                float fps = frame_count / (float)elapsed;
                std::cout << "[INFO] FPS: " << fps << " | Size: " << size / 1024 << " KB" << std::endl;
                frame_count = 0;
                fps_start = now;
            }
        }

client_disconnect:
        closesocket(client_socket);
    }

    closesocket(listen_socket);
    WSACleanup();
    return 0;
}