# NOBD-DC-ONLINE — WiFi to SPI Bridge
# Receives UDP MFP frames, forwards via SPI to GP2040-CE
# Pico 2 W GP11 (MOSI) -> GP2040-CE GPIO 27 (SPI1 TX)
# Pico 2 W GP10 (SCK)  -> GP2040-CE GPIO 26 (SPI1 SCK)
# Pico 2 W GP13 (CS)   -> GP2040-CE GPIO 25 (SPI1 CS)
# Pico 2 W GP12 (MISO) -> GP2040-CE GPIO 24 (SPI1 RX)
# Physical buttons always work — network only active when UDP arrives

import network
import socket
import machine
import time

WIFI_SSID = "YOUR_WIFI_SSID"          # set to your network name
WIFI_PASSWORD = "YOUR_WIFI_PASSWORD"  # set to your network password
MFP_PORT = 4977

led = machine.Pin("LED", machine.Pin.OUT)

print("========================================")
print("  NOBD-DC-ONLINE SPI Bridge")
print("  GP4/5/7/8 SPI -> GP2040-CE GPIO 23-26")
print("========================================")

wlan = network.WLAN(network.STA_IF)
wlan.active(True)
wlan.connect(WIFI_SSID, WIFI_PASSWORD)

timeout = 30
while not wlan.isconnected() and timeout > 0:
    led.toggle()
    time.sleep(0.5)
    timeout -= 1

if not wlan.isconnected():
    print("WiFi FAILED — idle (physical buttons active on GP2040-CE)")
    led.value(0)
    while True:
        time.sleep(10)

ip = wlan.ifconfig()[0]
print(f"WiFi OK! IP: {ip}")
led.value(1)

# Disable CYW43 WiFi power management — eliminates 10-50ms latency spikes
wlan.config(pm=0xa11140)

# SPI1 master: GP11=MOSI, GP10=SCK, GP13=CS (manual), GP12=MISO
cs = machine.Pin(13, machine.Pin.OUT, value=1)  # CS high = idle
spi = machine.SPI(1, baudrate=1_000_000, polarity=0, phase=0,
                   sck=machine.Pin(10), mosi=machine.Pin(11), miso=machine.Pin(12))

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('0.0.0.0', MFP_PORT))
sock.setblocking(False)

# Flush any stale packets from socket buffer
flushed = 0
while True:
    try:
        sock.recvfrom(64)
        flushed += 1
    except OSError:
        break
if flushed:
    print(f"Flushed {flushed} stale packets")

print(f"UDP listening on {ip}:{MFP_PORT}")
print("=== READY ===")

packets = 0
spi_frames = 0
last_stat = time.ticks_ms()

while True:
    try:
        data, addr = sock.recvfrom(64)
        if len(data) >= 12:
            cs.value(0)
            spi.write(data[0:12])
            cs.value(1)
            spi_frames += 1
            packets += 1
    except OSError:
        pass

    now = time.ticks_ms()
    if time.ticks_diff(now, last_stat) >= 5000:
        print(f"[{now//1000}s] UDP:{packets} SPI:{spi_frames} WiFi:{'OK' if wlan.isconnected() else 'LOST'}")
        last_stat = now
