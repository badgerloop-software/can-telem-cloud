#!/usr/bin/env python3
import json
import os
import re
import serial
import time
from pathlib import Path

OUT_PATH = Path("/run/can_telem/gnss.json")
OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
PORT = '/dev/ttyUSB3'  # ttyUSB2 is claimed by ModemManager for LTE; ttyUSB3 is the free AT port
BAUD = 115200

def log(msg):
    print(f"[gnss_service] {msg}", flush=True)

def convert_ddmm_to_decimal(val, direction):
    # Matches N/S/E/W format: dddd.mmmm
    m = re.match(r"(\d+)(\d{2}\.\d+)([NSEW])", f"{val}{direction}")
    if not m:
        return None
    deg = float(m.group(1))
    minutes = float(m.group(2))
    direction_char = m.group(3)
    
    decimal = deg + (minutes / 60.0)
    if direction_char in ['S', 'W']:
        decimal = -decimal
    return decimal

def parse_qgpsloc_line(line):
    # Format: +QGPSLOC: <UTC>,<latitude>,<longitude>,<hdop>,<altitude>,<fix>,<cog>,<spkm>,<spkn>,<date>,<nsat>
    m = re.search(r"\+QGPSLOC:\s*(.+)", line)
    if not m:
        return None
    parts = m.group(1).split(',')
    if len(parts) < 5:
        return None
    
    raw_lat = parts[1]
    raw_lon = parts[2]
    raw_elev = parts[4]
    
    if not raw_lat or not raw_lon:
        return None
        
    lat = convert_ddmm_to_decimal(raw_lat[:-1], raw_lat[-1])
    lon = convert_ddmm_to_decimal(raw_lon[:-1], raw_lon[-1])
    elev = float(raw_elev) if raw_elev else 0.0
    
    return lat, lon, elev

def write_fix(lat, lon, elev):
    payload = {
        "lat": lat,
        "lon": lon,
        "elev": elev,
        "timestamp_ns": time.time_ns(),
    }
    tmp = OUT_PATH.with_suffix(".tmp")
    tmp.write_text(json.dumps(payload), encoding="utf-8")
    os.replace(tmp, OUT_PATH)

def main():
    log("Starting AT-based GNSS service...")
    
    # Try to open serial port
    ser = None
    while True:
        try:
            if ser is None or not ser.is_open:
                ser = serial.Serial(PORT, BAUD, timeout=2)
                log(f"Opened serial port {PORT} successfully.")
                # Ensure GPS is enabled
                ser.write(b"AT+QGPS?\r\n")
                time.sleep(0.2)
                status_resp = ser.read_all().decode(errors='ignore')
                if "+QGPS: 0" in status_resp:
                    log("GPS is disabled. Enabling QGPS...")
                    ser.write(b"AT+QGPS=1\r\n")
                    time.sleep(0.5)
                    log(ser.read_all().decode(errors='ignore').strip())
                    
            # Poll location
            ser.write(b"AT+QGPSLOC=0\r\n")
            time.sleep(0.5)
            resp = ser.read_all().decode(errors='ignore')
            
            if "+QGPSLOC:" in resp:
                # Parse lines
                for line in resp.splitlines():
                    if "+QGPSLOC:" in line:
                        parsed = parse_qgpsloc_line(line)
                        if parsed:
                            lat, lon, elev = parsed
                            write_fix(lat, lon, elev)
                            # Optional logging
                            # log(f"Fix updated: {lat}, {lon}, {elev}")
            elif "+CME ERROR: 516" in resp:
                # GPS is active, searching for fix (Not fixed now)
                pass
            elif "+CME ERROR: 502" in resp or "ERROR" in resp:
                # Check if GPS was disabled
                ser.write(b"AT+QGPS?\r\n")
                time.sleep(0.2)
                status_resp = ser.read_all().decode(errors='ignore')
                if "+QGPS: 0" in status_resp:
                    log("GPS disabled unexpectedly. Re-enabling...")
                    ser.write(b"AT+QGPS=1\r\n")
                    time.sleep(0.5)
                    ser.read_all()
                    
            time.sleep(1.0)
        except Exception as e:
            log(f"Error in main loop: {e}")
            if ser:
                try:
                    ser.close()
                except Exception:
                    pass
                ser = None
            time.sleep(2.0)

if __name__ == "__main__":
    main()
