import socket
import cv2
import pickle
import struct
import numpy as np
import mss
import time

# Konfigurasi Server
HOST_IP = '0.0.0.0'  # Dengarkan semua koneksi masuk
PORT = 9999          # Port bebas

def start_server():
    # Inisialisasi Socket TCP
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    try:
        server_socket.bind((HOST_IP, PORT))
    except OSError as e:
        if e.winerror == 10048:
            print(f"[ERROR] Port {PORT} sudah digunakan!")
            print("Solusi: 1) Tutup program lain yang pakai port ini")
            print("        2) Ganti PORT di kode (misal: PORT = 9998)")
            print(f"        3) Jalankan: taskkill /F /PID [PID dari 'netstat -ano | findstr :{PORT}']")
        return
    
    server_socket.listen(5)
    print(f"[INFO] Server berjalan di port {PORT}. Menunggu Client Ubuntu...")

    client_socket, addr = server_socket.accept()
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

            frame_count = 0
            while True:
                # 1. Tangkap Layar
                img = np.array(sct.grab(target_monitor))
                
                # Debug: Check if image is captured
                if img.size == 0:
                    print("[ERROR] Captured image is empty!")
                    time.sleep(1)
                    continue

                # 2. Hapus channel Alpha (Transparansi) biar ringan (BGRA -> BGR)
                frame = cv2.cvtColor(img, cv2.COLOR_BGRA2BGR)

                # 3. Resize (Opsional: Aktifkan jika lag parah)
                # frame = cv2.resize(frame, (1280, 720))

                # 4. Kompresi JPEG (Quality 0-100). 
                # Turunkan ke 50-70 jika Wi-Fi lambat. Naikkan ke 90 jika pakai Kabel LAN.
                result, encoded_frame = cv2.imencode('.jpg', frame, [int(cv2.IMWRITE_JPEG_QUALITY), 80])
                
                if not result:
                    print("[ERROR] Failed to encode frame!")
                    continue

                # 5. Serialisasi data
                data = pickle.dumps(encoded_frame)
                
                # Debug: Print frame info every 30 frames
                frame_count += 1
                if frame_count % 30 == 0:
                    print(f"[DEBUG] Sending frame {frame_count}: {frame.shape}, Size: {len(data)} bytes")
                
                # 6. Kirim ukuran data dulu (Header), baru datanya (Payload)
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
        print(f"[ERROR] Koneksi putus: {e}")
    finally:
        client_socket.close()
        server_socket.close()
        print("[INFO] Server closed")

if __name__ == '__main__':
    start_server()