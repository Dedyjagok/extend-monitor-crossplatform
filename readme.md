# Monitor Extender - Linux Client Setup

## Quick Start (Ubuntu/Debian)

### Automatic Setup (Recommended)
```bash
chmod +x setup-ubuntu.sh
./setup-ubuntu.sh
```

### Manual Setup

1. **Install Dependencies**
```bash
sudo apt update
sudo apt install build-essential libsdl2-dev pkg-config
```

2. **Build Client**
```bash
chmod +x build-client.sh
./build-client.sh
```

3. **Run Client**
```bash
./client <server_ip>
# Example: ./client 192.168.1.100
```

## Alternative: Python Client

### Install Python Dependencies
```bash
python3 -m venv venv
source venv/bin/activate
pip install opencv-python numpy
```

### Run Python Client
```bash
python3 main-client.py <server_ip>
```

## Troubleshooting

### SDL2 Not Found
```bash
sudo apt install libsdl2-dev
```

### Permission Denied
```bash
chmod +x build-client.sh run-client.sh setup-ubuntu.sh
```

### Display Issues
Make sure you're running in a graphical environment (not SSH without X11 forwarding).

## Controls
- **ESC**: Quit application
- Runs in fullscreen mode by default

## System Requirements
- Ubuntu 20.04+ (or equivalent Debian-based distro)
- X11 or Wayland display server
- GCC 7+ with C++17 support
- SDL2 2.0+

## Network Requirements
- Open port 9999 (or configured port)
- Client and server must be on same network
- Server IP must be accessible from client