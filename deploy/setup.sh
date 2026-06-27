#!/usr/bin/env bash
# deploy/setup.sh - Run this on the Pi to install/update services
set -euo pipefail

# Ensure script is run as root
if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root (sudo)" 
   exit 1
fi

echo "Deploying system services and network configurations..."

# 1. Copy services
cp deploy/can0.service /etc/systemd/system/
cp deploy/can-telem.service /etc/systemd/system/
cp deploy/can-telem-gnss.service /etc/systemd/system/

# 2. Copy network overrides
cp deploy/10-managed-devices.conf /etc/NetworkManager/conf.d/
cp deploy/99-ignore-lte-net.rules /etc/udev/rules.d/

# 3. Reload systemd and udev rules
systemctl daemon-reload
udevadm control --reload-rules
udevadm trigger

# 4. Enable/Mask services
systemctl mask ModemManager
systemctl stop ModemManager || true
systemctl enable can0.service
systemctl enable can-telem.service
systemctl enable can-telem-gnss.service

# 5. Restart services to apply changes
systemctl restart NetworkManager
systemctl restart can0.service || true
systemctl restart can-telem-gnss.service || true
systemctl restart can-telem.service || true

echo "All services deployed successfully!"
