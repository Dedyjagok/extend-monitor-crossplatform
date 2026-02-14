import socket
import cv2
import struct
import numpy as np
import mss
import time
import win32gui
import win32api
import win32con

# Konfigurasi Server
HOST_IP = '0.0.0.0'  # Dengarkan semua koneksi masuk
PORT = 9999          # Port bebas

def draw_cursor(img, rel_x, rel_y):
    """
    Menggambar kursor panah sederhana di atas gambar.
    """
    # Warna Kursor (Putih dengan garis tepi Hitam)
    color_fill = (255, 255, 255)
    color_border = (0, 0, 0)
    
    # Bentuk Panah Kursor (Polygon sederhana)
    # Titik-titik koordinat relatif terhadap ujung panah (0,0)
    cursor_points = np.array([
        [0, 0],    # Ujung atas
        [0, 20],   # Bawah kiri
        [5, 15],   # Lekukan dalam
        [12, 22],  # Ekor 1
        [14, 20],  # Ekor 2
        [7, 12],   # Lekukan luar
        [15, 12]   # Kanan atas
    ], np.int32)

    # Geser titik ke posisi mouse sebenarnya
    cursor_points = cursor_points + np.array([rel_x, rel_y])

    # Gambar di layar (Fill dulu baru Border)
    try:
        cv2.fillPoly(img, [cursor_points], color_fill)
        cv2.polylines(img, [cursor_points], True, color_border, 1)
    except:
        pass # Abaikan jika mouse keluar batas array gambar

def start_server():
    # Inisialisasi Socket TCP
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    # Disable Nagle's algorithm for lower latency
    server_socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    
    try:
        server_socket.bind((HOST_IP, PORT))
    except OSError as e:
        # Check for specific Windows error 10048 (Address already in use)
        if hasattr(e, 'winerror') and e.winerror == 10048:
            print(f"[ERROR] Port {PORT} sudah digunakan!")
            print("Solusi: 1) Tutup program lain yang pakai port ini")
            print("        2) Ganti PORT di kode (misal: PORT = 9998)")
            print(f"        3) Jalankan: taskkill /F /PID [PID dari 'netstat -ano | findstr :{PORT}']")
        else:
            print(f"[ERROR] Bind failed: {e}")
        return
    
    server_socket.listen(5)
    print(f"[INFO] Server berjalan di port {PORT}. Menunggu Client...")

    client_socket, addr = server_socket.accept()
    client_socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1) # Also set on accepted socket
    print(f"[INFO] Terhubung dengan: {addr}")

    try:
        with mss.mss() as sct:
            # Debug: Print all monitors
            print(f"[DEBUG] Total monitors detected: {len(sct.monitors)}")
            for i, monitor in enumerate(sct.monitors):
                print(f"[DEBUG] Monitor {i}: {monitor}")
            
            # PENTING: sct.monitors[0] adalah semua layar digabung
            # sct.monitors[1] adalah layar utama (ASUS)
            # sct.monitors[2] adalah layar virtual (Extend) -> Target kita
            
            # Cek apakah monitor ke-2 ada
            if len(sct.monitors) < 3:
                print("[ERROR] Monitor Virtual tidak terdeteksi!")
                print("[ERROR] Pastikan VDD driver sudah terinstall dan monitor virtual aktif!")
                target_monitor = sct.monitors[1]  # Fallback
            else:
                target_monitor = sct.monitors[2]
                print(f"[INFO] Using virtual monitor: {target_monitor}")

            # Ambil koordinat offset monitor target (Penting untuk kalkulasi mouse)
            mon_top = target_monitor["top"]
            mon_left = target_monitor["left"]
            mon_width = target_monitor["width"]
            mon_height = target_monitor["height"]

            frame_count = 0
            while True:
                # 1. Tangkap Layar
                img = np.array(sct.grab(target_monitor))
                
                if img.size == 0:
                    print("[ERROR] Captured image is empty!")
                    time.sleep(0.01) # Short sleep to prevent CPU spin
                    continue

                # 2. Hapus channel Alpha (Transparansi) biar ringan (BGRA -> BGR)
                frame = cv2.cvtColor(img, cv2.COLOR_BGRA2BGR)

                # 3. Ambil Posisi Mouse Global di Windows
                # Menggunakan win32api agar akurat & cepat
                try:
                    flags, hcursor, (global_x, global_y) = win32gui.GetCursorInfo()
                    
                    # 4. Cek apakah Mouse berada di dalam area Monitor Virtual
                    if (mon_left <= global_x < mon_left + mon_width) and \
                       (mon_top <= global_y < mon_top + mon_height):
                        
                        # Hitung posisi relatif mouse terhadap monitor virtual
                        rel_x = global_x - mon_left
                        rel_y = global_y - mon_top
                        
                        # 5. Gambar Kursor Manual (Inject) ke dalam Video
                        draw_cursor(frame, rel_x, rel_y)
                except Exception as e:
                    # Ignore mouse errors to keep stream running
                    pass

                # 6. Kompresi JPEG severity - LOWERED QUALITY FOR SPEED
                # Quality 50-70 is usually a good sweet spot for latency vs quality
                result, encoded_frame = cv2.imencode('.jpg', frame, [int(cv2.IMWRITE_JPEG_QUALITY), 60])
                
                if not result:
                    print("[ERROR] Failed to encode frame!")
                    continue

                # 7. Serialisasi data - REMOVED PICKLE
                # Direct bytes is faster
                data = encoded_frame.tobytes()
                
                # Debug info occasionally
                frame_count += 1
                if frame_count % 60 == 0:
                     pass # Reduce spam
                
                # 8. Kirim ukuran data (Header) + Data (Payload)
                # "!Q" ensures consistent 8-byte size across all platforms (network byte order)
                message_size = struct.pack("!Q", len(data)) 
                
                try:
                    client_socket.sendall(message_size + data)
                except (BrokenPipeError, ConnectionResetError):
                    print("[ERROR] Client disconnected!")
                    break

    except KeyboardInterrupt:
        print("\n[INFO] Server stopped by user")
    except Exception as e:
        print(f"[ERROR] Error utama: {e}")
    finally:
        client_socket.close()
        server_socket.close()
        print("[INFO] Server closed")

if __name__ == '__main__':
    start_server()