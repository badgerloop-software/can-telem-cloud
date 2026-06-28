#!/usr/bin/env bash
set -e

echo "[enable_wifi] Enabling WiFi radio and autoconnect..."
sudo nmcli radio wifi on || true
sudo nmcli connection modify UWNet connection.autoconnect yes || true
echo "[enable_wifi] WiFi has been enabled!"
