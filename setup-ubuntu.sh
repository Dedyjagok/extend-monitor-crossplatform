#!/bin/bash
# setup-ubuntu.sh - Complete setup for Ubuntu clients
# This installs all dependencies and builds the client

set -e

echo "========================================"
echo "Monitor Extender - Ubuntu Setup"
echo "========================================"
echo ""

# Color codes
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "[INFO] This script will:"
echo "  1. Install required dependencies"
echo "  2. Build the C++ client"
echo "  3. Set up Python environment (optional)"
echo ""
read -p "Continue? (y/n) " -n 1 -r
echo ""

if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Setup cancelled"
    exit 0
fi

# Update package list
echo "[INFO] Updating package list..."
sudo apt update

# Install build essentials
echo "[INFO] Installing build essentials..."
sudo apt install -y build-essential pkg-config

# Install SDL2
echo "[INFO] Installing SDL2..."
sudo apt install -y libsdl2-dev

# Optional: Install Python dependencies
echo ""
read -p "Install Python client dependencies? (y/n) " -n 1 -r
echo ""
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "[INFO] Installing Python and pip..."
    sudo apt install -y python3 python3-pip python3-venv
    
    echo "[INFO] Creating virtual environment..."
    python3 -m venv venv
    source venv/bin/activate
    
    echo "[INFO] Installing Python packages..."
    pip install --upgrade pip
    pip install opencv-python numpy
    
    echo -e "${GREEN}[SUCCESS] Python environment ready${NC}"
    echo "Activate with: source venv/bin/activate"
    deactivate
fi

echo ""
echo "[INFO] Building C++ client..."
chmod +x build-client.sh
./build-client.sh

echo ""
echo -e "${GREEN}========================================"
echo "Setup Complete!"
echo "========================================${NC}"
echo ""
echo "To run the client:"
echo "  ./run-client.sh <server_ip>"
echo ""
echo "Or directly:"
echo "  ./client 192.168.1.100"
echo ""
echo "Python client (if installed):"
echo "  source venv/bin/activate"
echo "  python3 main-client.py <server_ip>"
echo ""