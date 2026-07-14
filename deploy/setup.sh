#!/usr/bin/env bash
# deploy/setup.sh - Run this on the Pi to install/update services
set -euo pipefail

# Ensure script is run as root
if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root (sudo)" 
   exit 1
fi

echo "Deploying system services and network configurations..."

# LTE scripts need ModemManager (mmcli) and GNSS/radio scripts need pyserial.
if ! dpkg -s modemmanager &>/dev/null || ! dpkg -s python3-serial &>/dev/null; then
  apt-get update
  apt-get install -y modemmanager python3-serial
fi

# 1. Copy services, timers, and default configurations
cp deploy/can0.service /etc/systemd/system/
cp deploy/can-telem.service /etc/systemd/system/
cp deploy/can-telem-gnss.service /etc/systemd/system/
cp deploy/network-connect.service /etc/systemd/system/
cp deploy/network-connect.default /etc/default/network-connect
cp deploy/rfd900-reset.service /etc/systemd/system/
cp deploy/rfd900-reset.timer /etc/systemd/system/
cp deploy/rtc-sync.service /etc/systemd/system/
cp deploy/rtc-sync-shutdown.service /etc/systemd/system/
cp deploy/rtc-sync.timer /etc/systemd/system/

# 2. Copy network overrides
cp deploy/10-managed-devices.conf /etc/NetworkManager/conf.d/
cp deploy/99-ignore-lte-net.rules /etc/udev/rules.d/

# 3. Reload systemd and udev rules
systemctl daemon-reload
udevadm control --reload-rules
udevadm trigger

# 4. Unmask and Enable services
systemctl unmask ModemManager
systemctl enable ModemManager
systemctl enable can0.service
systemctl enable can-telem.service
systemctl enable can-telem-gnss.service
systemctl enable network-connect.service
systemctl enable rfd900-reset.timer
systemctl enable rtc-sync.timer
systemctl enable rtc-sync-shutdown.service

# 5. Restart services to apply changes
systemctl restart ModemManager
systemctl restart NetworkManager
systemctl restart network-connect.service || true
systemctl restart can0.service || true
systemctl restart can-telem-gnss.service || true
systemctl restart can-telem.service || true
systemctl restart rfd900-reset.timer || true
systemctl restart rtc-sync.timer || true

echo "All services deployed successfully!"
