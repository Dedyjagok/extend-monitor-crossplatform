import socket
import cv2
import pickle
import struct
import numpy as np

# Konfigurasi Client
# GANTI INI dengan IP Laptop Windows (ASUS) Anda!
# Contoh: '192.168.0.1' (Kabel LAN) atau '192.168.1.x' (Wi-Fi)
SERVER_IP = '192.168.0.1' 
PORT = 9999

def start_client():
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    
    try:
        print(f"[INFO] Mencoba menghubungkan ke {SERVER_IP}:{PORT}...")
        client_socket.connect((SERVER_IP, PORT))
        print("[INFO] Terhubung! Tekan 'q' atau 'Esc' untuk keluar.")
    except Exception as e:
        print(f"[ERROR] Gagal terhubung: {e}")
        return

    data = b""
    # Ukuran struct "L" (4 bytes)
    payload_size = struct.calcsize("L") 

    cv2.namedWindow("Extended Monitor", cv2.WINDOW_NORMAL)
    # Set Fullscreen
    cv2.setWindowProperty("Extended Monitor", cv2.WND_PROP_FULLSCREEN, cv2.WINDOW_FULLSCREEN)

    try:
        while True:
            # 1. Terima Header (Ukuran Data)
            while len(data) < payload_size:
                packet = client_socket.recv(4096)
                if not packet: break
                data += packet
            
            if not data: break

            packed_msg_size = data[:payload_size]
            data = data[payload_size:]
            msg_size = struct.unpack("L", packed_msg_size)[0]

            # 2. Terima Payload (Data Gambar) sampai lengkap
            while len(data) < msg_size:
                data += client_socket.recv(4096)

            frame_data = data[:msg_size]
            data = data[msg_size:]

            # 3. Dekode Gambar
            frame_numpy = pickle.loads(frame_data)
            frame = cv2.imdecode(frame_numpy, cv2.IMREAD_COLOR)

            # 4. Tampilkan
            if frame is not None:
                cv2.imshow("Extended Monitor", frame)
            
            # Tekan 'q' atau 'Esc' untuk keluar
            key = cv2.waitKey(1)
            if key == ord('q') or key == 27:
                break

    except Exception as e:
        print(f"[ERROR] Terjadi kesalahan: {e}")
    finally:
        client_socket.close()
        cv2.destroyAllWindows()

if __name__ == '__main__':
    start_client()