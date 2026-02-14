import socket
import cv2
import struct
import numpy as np
import dxcam
import threading
import time
import win32gui

# Konfigurasi
HOST_IP = '0.0.0.0'
PORT = 9999
WIDTH, HEIGHT = 1280, 720 # Target Resolution (Resize agar ringan di network)

# Inisialisasi UDP Socket (Lebih cepat dari TCP)
server_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
# Perbesar buffer socket agar tidak packet loss
try:
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 65535)
except:
    print("[WARNING] Could not set SO_SNDBUF")

# Inisialisasi DXCam (GPU Capture - Super Cepat)
# output_idx=0 -> Monitor Utama
# output_idx=1 -> Monitor Kedua (Virtual/Extend)
# Ganti ke 0 jika ingin test capture layar utama dulu
TARGET_MONITOR_IDX = 1 

try:
    camera = dxcam.create(output_idx=TARGET_MONITOR_IDX, output_color="BGR")
    print(f"[INFO] DXCam initialized on monitor index {TARGET_MONITOR_IDX}")
except Exception as e:
    print(f"[ERROR] Gagal init DXCam di monitor {TARGET_MONITOR_IDX}. Fallback ke monitor 0.")
    try:
        camera = dxcam.create(output_idx=0, output_color="BGR")
    except Exception as e2:
        print(f"[FETAL] DXCam gagal total: {e2}")
        exit()

client_addr = None
running = True

def draw_cursor(frame, offset_x=0, offset_y=0):
    try:
        flags, hcursor, (x, y) = win32gui.GetCursorInfo()
        
        # Adjust coordinate relative to the extended monitor if needed
        # dxcam returns the frame of the specific monitor, but GetCursorInfo is global.
        # Check if cursor is roughly within the capture area (logic simplified for speed)
        
        # Simple Logic: Draw cursor at global (x,y) minus monitor offset
        # Note: Untuk implementasi sempurna, kita butuh tahu offset monitor virtual.
        # Di sini kita asumsi cursor *sudah* di area monitor yang benar jika user melihatnya.
        # Jika offset_x/y diperlukan (misal monitor 2 ada di kanan monitor 1), 
        # kita harus pass offset tersebut. 
        # Untuk sekarang kita gambar saja di posisi relatif (x, y) yang dikirim dari win32gui
        # (Perlu dikurangi offset monitor jika tidak 0,0)
        
        # NOTE: Tanpa 'enum_display_monitors', kita sulit tahu offset pasti secara otomatis via dxcam saja.
        # User mungkin perlu manual adjust atau kita pakai logic sederhana dulu.
        
        # Gambar lingkaran merah sebagai kursor (lebih cepat dari poligon)
        # cv2.circle(frame, (x, y), 5, (0, 0, 255), -1)
        
        # Jika ingin bentuk panah:
        cursor_points = np.array([[0, 0], [0, 20], [5, 15], [12, 22], [14, 20], [7, 12], [15, 12]], np.int32)
        
        # WARNING: offset_x dan offset_y harus diisi manual atau didapat dari win32api.GetMonitorInfo
        # Misal Monitor 1 width 1920. Maka Monitor 2 mulai di x=1920.
        # Kita perlu kurangi x dengan 1920 agar kursor muncul di frame monitor 2.
        
        # Placeholder logic: Gambar saja di posisi relatif frame
        # (Akan geser jika monitor virtual ada offset)
        pass 

    except:
        pass
    return frame

def wait_for_handshake():
    """Menunggu client mengirim 'HELLO' via UDP agar server tahu IP client"""
    global client_addr
    print(f"[INFO] Menunggu ping dari Client di port {PORT}...")
    while client_addr is None and running:
        try:
            data, addr = server_socket.recvfrom(1024)
            if data.startswith(b'HELLO'):
                client_addr = addr
                print(f"[INFO] Client ditemukan: {addr}")
        except:
            pass
        time.sleep(0.1)

def start_stream():
    global client_addr
    print("[INFO] Memulai streaming (Tekan Ctrl+C untuk Stop)...")
    
    # Mulai capture (Targetkan 60 FPS)
    camera.start(target_fps=60, video_mode=True)

    # Dapatkan info monitor untuk offset kursor (Manual dulu jika API kompleks)
    # Asumsi umum: Monitor 2 ada di sebelah kanan Monitor 1 (width=1920)
    # Jika cursor tidak pas, user bisa sesuaikan OFFSET_X ini.
    # OFFSET_X = 1920 
    # OFFSET_Y = 0

    frame_count = 0
    while running:
        if client_addr is None:
            time.sleep(0.5)
            continue

        # 1. Ambil Frame dari GPU (Non-blocking)
        frame = camera.get_latest_frame()
        if frame is None:
            continue

        # 2. Resize (Sangat penting untuk mengurangi lag jaringan)
        # Frame asli mungkin 1920x1080 atau 1366x768. Resize ke 1280x720 atau lebih kecil.
        frame = cv2.resize(frame, (WIDTH, HEIGHT))

        # 3. Gambar Kursor (Opsional - logic offset perlu disempurnakan)
        # frame = draw_cursor(frame)
        
        # Gambar kursor simple (Global position) using win32gui
        try:
             flags, hcursor, (gx, gy) = win32gui.GetCursorInfo()
             # Manual Offset Hack: Jika monitor 2 ada di kanan (x > 1920)
             # Kita perlu tahu offset monitor yang sedang dicapture dxcam.
             # Dxcam tidak memberitahu offset global secara langsung di object camera, 
             # tapi kita bisa hitung relatif.
             # Untuk "Barrier" cursor visualizer yang SANGAT PRESISI, perlu logic monitor detection lagi.
             # Untuk MVP, kita skip gambar kursor via OpenCV server-side jika DXCam menangkap layar penuh,
             # karena kursor Windows asli biasanya SUDAH TER-RENDER oleh DXGI jika hardware cursor enabled?
             # TAPI DXGI seringnya tidak capture hardware cursor.
             
             # Kita gambar titik simple untuk indikator
             # Asumsi monitor extend ada di sebelah kanan monitor primary FHD (1920 width)
             rel_x = gx - 1920 
             rel_y = gy
             
             if 0 <= rel_x < WIDTH and 0 <= rel_y < HEIGHT:
                 cv2.circle(frame, (rel_x, rel_y), 5, (0, 0, 255), -1)
        except:
            pass

        # 4. Encode ke JPEG code 'turbo' check
        # Quality 50-70 cukup untuk video gerak
        _, encoded = cv2.imencode('.jpg', frame, [int(cv2.IMWRITE_JPEG_QUALITY), 60])
        
        # 5. Pecah paket UDP (Max 60k bytes per paket agar aman)
        data = encoded.tobytes()
        
        # Kirim via UDP (Fire and Forget)
        # Packet fragmentation manual diperlukan jika > 60k
        # Tapi untuk latency rendah, lebih baik drop frame besar daripada fragmentasi (bikin lag/artifact)
        try:
            if len(data) < 62000:
                # Tambahkan header simple (L = ulong size = 4 bytes)
                # Client harus baca 4 bytes dulu
                server_socket.sendto(struct.pack("L", len(data)) + data, client_addr)
            else:
                # Frame terlalu besar untuk satu paket UDP. 
                # Opsional: Implementasi fragmentasi atau abaikan.
                # Kita abaikan demi speed (Client akan skip frame ini)
                # print("[WARN] Frame dropped (Too big for UDP)")
                pass
        except Exception as e:
            pass # UDP Packet loss expected

    camera.stop()
    server_socket.close()

if __name__ == "__main__":
    try:
        # Jalankan handshake di thread terpisah
        t = threading.Thread(target=wait_for_handshake, daemon=True)
        t.start()
        
        start_stream()
    except KeyboardInterrupt:
        running = False
        print("[INFO] Stopping...")