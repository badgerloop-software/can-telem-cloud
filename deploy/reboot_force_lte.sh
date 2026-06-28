#!/usr/bin/env bash
set -e

echo "[reboot_force_lte] Disabling WiFi autoconnect..."
sudo nmcli connection modify UWNet connection.autoconnect no || true
sudo nmcli radio wifi off || true

echo "[reboot_force_lte] Ensuring LTE is set to autoconnect..."
sudo nmcli connection modify lte connection.autoconnect yes || true

echo "[reboot_force_lte] Rebooting the Pi now..."
sudo reboot
