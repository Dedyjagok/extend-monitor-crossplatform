import socket
import cv2
import struct
import numpy as np

import sys

# Konfigurasi
SERVER_IP = '192.168.0.1' # Default, will be overwritten by args
PORT = 9999
MAX_DGRAM = 65535

# Check command line args for IP
if len(sys.argv) > 1:
    SERVER_IP = sys.argv[1]

def start_client():
    # Inisialisasi UDP Socket
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    # Perbesar Buffer Terima (PENTING untuk video stream)
    try:
        client_socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, MAX_DGRAM * 10)
    except:
        print("[WARN] Gagal set SO_RCVBUF. Stream mungkin lag/putus.")
    
    # Timeout agar tidak hang selamanya
    client_socket.settimeout(5) # 5 detik

    # Handshake: Kirim 'HELLO' ke server agar server tahu IP kita
    print(f"[INFO] Mengirim Hello ke Server {SERVER_IP}...")
    try:
        # Kirim beberapa kali untuk memastikan server terima
        for _ in range(3):
            client_socket.sendto(b'HELLO', (SERVER_IP, PORT))
    except Exception as e:
        print(f"[ERROR] Gagal kirim Hello: {e}")
        return

    cv2.namedWindow("Extender UDP", cv2.WINDOW_NORMAL)
    cv2.setWindowProperty("Extender UDP", cv2.WND_PROP_FULLSCREEN, cv2.WINDOW_FULLSCREEN)

    print("[INFO] Menunggu stream...")
    frame_count = 0
    
    while True:
        try:
            # Terima paket UDP
            packet, _ = client_socket.recvfrom(MAX_DGRAM + 100)
            
            # Parsing Header (4 bytes pertama adalah ukuran ulong - "L" = 4 bytes)
            if len(packet) < 4: 
                continue
            
            # Baca size
            msg_size_bytes = packet[:4]
            msg_size = struct.unpack("L", msg_size_bytes)[0]
            
            # Ambil data gambar
            data = packet[4:]

            # Validasi ukuran data
            if len(data) != msg_size:
                # Data rusak/terpotong (UDP packet loss)
                continue

            # Decode JPEG
            frame_numpy = np.frombuffer(data, dtype=np.uint8)
            frame = cv2.imdecode(frame_numpy, cv2.IMREAD_COLOR)

            if frame is not None:
                cv2.imshow("Extender UDP", frame)
                frame_count += 1
            
            # Exit key
            if cv2.waitKey(1) == 27: # ESC
                break
                
        except socket.timeout:
            print("[INFO] Timeout... Mengirim Hello lagi...")
            client_socket.sendto(b'HELLO', (SERVER_IP, PORT))
        except Exception as e:
            # print(f"[ERROR] Recv: {e}")
            pass

    client_socket.close()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    start_client()