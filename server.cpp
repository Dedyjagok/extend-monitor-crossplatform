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
const int PORT = 9999;
const int TARGET_WIDTH = 960;
const int TARGET_HEIGHT = 540;
const int JPEG_QUALITY = 30;
const int TARGET_MONITOR = 1; // 0 = primary, 1 = secondary

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

    bool Initialize(int monitor_index) {
        HRESULT hr;

        // Create D3D11 Device
        D3D_FEATURE_LEVEL feature_level;
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
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
            std::cerr << "[ERROR] Failed to create D3D11 device: 0x" << std::hex << hr << std::endl;
            return false;
        }

        // Get DXGI Device
        IDXGIDevice* dxgi_device = nullptr;
        hr = d3d_device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi_device);
        if (FAILED(hr)) {
            std::cerr << "[ERROR] Failed to get DXGI device" << std::endl;
            return false;
        }

        // Get DXGI Adapter
        IDXGIAdapter* dxgi_adapter = nullptr;
        hr = dxgi_device->GetAdapter(&dxgi_adapter);
        dxgi_device->Release();
        if (FAILED(hr)) {
            std::cerr << "[ERROR] Failed to get DXGI adapter" << std::endl;
            return false;
        }

        // Enumerate outputs (monitors)
        IDXGIOutput* dxgi_output = nullptr;
        hr = dxgi_adapter->EnumOutputs(monitor_index, &dxgi_output);
        dxgi_adapter->Release();
        if (FAILED(hr)) {
            std::cerr << "[ERROR] Failed to get monitor " << monitor_index 
                      << " (0x" << std::hex << hr << ")" << std::endl;
            return false;
        }

        // Get Output1 interface
        IDXGIOutput1* dxgi_output1 = nullptr;
        hr = dxgi_output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&dxgi_output1);
        dxgi_output->Release();
        if (FAILED(hr)) {
            std::cerr << "[ERROR] Failed to get IDXGIOutput1" << std::endl;
            return false;
        }

        // Create Desktop Duplication
        hr = dxgi_output1->DuplicateOutput(d3d_device, &duplication);
        dxgi_output1->Release();
        if (FAILED(hr)) {
            std::cerr << "[ERROR] Failed to create desktop duplication: 0x" << std::hex << hr << std::endl;
            std::cerr << "[INFO] Make sure no other capture tool is running" << std::endl;
            return false;
        }

        duplication->GetDesc(&dupl_desc);
        monitor_width = dupl_desc.ModeDesc.Width;
        monitor_height = dupl_desc.ModeDesc.Height;

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

int main() {
    std::cout << "[INFO] High-Performance Monitor Extender Server (C++)" << std::endl;
    std::cout << "[INFO] Target: Monitor " << TARGET_MONITOR << std::endl;

    // Initialize Winsock
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "[ERROR] WSAStartup failed" << std::endl;
        return 1;
    }

    // Initialize Desktop Duplication
    DesktopDuplicator duplicator;
    if (!duplicator.Initialize(TARGET_MONITOR)) {
        std::cerr << "[FATAL] Failed to initialize desktop capture" << std::endl;
        WSACleanup();
        return 1;
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

            // Resize
            ResizeBGRA(frame_bgra, duplicator.GetWidth(), duplicator.GetHeight(),
                      resized_bgra, TARGET_WIDTH, TARGET_HEIGHT);

            // Encode (placeholder - integrate libjpeg-turbo for real JPEG)
            EncodeJPEG(resized_bgra, TARGET_WIDTH, TARGET_HEIGHT, jpeg_data, JPEG_QUALITY);

            // Send size header (8 bytes, network byte order)
            uint64_t size = jpeg_data.size();
            uint64_t size_net = htonll(size);
            
            if (send(client_socket, (char*)&size_net, sizeof(size_net), 0) == SOCKET_ERROR) {
                std::cerr << "[INFO] Client disconnected (send size failed)" << std::endl;
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