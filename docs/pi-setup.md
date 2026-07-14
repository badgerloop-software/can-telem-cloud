# Pi Telemetry Setup

This document outlines the configurations and services running on the Raspberry Pi telemetry system.

## Display Settings
The Pi is connected to a 7-inch display. Since it's mounted upside-down, the screen is flipped 180 degrees via the framebuffer.
- **Rotation**: Configured via `fbcon=rotate:2` added to `/boot/firmware/cmdline.txt`.
- **Terminal Font**: Increased for readability by modifying `/etc/default/console-setup`:
  - `FONTFACE="Terminus"`
  - `FONTSIZE="16x32"`

## Network Configuration
The Pi uses NetworkManager with an **LTE-first** priority scheme (configured in `deploy/network-connect.sh` and `10-managed-devices.conf`):
- **ModemManager**: Unmasked and active. It manages the Quectel EC25 modem.
- **ttyUSB Allocation**:
  - The Quectel module enumerates multiple `/dev/ttyUSB*` ports.
  - A udev rule (`/etc/udev/rules.d/99-ignore-lte-net.rules`) ensures ModemManager ignores `/dev/ttyUSB3`, keeping it free for our Python GNSS service to query GPS and LTE signal strength via AT commands.
- **LTE Fallback**: If LTE connectivity fails, the `network-connect` service will automatically fallback to trying saved WiFi networks.

## Services
- `can-telem.service`: The main C application that reads CAN data and uploads it to InfluxDB.
- `can-telem-gnss.service`: A Python script (`tools/gnss_service.py`) that queries the LTE module over `/dev/ttyUSB3` using AT commands to get GPS location and LTE signal strength (`AT+CSQ`). It writes this data to `/run/can_telem/gnss.json`.
- `network-connect.service`: Custom script that runs on boot to bring up LTE or fallback to WiFi.
- `rfd900-reset.service`: Resets the RFD900 radio module.
- `rtc-sync.service`: Synchronizes the system clock with the RTC module.
