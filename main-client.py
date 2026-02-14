import socket
import cv2
import pickle
import struct
import numpy as np
import sys

# Configuration
SERVER_IP = '192.168.0.1'  # Change to Windows laptop IP
PORT = 9999

def start_client():
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    client_socket.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)  # Added keepalive
    
    try:
        print(f"[INFO] Connecting to {SERVER_IP}:{PORT}...")
        client_socket.settimeout(10)  # 10 second timeout
        client_socket.connect((SERVER_IP, PORT))
        client_socket.settimeout(None)  # Remove timeout after connection
        print("[INFO] Connected! Press 'q' or 'Esc' to exit.")
    except socket.timeout:
        print(f"[ERROR] Connection timeout. Check if server is running and IP is correct.")
        return
    except Exception as e:
        print(f"[ERROR] Connection failed: {e}")
        return

    data = b""
    payload_size = struct.calcsize("L")

    # Check if display is available (for headless systems)
    try:
        cv2.namedWindow("Extended Monitor", cv2.WINDOW_NORMAL)
        cv2.setWindowProperty("Extended Monitor", cv2.WND_PROP_FULLSCREEN, cv2.WINDOW_FULLSCREEN)
    except Exception as e:
        print(f"[ERROR] Cannot create window. Is X11 display available? {e}")
        client_socket.close()
        return

    try:
        while True:
            # Receive header
            while len(data) < payload_size:
                packet = client_socket.recv(4096)
                if not packet:
                    print("[INFO] Connection closed by server.")
                    return
                data += packet

            packed_msg_size = data[:payload_size]
            data = data[payload_size:]
            msg_size = struct.unpack("L", packed_msg_size)[0]

            # Receive payload
            while len(data) < msg_size:
                packet = client_socket.recv(4096)
                if not packet:
                    print("[INFO] Connection closed by server.")
                    return
                data += packet

            frame_data = data[:msg_size]
            data = data[msg_size:]

            # Decode image
            frame_numpy = pickle.loads(frame_data)
            frame = cv2.imdecode(frame_numpy, cv2.IMREAD_COLOR)

            if frame is not None:
                cv2.imshow("Extended Monitor", frame)
            
            key = cv2.waitKey(1)
            if key == ord('q') or key == 27:
                break

    except Exception as e:
        print(f"[ERROR] Error occurred: {e}")
    finally:
        client_socket.close()
        cv2.destroyAllWindows()

if __name__ == '__main__':
    start_client()