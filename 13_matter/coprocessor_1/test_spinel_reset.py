#!/usr/bin/env python3
"""Send a Spinel CMD_RESET over HDLC-lite to the RCP and print any response."""
import serial, time, sys

PORT = '/dev/cu.usbmodem11101'

def fcs16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0x8408
            else:
                crc >>= 1
    return crc ^ 0xFFFF

def hdlc_escape(data):
    out = bytearray()
    for b in data:
        if b == 0x7E:
            out += bytes([0x7D, 0x5E])
        elif b == 0x7D:
            out += bytes([0x7D, 0x5D])
        else:
            out.append(b)
    return bytes(out)

def hdlc_frame(payload):
    fcs = fcs16(payload)
    body = payload + bytes([fcs & 0xFF, (fcs >> 8) & 0xFF])
    return bytes([0x7E]) + hdlc_escape(body) + bytes([0x7E])

# Spinel: header=FLAG|IID0|TID0=0x80, CMD_RESET=1 (varint 0x01), RESET_STACK=2
spinel_payload = bytes([0x80, 0x01, 0x02])
frame = hdlc_frame(spinel_payload)
print("TX:", frame.hex())
sys.stdout.flush()

s = serial.Serial(PORT, 115200, timeout=0.2)
# raise DTR/RTS so CDC ACM line control sees a host
s.dtr = True
s.rts = False
s.reset_input_buffer()

# send a couple of reset frames spaced out (RCP may need USB settle time)
for i in range(3):
    s.write(frame)
    s.flush()
    time.sleep(0.3)

end = time.time() + 4
got = bytearray()
while time.time() < end:
    data = s.read(256)
    if data:
        got += data
        print("RX raw:", data.hex())
        sys.stdout.flush()
s.close()
print("RX total bytes:", len(got))
