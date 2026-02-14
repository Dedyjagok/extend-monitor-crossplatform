import socket
import struct
import time
import threading
import cv2
import numpy as np
import mss
import win32gui
import win32con

# Konfigurasi Server
HOST_IP = "0.0.0.0"
PORT = 9999
WIDTH = 960  # qHD Resolution (1/4 of 1080p)
HEIGHT = 540

# Global Variables
camera = None
sct = None
TARGET_MONITOR_IDX = 1 # 0=All, 1=Main, 2=Secondary (in MSS)
FORCE_GPU_INDEX = 1    # FORCED to Device 1 (RTX 2050)
monitor_info = {"top": 0, "left": 0, "width": 1920, "height": 1080}

# DXCam (GPU Capture)
try:
    import dxcam
    DXCAM_AVAILABLE = True
except ImportError:
    DXCAM_AVAILABLE = False
    print("[WARNING] dxcam not installed. GPU capture unavailable.")

def get_cursor_info():
    try:
        flags, hcursor, (x, y) = win32gui.GetCursorInfo()
        return x, y
    except:
        return 0, 0

def init_camera():
    global camera, sct, DXCAM_AVAILABLE, monitor_info
    
    # 1. Detect Monitors via MSS
    with mss.mss() as sct_temp:
        monitors = sct_temp.monitors
        print(f"\n[INFO] --- Monitor Detection ---")
        print(f"[INFO] MSS (OS) Monitors: {len(monitors)-1}")
        
        target_mss_idx = 2 if len(monitors) > 2 else 1
        monitor_info = monitors[target_mss_idx]
        print(f"[INFO] Target Monitor: {target_mss_idx} (Extended)" if target_mss_idx == 2 else f"[WARN] Target Monitor: {target_mss_idx} (Main - Mirrored)")

    # 2. Advanced GPU Scanning (DXCam)
    if DXCAM_AVAILABLE and len(monitors) > 2:
        print(f"\n[INFO] --- Scanning GPUs for Extended Monitor ---")
        
        # A. Manual Override with better error handling
        if FORCE_GPU_INDEX != -1:
            print(f"[INFO] FORCING GPU Device {FORCE_GPU_INDEX} (User Override)...")
            # Try different output indices (0 and 1)
            for output_idx in [1, 0]:
                try:
                    print(f"  > Trying Device {FORCE_GPU_INDEX}, Output {output_idx}...")
                    temp_camera = dxcam.create(device_idx=FORCE_GPU_INDEX, output_idx=output_idx, output_color="BGR")
                    
                    # Test if we can capture a frame
                    test_frame = temp_camera.grab()
                    if test_frame is not None and test_frame.shape[0] > 0:
                        print(f"[SUCCESS] Forced connection successful!")
                        print(f"[SUCCESS] [GPU ACTIVE] Using Device {FORCE_GPU_INDEX} (Output {output_idx})")
                        print(f"    Captured test frame: {test_frame.shape[1]}x{test_frame.shape[0]}")
                        camera = temp_camera
                        return True
                    else:
                        temp_camera.release()
                        print(f"    [FAILED] Output {output_idx}: Could not capture frame")
                except Exception as e:
                    print(f"    [FAILED] Output {output_idx}: {e}")
                    
            print(f"[ERROR] Could not connect to Device {FORCE_GPU_INDEX} on any output")
            print("Reverting to Auto-Scan...")

        # B. Auto-Scan with better detection
        found_cameras = {}
        
        # Scan devices 0-3, trying both output indices
        for device_idx in range(4):
            print(f"[INFO] Checking GPU Device {device_idx}...")
            
            for output_idx in [1, 0]:  # Try extended monitor output first
                try:
                    temp_cam = dxcam.create(device_idx=device_idx, output_idx=output_idx, output_color="BGR")
                    
                    # Test capture
                    test_frame = temp_cam.grab()
                    if test_frame is not None and test_frame.shape[0] > 0:
                        print(f"  > [SUCCESS] Monitor FOUND on Device {device_idx}, Output {output_idx}")
                        print(f"    Resolution: {test_frame.shape[1]}x{test_frame.shape[0]}")
                        found_cameras[(device_idx, output_idx)] = temp_cam
                        break  # Found working output for this device
                    else:
                        temp_cam.release()
                except Exception as e:
                    pass

        # SELECT THE BEST GPU
        if not found_cameras:
            print("\n[WARNING] Could not find Extended Monitor on ANY GPU.")
            print("  -> Your Virtual Display Driver might be software-only.")
        else:
            # User prefers Device 1 (RTX), so check if we have it
            selected_key = None
            
            # Logic: If we found extended monitor on multiple GPUs, prefer Device 1
            for key in found_cameras.keys():
                if key[0] == 1:  # Device 1
                    selected_key = key
                    print(f"[SUCCESS] [dGPU PRIORITY] Selecting Device 1, Output {key[1]} (RTX 2050)")
                    break
            
            # If Device 1 not found, try Device 0
            if not selected_key:
                for key in found_cameras.keys():
                    if key[0] == 0:  # Device 0
                        selected_key = key
                        print(f"[SUCCESS] [iGPU] Selecting Device 0, Output {key[1]} (RTX not available for this Display)")
                        break
            
            # If neither 0 nor 1, pick the first one
            if not selected_key:
                selected_key = list(found_cameras.keys())[0]
                print(f"[SUCCESS] Selecting Device {selected_key[0]}, Output {selected_key[1]}")

            # Set global camera
            camera = found_cameras[selected_key]
            
            # Release others
            for key, cam in found_cameras.items():
                if key != selected_key:
                    try:
                        cam.stop()
                        cam.release()
                        del cam
                    except:
                        pass
            
            return True
        
    # 3. Fallback
    print(f"[INFO] [CPU FALLBACK] Using MSS on Monitor {target_mss_idx}")
    sct = mss.mss()
    return True

def get_frame():
    global camera, sct, monitor_info
    if camera is not None:
         # DXCam
         return camera.get_latest_frame()
    elif sct is not None:
        try:
            # Grab specific monitor region from MSS
            img = np.array(sct.grab(monitor_info))
            return cv2.cvtColor(img, cv2.COLOR_BGRA2BGR)
        except Exception as e:
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
    print(f"[INFO] Resolution: {WIDTH}x{HEIGHT} | JPEG Quality: 30")

    if not init_camera():
        print("[FATAL] Could not initialize any capture method.")
        return

    if camera:
        print("[INFO] Starting DXCam Capture...")
        # DXCam 0.0.5: Use default buffer (latest frame usually implies low latency anyway)
        camera.start(target_fps=60, video_mode=True)
    else:
        print("Starting MSS Capture...")

    try:
        while True:
            print("[INFO] Waiting for client connection...")
            client_socket, addr = server_socket.accept()
            print(f"[INFO] Client connected from: {addr}")
            client_socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

            try:
                # Capture Loop
                fps_start = time.time()
                frame_count = 0
                
                while True:
                    frame = get_frame()
                    if frame is None:
                        continue

                    # Resize (Optimized)
                    frame_resized = cv2.resize(frame, (WIDTH, HEIGHT), interpolation=cv2.INTER_NEAREST)

                    # Draw Cursor
                    if True: # Always draw cursor for now
                        cur_x, cur_y = get_cursor_info()
                        # Calculate relative position
                        # monitor_info = {'left': ..., 'top': ...}
                        rel_x = cur_x - monitor_info['left']
                        rel_y = cur_y - monitor_info['top']
                        
                        # Scale to resized resolution
                        scale_x = WIDTH / (monitor_info['width'] or 1)
                        scale_y = HEIGHT / (monitor_info['height'] or 1)
                        
                        final_x = int(rel_x * scale_x)
                        final_y = int(rel_y * scale_y)
                        
                        cv2.circle(frame_resized, (final_x, final_y), 5, (0, 0, 255), -1)

                    # Encode JPEG
                    ret, buffer = cv2.imencode('.jpg', frame_resized, [cv2.IMWRITE_JPEG_QUALITY, 30])
                    if not ret: continue
                    
                    data = buffer.tobytes()
                    size = len(data)

                    # Send Size + Data
                    # '!Q' = unsigned long long (8 bytes) - Matches Client
                    client_socket.sendall(struct.pack('!Q', size) + data)
                    
                    # FPS Counter
                    frame_count += 1
                    if time.time() - fps_start >= 5.0:
                        fps = frame_count / (time.time() - fps_start)
                        print(f"[INFO] FPS: {fps:.1f} | Size: {size/1024:.1f}KB")
                        frame_count = 0
                        fps_start = time.time()

            except (ConnectionResetError, BrokenPipeError):
                print("[INFO] Client disconnected.")
            finally:
                client_socket.close()
                if camera: 
                    try:
                        camera.stop()
                    except: pass

    except KeyboardInterrupt:
        print("[INFO] Server stopped.")
    finally:
        server_socket.close()

if __name__ == "__main__":
    start_server()