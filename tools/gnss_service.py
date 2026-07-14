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

def parse_csq(response):
    """Parse AT+CSQ response. Returns (rssi_raw, rssi_dbm) or (None, None).
    rssi_raw: 0-31 (99 = not known/detectable)
    rssi_dbm: -113 + 2*rssi_raw  (standard mapping per 3GPP TS 27.007)
    """
    m = re.search(r'\+CSQ:\s*(\d+),', response)
    if not m:
        return None, None
    rssi_raw = int(m.group(1))
    if rssi_raw == 99:
        return 99, None  # unknown
    rssi_dbm = -113 + 2 * rssi_raw
    return rssi_raw, rssi_dbm


def write_fix(lat, lon, elev, rssi_raw=None, rssi_dbm=None):
    payload = {
        "lat": lat,
        "lon": lon,
        "elev": elev,
        "timestamp_ns": time.time_ns(),
    }
    if rssi_raw is not None:
        payload["rssi_raw"] = rssi_raw
    if rssi_dbm is not None:
        payload["rssi_dbm"] = rssi_dbm
    tmp = OUT_PATH.with_suffix(".tmp")
    tmp.write_text(json.dumps(payload), encoding="utf-8")
    os.replace(tmp, OUT_PATH)

def main():
    log("Starting AT-based GNSS service...")

    # Cached signal strength (updated every loop; persists even without a GPS fix)
    last_rssi_raw = None
    last_rssi_dbm = None

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

            # --- Query LTE signal strength (AT+CSQ) ---
            ser.write(b"AT+CSQ\r\n")
            time.sleep(0.2)
            csq_resp = ser.read_all().decode(errors='ignore')
            rssi_raw, rssi_dbm = parse_csq(csq_resp)
            if rssi_raw is not None:
                last_rssi_raw = rssi_raw
                last_rssi_dbm = rssi_dbm

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
                            write_fix(lat, lon, elev, last_rssi_raw, last_rssi_dbm)
                            # Optional logging
                            # log(f"Fix updated: {lat}, {lon}, {elev}, rssi={last_rssi_dbm} dBm")
            elif "+CME ERROR: 516" in resp:
                # GPS is active, searching for fix (Not fixed now)
                # Still update or create the cache with signal strength
                try:
                    import json as _json
                    data = {}
                    if OUT_PATH.exists():
                        try:
                            data = _json.loads(OUT_PATH.read_text(encoding='utf-8'))
                        except Exception:
                            pass
                    if last_rssi_raw is not None:
                        data['rssi_raw'] = last_rssi_raw
                    if last_rssi_dbm is not None:
                        data['rssi_dbm'] = last_rssi_dbm
                    
                    # Ensure timestamp exists so C code knows it's fresh
                    if 'timestamp_ns' not in data:
                        data['timestamp_ns'] = time.time_ns()

                    tmp = OUT_PATH.with_suffix(".tmp")
                    tmp.write_text(_json.dumps(data), encoding='utf-8')
                    os.replace(tmp, OUT_PATH)
                except Exception:
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
