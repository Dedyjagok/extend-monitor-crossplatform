import socket
import cv2
import struct
import numpy as np
import sys

# Konfigurasi
SERVER_IP = '192.168.0.1' # Default, will be overwritten by args
PORT = 9999

# Check command line args for IP
if len(sys.argv) > 1:
    SERVER_IP = sys.argv[1]

def recv_all(sock, count):
    """Helper to receive exactly count bytes."""
    buf = b''
    while count:
        newbuf = sock.recv(count)
        if not newbuf: return None
        buf += newbuf
        count -= len(newbuf)
    return buf

def start_client():
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    client_socket.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
    client_socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1) # Low latency

    print(f"[INFO] Connecting to {SERVER_IP}:{PORT}...")
    try:
        client_socket.settimeout(5)
        client_socket.connect((SERVER_IP, PORT))
        client_socket.settimeout(None) # Remove timeout
        print("[INFO] Connected to TCP Server!")
    except Exception as e:
        print(f"[ERROR] Connection failed: {e}")
        return

    cv2.namedWindow("Extender TCP", cv2.WINDOW_NORMAL)
    cv2.setWindowProperty("Extender TCP", cv2.WND_PROP_FULLSCREEN, cv2.WINDOW_FULLSCREEN)

    try:
        while True:
            # 1. Read Header (8 bytes for !Q size)
            header_data = recv_all(client_socket, 8)
            if not header_data:
                print("[ERROR] Failed to receive header (Server disconnected?)")
                break
            
            try:
                msg_size = struct.unpack("!Q", header_data)[0]
            except Exception as e:
                print(f"[ERROR] Failed to unpack header: {e}")
                break

            # 2. Read Payload (Image Data)
            frame_data = recv_all(client_socket, msg_size)
            if not frame_data:
                print(f"[ERROR] Failed to receive payload of size {msg_size}")
                break

            # 3. Decode
            frame_numpy = np.frombuffer(frame_data, dtype=np.uint8)
            frame = cv2.imdecode(frame_numpy, cv2.IMREAD_COLOR)

            if frame is not None:
                cv2.imshow("Extender TCP", frame)
            else:
                print("[WARN] Received empty/invalid frame")
            
            if cv2.waitKey(1) == 27: # ESC
                print("[INFO] User pressed ESC")
                break

    except Exception as e:
        print(f"[ERROR] Stream loop error: {e}")
    finally:
        client_socket.close()
        cv2.destroyAllWindows()
        print("[INFO] Client closed")

if __name__ == "__main__":
    start_client()