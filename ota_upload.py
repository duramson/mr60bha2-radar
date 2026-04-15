"""OTA upload script for PlatformIO.

Usage from PlatformIO:
    pio run -e ota -t upload

Standalone (after building):
    python3 ota_upload.py [firmware.bin] [ip]
"""
import sys, os, time

def do_ota_upload(firmware_path, ip):
    """Upload firmware.bin via HTTP POST to ESP32."""
    import urllib.request
    url = f"http://{ip}/ota"
    size = os.path.getsize(firmware_path)
    print(f"OTA upload: {firmware_path} ({size} bytes) -> {url}")

    with open(firmware_path, "rb") as f:
        data = f.read()

    req = urllib.request.Request(url, data=data, method="POST")
    req.add_header("Content-Type", "application/octet-stream")
    req.add_header("Content-Length", str(len(data)))

    start = time.time()
    resp = urllib.request.urlopen(req, timeout=120)
    elapsed = time.time() - start
    body = resp.read().decode()
    print(f"Response ({resp.status}): {body}")
    print(f"Upload completed in {elapsed:.1f}s ({size/elapsed/1024:.0f} KB/s)")

# --- Standalone mode ---
if __name__ == "__main__":
    fw = sys.argv[1] if len(sys.argv) > 1 else ".pio/build/esp32-c6-devkitc-1/firmware.bin"
    ip = sys.argv[2] if len(sys.argv) > 2 else "10.0.90.164"
    do_ota_upload(fw, ip)
    sys.exit(0)

# --- PlatformIO mode ---
try:
    Import("env")

    def ota_upload_action(source, target, env):
        firmware = str(source[0])
        ip = env.GetProjectOption("custom_ota_ip", "10.0.90.164")
        do_ota_upload(firmware, ip)

    env.Replace(UPLOADCMD=ota_upload_action)
except NameError:
    pass  # Not running inside PlatformIO
