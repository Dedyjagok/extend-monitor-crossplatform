import socket
import cv2
import struct
import numpy as np
import threading
import time
import win32gui

# Try to import DXCam, fallback to MSS if failed
try:
    import dxcam
    DXCAM_AVAILABLE = True
except ImportError:
    print("[WARN] DXCam not installed. Falling back to MSS.")
    import mss
    DXCAM_AVAILABLE = False

# Konfigurasi
HOST_IP = '0.0.0.0'
PORT = 9999
WIDTH, HEIGHT = 1280, 720 # Target Resolution

# Inisialisasi UDP Socket
server_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
try:
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 65535)
except:
    pass

# Global Variables
camera = None
sct = None
client_addr = None
running = True
TARGET_MONITOR_IDX = 1 

def init_camera():
    global camera, sct, DXCAM_AVAILABLE
    
    if DXCAM_AVAILABLE:
        try:
            # Output_idx=1 for second monitor
            camera = dxcam.create(output_idx=TARGET_MONITOR_IDX, output_color="BGR")
            print(f"[INFO] DXCam initialized on monitor {TARGET_MONITOR_IDX}")
            return True
        except Exception as e:
            print(f"[ERROR] DXCam init failed on monitor {TARGET_MONITOR_IDX}: {e}")
            try:
                # Fallback to monitor 0
                camera = dxcam.create(output_idx=0, output_color="BGR")
                print(f"[INFO] Fallback: DXCam initialized on monitor 0")
                return True
            except Exception as e2:
                print(f"[ERROR] DXCam failed completely: {e2}")
                DXCAM_AVAILABLE = False
    
    if not DXCAM_AVAILABLE:
        print("[INFO] Using MSS for screen capture")
        sct = mss.mss()
        return True
    
    return False

def get_frame():
    global camera, sct
    if DXCAM_AVAILABLE and camera is not None:
        return camera.get_latest_frame()
    elif sct is not None:
        try:
            # Check monitor count
            if len(sct.monitors) > TARGET_MONITOR_IDX + 1:
                mon = sct.monitors[TARGET_MONITOR_IDX + 1] # mss is 1-indexed for specific monitors
            else:
                mon = sct.monitors[1] # Primary
            
            img = np.array(sct.grab(mon))
            return cv2.cvtColor(img, cv2.COLOR_BGRA2BGR)
        except Exception as e:
            # print(f"MSS Error: {e}")
            return None
    return None

def wait_for_handshake():
    """Menunggu client mengirim 'HELLO' via UDP agar server tahu IP client"""
    global client_addr
    print(f"[INFO] Listening for Client Handshake on {HOST_IP}:{PORT}...")
    while client_addr is None and running:
        try:
            data, addr = server_socket.recvfrom(1024)
            print(f"[DEBUG] Received data from {addr}: {data}")
            if data.startswith(b'HELLO'):
                client_addr = addr
                print(f"[INFO] === Client CONNECTED: {addr} ===")
                # Send ack back
                server_socket.sendto(b'ACK', addr)
        except Exception as e:
            # print(f"Handshake error: {e}")
            pass
        time.sleep(0.1)

def start_stream():
    if not init_camera():
        print("[FATAL] Could not initialize any capture method.")
        return

    if DXCAM_AVAILABLE:
        print("[INFO] Starting DXCam Capture (60 FPS Target)...")
        camera.start(target_fps=60, video_mode=True)
    else:
        print("[INFO] Starting MSS Capture...")

    print(f"[INFO] Stream Ready. Waiting for client at {PORT}...")

    # Wait until client connects
    while client_addr is None and running:
        time.sleep(1)
        print("[INFO] Waiting for client connection...")

    print("[INFO] Streaming started!")
    
    frame_count = 0
    while running:
        if client_addr is None:
            time.sleep(0.5)
            continue

        # 1. Capture
        frame = get_frame()
        if frame is None:
            time.sleep(0.01)
            continue

        # 2. Resize
        frame = cv2.resize(frame, (WIDTH, HEIGHT))

        # 3. Draw Cursor using win32gui
        try:
             flags, hcursor, (gx, gy) = win32gui.GetCursorInfo()
             # Manual Offset Hack: Adjust based on your setup
             rel_x = gx - 1920 
             rel_y = gy
             
             if 0 <= rel_x < WIDTH and 0 <= rel_y < HEIGHT:
                 cv2.circle(frame, (rel_x, rel_y), 5, (0, 0, 255), -1)
        except:
            pass

        # 4. Encode
        _, encoded = cv2.imencode('.jpg', frame, [int(cv2.IMWRITE_JPEG_QUALITY), 60])
        data = encoded.tobytes()
        
        # 5. Send UDP
        try:
            if len(data) < 62000:
                server_socket.sendto(struct.pack("L", len(data)) + data, client_addr)
        except Exception as e:
            pass 

    if DXCAM_AVAILABLE:
        camera.stop()
    server_socket.close()
    print("[INFO] Server Stopped")

if __name__ == "__main__":
    try:
        t = threading.Thread(target=wait_for_handshake, daemon=True)
        t.start()
        start_stream()
    except KeyboardInterrupt:
        running = False
        print("[INFO] Keyboard Interrupt")