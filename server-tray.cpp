// server-tray.cpp - System Tray version of the monitor extender server
// Compile: cl /std:c++17 /O2 /EHsc /DWINDOWS_TRAY /Fe:server-tray.exe server-tray.cpp d3d11.lib dxgi.lib ws2_32.lib shell32.lib user32.lib

#define _WIN32_WINNT 0x0601
#include <winsock2.h>
#include <ws2tcpip.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <vector>
#include <memory>
#include <chrono>
#include <cstring>
#include <thread>
#include <atomic>

// STB Image library for loading cursor image
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "stb_image.h"

// Link libraries
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

// Configuration
const char* PROTOCOL_VERSION = "v2.0-Magic";
const int PORT = 9999;
const int TARGET_WIDTH = 1280;
const int TARGET_HEIGHT = 720;
const int JPEG_QUALITY = 30;
const char* CURSOR_IMAGE_PATH = "assets/cursor-point.jpeg";

// Global cursor image data
struct CursorImage {
    uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
    int channels = 0;
} g_cursorImage;

// Tray Icon IDs
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_SHOW_CONSOLE 1002
#define ID_TRAY_HIDE_CONSOLE 1003

// Global variables for tray
HWND g_hwnd = NULL;
bool g_consoleAllocated = false;
NOTIFYICONDATAA g_nid = {0};
std::atomic<bool> g_running(true);
HWND g_consoleWindow = NULL;

// Forward declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void AddTrayIcon(HWND hwnd);
void RemoveTrayIcon();
void ShowContextMenu(HWND hwnd);
void AllocateConsole();
void DeallocateConsole();

// Byte order conversion
inline uint64_t htonll(uint64_t value) {
    static const int num = 42;
    if (*(char*)&num == 42) {
        uint32_t high_part = htonl((uint32_t)(value >> 32));
        uint32_t low_part = htonl((uint32_t)(value & 0xFFFFFFFFLL));
        return ((uint64_t)low_part << 32) | high_part;
    }
    return value;
}

// Include the DesktopDuplicator, EncodeJPEG, DrawCursor, and ResizeBGRA functions from server.cpp
// (Copying them here to keep this self-contained)

class DesktopDuplicator {
private:
    ID3D11Device* d3d_device = nullptr;
    ID3D11DeviceContext* d3d_context = nullptr;
    IDXGIOutputDuplication* duplication = nullptr;
    ID3D11Texture2D* staging_texture = nullptr;
    DXGI_OUTDUPL_DESC dupl_desc;
    int monitor_width = 0;
    int monitor_height = 0;
    int monitor_left = 0;
    int monitor_top = 0;

public:
    ~DesktopDuplicator() { Cleanup(); }

    bool Initialize(int target_monitor_index) {
        HRESULT hr;
        
        // Create DXGI Factory
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

        // Iterate Adapters
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 adapter_desc;
            adapter->GetDesc1(&adapter_desc);

            // Iterate Outputs
            for (UINT j = 0; adapter->EnumOutputs(j, &output) != DXGI_ERROR_NOT_FOUND; ++j) {
                if (global_monitor_count == target_monitor_index) {
                    DXGI_OUTPUT_DESC output_desc;
                    output->GetDesc(&output_desc);
                    std::wcout << L"[SUCCESS] Found Monitor " << target_monitor_index << L" on " << adapter_desc.Description << std::endl;
                    
                    // Create D3D11 Device
                    D3D_FEATURE_LEVEL feature_level;
                    hr = D3D11CreateDevice(
                        adapter,
                        D3D_DRIVER_TYPE_UNKNOWN,
                        nullptr, 0, nullptr, 0,
                        D3D11_SDK_VERSION,
                        &d3d_device, &feature_level, &d3d_context
                    );

                    if (FAILED(hr)) {
                        std::cerr << "[ERROR] Failed to create D3D11 Device" << std::endl;
                        return false;
                    }

                    // Duplicate Output
                    IDXGIOutput1* output1 = nullptr;
                    hr = output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);
                    if (SUCCEEDED(hr)) {
                        hr = output1->DuplicateOutput(d3d_device, &duplication);
                        output1->Release();
                        if (SUCCEEDED(hr)) {
                            duplication->GetDesc(&dupl_desc);
                            monitor_width = dupl_desc.ModeDesc.Width;
                            monitor_height = dupl_desc.ModeDesc.Height;
                            monitor_left = output_desc.DesktopCoordinates.left;
                            monitor_top = output_desc.DesktopCoordinates.top;
                            found = true;
                        }
                    }
                    break;
                }
                global_monitor_count++;
                output->Release();
                output = nullptr;
            }
            
            if (found) {
                output->Release();
                adapter->Release();
                break;
            }
            adapter->Release();
        }
        factory->Release();

        if (!found) return false;

        std::cout << "[SUCCESS] GPU Capture initialized: " << monitor_width << "x" << monitor_height << std::endl;

        // Create staging texture
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
        return SUCCEEDED(hr);
    }

    bool CaptureFrame(std::vector<uint8_t>& bgra_buffer) {
        if (!duplication) return false;

        IDXGIResource* desktop_resource = nullptr;
        DXGI_OUTDUPL_FRAME_INFO frame_info;
        HRESULT hr = duplication->AcquireNextFrame(10, &frame_info, &desktop_resource);
        
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;
        if (FAILED(hr)) return false;

        ID3D11Texture2D* acquired_texture = nullptr;
        hr = desktop_resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&acquired_texture);
        desktop_resource->Release();
        if (FAILED(hr)) { duplication->ReleaseFrame(); return false; }

        d3d_context->CopyResource(staging_texture, acquired_texture);
        acquired_texture->Release();
        duplication->ReleaseFrame();

        D3D11_MAPPED_SUBRESOURCE mapped_resource;
        hr = d3d_context->Map(staging_texture, 0, D3D11_MAP_READ, 0, &mapped_resource);
        if (FAILED(hr)) return false;

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
};

bool EncodeJPEG(const std::vector<uint8_t>& bgra_data, int width, int height, 
                std::vector<uint8_t>& jpeg_data, int quality) {
    jpeg_data.resize(width * height * 3);
    for (int i = 0; i < width * height; i++) {
        jpeg_data[i * 3 + 0] = bgra_data[i * 4 + 2]; // R
        jpeg_data[i * 3 + 1] = bgra_data[i * 4 + 1]; // G
        jpeg_data[i * 3 + 2] = bgra_data[i * 4 + 0]; // B
    }
    return true;
}

void DrawCursor(std::vector<uint8_t>& buffer, int width, int height, int offset_x, int offset_y) {
    CURSORINFO ci = {0};
    ci.cbSize = sizeof(ci);
    if (GetCursorInfo(&ci)) {
        if (ci.flags == CURSOR_SHOWING) {
            int cx = ci.ptScreenPos.x - offset_x;
            int cy = ci.ptScreenPos.y - offset_y;
            
            // If cursor image loaded, draw it
            if (g_cursorImage.data && g_cursorImage.width > 0 && g_cursorImage.height > 0) {
                // Draw cursor image centered on cursor position
                int half_w = g_cursorImage.width / 2;
                int half_h = g_cursorImage.height / 2;
                
                for (int y = 0; y < g_cursorImage.height; y++) {
                    for (int x = 0; x < g_cursorImage.width; x++) {
                        int screen_x = cx - half_w + x;
                        int screen_y = cy - half_h + y;
                        
                        if (screen_x >= 0 && screen_x < width && screen_y >= 0 && screen_y < height) {
                            int src_idx = (y * g_cursorImage.width + x) * g_cursorImage.channels;
                            int dst_idx = (screen_y * width + screen_x) * 4;
                            
                            // Copy RGB from cursor image (JPEG doesn't have alpha, so use full opacity)
                            if (g_cursorImage.channels >= 3) {
                                buffer[dst_idx + 2] = g_cursorImage.data[src_idx + 0]; // R
                                buffer [dst_idx + 1] = g_cursorImage.data[src_idx + 1]; // G
                                buffer[dst_idx + 0] = g_cursorImage.data[src_idx + 2]; // B
                                buffer[dst_idx + 3] = 255; // A
                            }
                        }
                    }
                }
            } else {
                // Fallback: Draw 10x10 red square if image not loaded
                int size = 10;
                for (int y = cy - size/2; y < cy + size/2; y++) {
                    for (int x = cx - size/2; x < cx + size/2; x++) {
                        if (x >= 0 && x < width && y >= 0 && y < height) {
                            int idx = (y * width + x) * 4;
                            buffer[idx + 0] = 0; buffer[idx + 1] = 0;
                            buffer[idx + 2] = 255; buffer[idx + 3] = 255;
                        }
                    }
                }
            }
        }
    }
}

void ResizeBGRA(const std::vector<uint8_t>& src, int src_w, int src_h,
                std::vector<uint8_t>& dst, int dst_w, int dst_h) {
    dst.resize(dst_w * dst_h * 4);
    float x_ratio = (float)(src_w - 1) / dst_w;
    float y_ratio = (float)(src_h - 1) / dst_h;
    
    for (int y = 0; y < dst_h; y++) {
        for (int x = 0; x < dst_w; x++) {
            int src_x = std::min((int)(x * x_ratio), src_w - 1);
            int src_y = std::min((int)(y * y_ratio), src_h - 1);
            int src_idx = (src_y * src_w + src_x) * 4;
            int dst_idx = (y * dst_w + x) * 4;
            dst[dst_idx + 0] = src[src_idx + 0];
            dst[dst_idx + 1] = src[src_idx + 1];
            dst[dst_idx + 2] = src[src_idx + 2];
            dst[dst_idx + 3] = src[src_idx + 3];
        }
    }
}

// Server thread
void ServerThread() {
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);

    // Load custom cursor image
    g_cursorImage.data = stbi_load(CURSOR_IMAGE_PATH, &g_cursorImage.width, &g_cursorImage.height, &g_cursorImage.channels, 3);
    if (g_cursorImage.data) {
        std::cout << "[SUCCESS] Cursor loaded: " << g_cursorImage.width << "x" << g_cursorImage.height << std::endl;
    }

    DesktopDuplicator duplicator;
    int selected_monitor = 1;
    if (!duplicator.Initialize(selected_monitor)) {
        selected_monitor = 0;
        if (!duplicator.Initialize(selected_monitor)) {
            std::cerr << "[FATAL] Failed to initialize capture" << std::endl;
            return;
        }
    }

    SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int flag = 1;
    setsockopt(listen_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));

    sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    bind(listen_socket, (sockaddr*)&server_addr, sizeof(server_addr));
    listen(listen_socket, 1);
    std::cout << "[INFO] Server ready on port " << PORT << std::endl;

    while (g_running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listen_socket, &readfds);
        timeval timeout = {1, 0};
        
        if (select(0, &readfds, NULL, NULL, &timeout) <= 0) continue;

        SOCKET client_socket = accept(listen_socket, nullptr, nullptr);
        if (client_socket == INVALID_SOCKET) continue;

        setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));
        std::cout << "[INFO] Client connected" << std::endl;

        std::vector<uint8_t> frame_bgra, resized_bgra, jpeg_data;
        
        while (g_running) {
            if (!duplicator.CaptureFrame(frame_bgra)) continue;
            DrawCursor(frame_bgra, duplicator.GetWidth(), duplicator.GetHeight(), 
                       duplicator.GetLeft(), duplicator.GetTop());
            ResizeBGRA(frame_bgra, duplicator.GetWidth(), duplicator.GetHeight(),
                      resized_bgra, TARGET_WIDTH, TARGET_HEIGHT);
            EncodeJPEG(resized_bgra, TARGET_WIDTH, TARGET_HEIGHT, jpeg_data, JPEG_QUALITY);

            uint32_t magic_net = htonl(0xDEADBEEF);
            uint64_t size = jpeg_data.size();
            uint64_t size_net = htonll(size);
            uint32_t width_net = htonl(TARGET_WIDTH);
            uint32_t height_net = htonl(TARGET_HEIGHT);
            
            char header[20];
            memcpy(header, &magic_net, 4);
            memcpy(header + 4, &size_net, 8);
            memcpy(header + 12, &width_net, 4);
            memcpy(header + 16, &height_net, 4);

            if (send(client_socket, header, 20, 0) == SOCKET_ERROR) break;

            int total_sent = 0;
            while (total_sent < (int)size) {
                int sent = send(client_socket, (char*)jpeg_data.data() + total_sent, 
                               size - total_sent, 0);
                if (sent == SOCKET_ERROR) goto client_disconnect;
                total_sent += sent;
            }
        }

client_disconnect:
        closesocket(client_socket);
    }

    closesocket(listen_socket);
    WSACleanup();
}

// Console allocation/deallocation
void AllocateConsole() {
    if (g_consoleAllocated && g_consoleWindow && IsWindow(g_consoleWindow)) {
        // Console already exists, just show it
        ShowWindow(g_consoleWindow, SW_SHOW);
        SetForegroundWindow(g_consoleWindow);
        return;
    }
    
    if (AllocConsole()) {
        FILE* fDummy;
        freopen_s(&fDummy, "CONOUT$", "w", stdout);
        freopen_s(&fDummy, "CONOUT$", "w", stderr);
        freopen_s(&fDummy, "CONIN$", "r", stdin);
        std::cout.clear();
        std::cerr.clear();
        std::cin.clear();
        
        g_consoleWindow = GetConsoleWindow();
        g_consoleAllocated = true;
        
        std::cout << "[INFO] Monitor Extender Server" << std::endl;
        std::cout << "[INFO] Protocol Version: " << PROTOCOL_VERSION << std::endl;
        std::cout << "[INFO] Server running in background. Right-click tray icon to manage." << std::endl;
    }
}

void DeallocateConsole() {
    if (g_consoleAllocated) {
        ::FreeConsole();
        g_consoleWindow = NULL;
        g_consoleAllocated = false;
    }
}

// Tray functions
void AddTrayIcon(HWND hwnd) {
    g_nid.cbSize = sizeof(NOTIFYICONDATAA);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    strcpy_s(g_nid.szTip, "Monitor Extender Server");
    Shell_NotifyIconA(NIM_ADD, &g_nid);
}

void RemoveTrayIcon() {
    Shell_NotifyIconA(NIM_DELETE, &g_nid);
}

void ShowContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    
    bool consoleVisible = IsWindowVisible(g_consoleWindow);
    AppendMenuA(hMenu, MF_STRING | (consoleVisible ? MF_CHECKED : 0), ID_TRAY_SHOW_CONSOLE, "Show Console");
    AppendMenuA(hMenu, MF_STRING | (!consoleVisible ? MF_CHECKED : 0), ID_TRAY_HIDE_CONSOLE, "Hide Console");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING, ID_TRAY_EXIT, "Exit");
    
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
                ShowContextMenu(hwnd);
            }
            break;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_TRAY_SHOW_CONSOLE:
                    AllocateConsole();
                    break;
                case ID_TRAY_HIDE_CONSOLE:
                    if (g_consoleAllocated && g_consoleWindow) {
                        DeallocateConsole();
                    }
                    break;
                case ID_TRAY_EXIT:
                    g_running = false;
                    PostQuitMessage(0);
                    break;
            }
            break;
        case WM_DESTROY:
            RemoveTrayIcon();
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProcA(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int main() {
    // NOTE: App starts as Windows GUI (no console)
    // Console can be allocated via tray menu: "Show Console"
    
    // Create hidden window for message handling
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "MonitorExtenderTray";
    RegisterClassExA(&wc);
    
    g_hwnd = CreateWindowExA(0, "MonitorExtenderTray", "Monitor Extender", 0, 
                            0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);
    
    AddTrayIcon(g_hwnd);
    
    // Start server thread
    std::thread serverThread(ServerThread);
    
    // Message loop
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    
    g_running = false;
    serverThread.join();
    RemoveTrayIcon();
    
    return 0;
}
