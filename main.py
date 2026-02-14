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

# Global Variables
camera = None
sct = None
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

def start_server():
    # Inisialisasi TCP Socket
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1) # Low latency

    try:
        server_socket.bind((HOST_IP, PORT))
    except Exception as e:
        print(f"[ERROR] Bind failed: {e}")
        return

    server_socket.listen(1)
    print(f"[INFO] TCP Server listening on {HOST_IP}:{PORT}")

    if not init_camera():
        print("[FATAL] Could not initialize any capture method.")
        return

    if DXCAM_AVAILABLE:
        print("[INFO] Starting DXCam Capture (60 FPS Target)...")
        camera.start(target_fps=60, video_mode=True)
    else:
        print("[INFO] Starting MSS Capture...")

    try:
        while True:
            print("[INFO] Waiting for client connection...")
            client_socket, addr = server_socket.accept()
            print(f"[INFO] Client connected from: {addr}")
            
            # Low latency settings for client socket
            client_socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

            try:
                frame_count = 0
                while True:
                    # 1. Capture
                    frame = get_frame()
                    if frame is None:
                        time.sleep(0.001) # Ultra short sleep
                        continue

                    # 2. Resize
                    frame = cv2.resize(frame, (WIDTH, HEIGHT))

                    # 3. Draw Cursor using win32gui
                    try:
                        flags, hcursor, (gx, gy) = win32gui.GetCursorInfo()
                        # Manual Offset Hack: Adjust based on your setup
                        # Assuming Monitor 2 is to the right of Monitor 1 (1920x1080)
                        rel_x = gx - 1920 
                        rel_y = gy
                        
                        if 0 <= rel_x < WIDTH and 0 <= rel_y < HEIGHT:
                            cv2.circle(frame, (rel_x, rel_y), 5, (0, 0, 255), -1)
                    except:
                        pass

                    # 4. Encode
                    # Quality 60 is a good balance for speed/size
                    _, encoded = cv2.imencode('.jpg', frame, [int(cv2.IMWRITE_JPEG_QUALITY), 60])
                    data = encoded.tobytes()
                    
                    # 5. Send TCP (8-byte header + payload)
                    # !Q = unsigned long long (8 bytes)
                    msg_size = struct.pack("!Q", len(data))
                    client_socket.sendall(msg_size + data)
                    
                    frame_count += 1
                    
            except (ConnectionResetError, BrokenPipeError):
                print("[INFO] Client disconnected")
            except Exception as e:
                print(f"[ERROR] Stream error: {e}")
            finally:
                client_socket.close()

    except KeyboardInterrupt:
        print("[INFO] Server stopping...")
    finally:
        if DXCAM_AVAILABLE:
            camera.stop()
        server_socket.close()
        print("[INFO] Server closed")

if __name__ == "__main__":
    start_server()