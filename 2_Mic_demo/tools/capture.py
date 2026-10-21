#!/usr/bin/env python3
"""
XIAO nRF54LM20B Mic Demo -- PC-side recording receiver script

Receives the audio stream sent by the board over USB CDC serial (frame
protocol described in the header comment of firmware main.c) and wraps it
into a .wav file saved in this script's project directory.

Usage:
    python3 capture.py [serial_port] [baud_rate]
    python3                       # default /dev/cu.usbmodem1101 115200
    python3 /dev/cu.usbmodem1101

Flow:
    1. Open the serial port (assert DTR)
    2. Wait for the board to send "XIAOWAV1" + header
    3. Receive PCM stream until "XIAOWAV0"
    4. Write rec_<timestamp>.wav to the current directory
    Each press of the User Button on the board starts/stops a recording and
    produces one wav.

Dependency: pyserial (pio env ships it: ~/.platformio/penv/.zephyr-4.4.0/bin/python)
"""

import os
import sys
import time
import struct
import wave

try:
    import serial
except ImportError:
    sys.exit("pyserial missing: run with the pio env python, or pip install pyserial")

MAGIC_START = b"XIAOWAV1"   # Start: followed by a 4+2+2 byte header
MAGIC_STOP  = b"WAV0"        # Last 4 bytes of the stop prefix, for cross-block matching
MAGIC_STOP_FULL = b"XIAOWAV0"

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem1101"
BAUD = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
OUTDIR = os.getcwd()   # Project folder (the directory this script runs in)


def main():
    ser = serial.Serial(PORT, BAUD, timeout=0.2)
    ser.dtr = True
    print(f"Listening on {PORT} ... press the User Button on the board to start/stop recording. Ctrl+C to quit.")

    buf = bytearray()
    state = "wait_start"   # wait_start | recording
    pcm = bytearray()
    rate = bits = chan = 0

    try:
        while True:
            data = ser.read(2048)
            if data:
                buf += data
            elif not buf:
                continue

            if state == "wait_start":
                i = buf.find(MAGIC_START)
                if i < 0:
                    # Keep the last 8 bytes so the magic can't span across blocks
                    del buf[:-8]
                    continue
                # Header: magic(8) + rate(4) + bits(2) + chan(2)
                if len(buf) < i + 16:
                    buf = buf[i:]
                    continue
                rate, bits, chan = struct.unpack_from("<IHH", buf, i + 8)
                print(f"[recording started] {rate}Hz {bits}bit {chan}ch")
                buf = buf[i + 16:]
                pcm = bytearray()
                state = "recording"

            elif state == "recording":
                j = buf.find(MAGIC_STOP_FULL)
                if j < 0:
                    # Keep the last 8 bytes so the magic can't span blocks; append the rest to pcm
                    keep = min(8, len(buf))
                    pcm += buf[:-keep]
                    buf = buf[-keep:] if keep else bytearray()
                    continue
                # Finalize
                pcm += buf[:j]
                buf = buf[j + 8:]
                fname = os.path.join(OUTDIR, f"rec_{int(time.time())}.wav")
                with wave.open(fname, "wb") as w:
                    w.setnchannels(chan)
                    w.setsampwidth(bits // 8)
                    w.setframerate(rate)
                    w.writeframes(pcm)
                print(f"[recording stopped] saved {fname}  ({len(pcm)} bytes, {len(pcm)/rate/chan/(bits//8):.2f}s)")
                state = "wait_start"

    except KeyboardInterrupt:
        print("\nexiting.")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
