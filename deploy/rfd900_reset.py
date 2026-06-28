import serial
import time
import subprocess

PORT = '/dev/ttyAMA0'

def log(msg):
    print(f"[rfd900_reset] {msg}", flush=True)

def run_cmd(cmd):
    try:
        subprocess.run(cmd, shell=True, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return True
    except subprocess.CalledProcessError as e:
        log(f"Command failed: {cmd}. Error: {e.stderr.decode().strip()}")
        return False

def main():
    log("Stopping can-telem.service...")
    run_cmd("sudo systemctl stop can-telem.service")
    time.sleep(1.0)
    
    try:
        for baud in [115200, 57600]:
            log(f"Sending reset sequence at {baud} baud...")
            try:
                ser = serial.Serial(PORT, baud, timeout=1)
                ser.read_all()
                time.sleep(1.2)
                
                # Send +++
                ser.write(b'+++')
                time.sleep(1.2)
                
                # Write target registers blindly
                ser.write(b'ATS3=420\r\n')
                time.sleep(0.1)
                ser.write(b'ATS2=96\r\n')
                time.sleep(0.1)
                ser.write(b'AT&W\r\n')
                time.sleep(0.2)
                ser.write(b'ATZ\r\n')
                time.sleep(0.5)
                ser.close()
                log(f"Payload sent successfully at {baud} baud.")
            except Exception as e:
                log(f"Failed at {baud}: {e}")
                
        log("Waiting for radio to reboot...")
        time.sleep(3.0)
    except Exception as e:
        log(f"Error during radio reset sequence: {e}")
        
    log("Restarting can-telem.service...")
    run_cmd("sudo systemctl start can-telem.service")
    log("Reset complete!")

if __name__ == '__main__':
    main()
