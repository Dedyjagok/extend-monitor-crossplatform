#!/bin/bash
# run-client.sh - Easy launcher for the client
# Usage: ./run-client.sh [server_ip]

set -e

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "========================================"
echo "Monitor Extender - Client Launcher"
echo "========================================"
echo ""

# Check if client exists
if [ ! -f "client" ]; then
    echo -e "${RED}[ERROR] Client executable not found${NC}"
    echo ""
    echo "Build it first with:"
    echo "  ./build-client.sh"
    exit 1
fi

# Get server IP
if [ $# -eq 0 ]; then
    echo -e "${YELLOW}[INFO] No server IP provided${NC}"
    echo ""
    read -p "Enter server IP address: " SERVER_IP
    
    if [ -z "$SERVER_IP" ]; then
        echo -e "${RED}[ERROR] Server IP is required${NC}"
        exit 1
    fi
else
    SERVER_IP=$1
fi

echo "[INFO] Connecting to: $SERVER_IP:9999"
echo "[INFO] Press ESC to quit"
echo ""

# Run client
./client "$SERVER_IP"

echo ""
echo "[INFO] Client closed"