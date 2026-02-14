#!/bin/bash
# build-client.sh - Automated build script for Ubuntu/Linux client
# Usage: chmod +x build-client.sh && ./build-client.sh

set -e  # Exit on error

echo "========================================"
echo "Monitor Extender - Linux Client Builder"
echo "========================================"
echo ""

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if running on Linux
if [[ "$OSTYPE" != "linux-gnu"* ]]; then
    echo -e "${YELLOW}[WARNING] This script is designed for Linux/Ubuntu${NC}"
    echo "For other platforms, manual configuration may be needed"
    echo ""
fi

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Check for g++
echo "[INFO] Checking for C++ compiler..."
if ! command_exists g++; then
    echo -e "${RED}[ERROR] g++ not found${NC}"
    echo "Install with: sudo apt install build-essential"
    exit 1
fi

GCC_VERSION=$(g++ --version | head -n1)
echo -e "${GREEN}[SUCCESS] Found: $GCC_VERSION${NC}"
echo ""

# Check for SDL2 development libraries
echo "[INFO] Checking for SDL2..."
SDL2_FOUND=false

if pkg-config --exists sdl2; then
    SDL2_FOUND=true
    SDL2_VERSION=$(pkg-config --modversion sdl2)
    echo -e "${GREEN}[SUCCESS] SDL2 $SDL2_VERSION found${NC}"
elif [ -f "/usr/include/SDL2/SDL.h" ]; then
    SDL2_FOUND=true
    echo -e "${GREEN}[SUCCESS] SDL2 headers found${NC}"
fi

if [ "$SDL2_FOUND" = false ]; then
    echo -e "${RED}[ERROR] SDL2 development libraries not found${NC}"
    echo ""
    echo "Install SDL2 with:"
    echo "  sudo apt update"
    echo "  sudo apt install libsdl2-dev"
    echo ""
    read -p "Would you like to install SDL2 now? (y/n) " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo "[INFO] Installing SDL2..."
        sudo apt update
        sudo apt install -y libsdl2-dev
        echo -e "${GREEN}[SUCCESS] SDL2 installed${NC}"
    else
        echo -e "${YELLOW}[ABORT] Cannot build without SDL2${NC}"
        exit 1
    fi
fi
echo ""

# Build client
echo "[INFO] Building client..."
echo "[INFO] Compiler flags: -std=c++17 -O3 -Wall"
echo ""

g++ -std=c++17 -O3 -Wall client.cpp -o client \
    $(pkg-config --cflags --libs sdl2) \
    -lpthread

if [ $? -eq 0 ]; then
    echo ""
    echo -e "${GREEN}[SUCCESS] client executable built successfully${NC}"
    echo ""
    
    # Make executable
    chmod +x client
    
    # Display info
    echo "========================================"
    echo "Build Complete!"
    echo "========================================"
    echo ""
    echo "Executable: ./client"
    echo "Size: $(du -h client | cut -f1)"
    echo ""
    echo "Usage:"
    echo "  ./client <server_ip>"
    echo ""
    echo "Example:"
    echo "  ./client 192.168.1.100"
    echo ""
    echo "Notes:"
    echo "  - Press ESC to quit"
    echo "  - Runs in fullscreen mode"
    echo "  - Server must be running on Windows machine"
    echo ""
else
    echo ""
    echo -e "${RED}[ERROR] Build failed${NC}"
    echo "Check the error messages above"
    exit 1
fi