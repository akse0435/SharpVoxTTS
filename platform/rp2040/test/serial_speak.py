#!/usr/bin/env python3
# Host reader for the SharpVox RP2040 USB CDC build (SHVX_TRANSPORT_USB=1).
# Opens the board's serial port, sends a line of text, collects the streamed
# PCM and writes a WAV.
#
# Usage: serial_speak.py "text to speak" [port] [out.wav]
# Defaults: port /dev/ttyACM0, out output.wav
#
# Protocol produced by main.cpp:
#   "SHVX UART OK\r\n"
#   "SHVX READY\r\n"
#   "SHVX BEGIN <rate>\r\n"
#   "SHVX CHUNK <n>\r\n" + n*2 bytes raw int16 PCM   (repeated)
#   "SHVX END\r\n"

import sys
import struct
import wave
import serial

text = sys.argv[1] if len(sys.argv) > 1 else "Hello world."
port = sys.argv[2] if len(sys.argv) > 2 else "/dev/ttyACM0"
out  = sys.argv[3] if len(sys.argv) > 3 else "output.wav"

# Baud is ignored by USB CDC; DTR assert lets the firmware know a host is here.
ser = serial.Serial(port, 115200, timeout=10)
ser.dtr = True

def read_line():
    line = ser.readline()
    if not line:
        raise TimeoutError("serial read timed out waiting for a line")
    return line.rstrip(b"\r\n").decode("ascii", "replace")

def read_exact(n):
    buf = bytearray()
    while len(buf) < n:
        part = ser.read(n - len(buf))
        if not part:
            raise TimeoutError(f"serial read timed out ({len(buf)}/{n} bytes)")
        buf += part
    return bytes(buf)

# Drain the boot banner if present, then send the text. If the board has
# already booted past the banner it sits in its read loop, so send regardless.
print(f"port {port}: sending {text!r}", file=sys.stderr)
ser.reset_input_buffer()
ser.write((text + "\n").encode("ascii"))
ser.flush()

rate = 22050
pcm = bytearray()
while True:
    line = read_line()
    if line.startswith("SHVX BEGIN "):
        rate = int(line.split()[2])
        print(f"stream started: {rate} Hz", file=sys.stderr)
    elif line.startswith("SHVX CHUNK "):
        n = int(line.split()[2])
        pcm += read_exact(n * 2)
    elif line == "SHVX END":
        break
    elif line:
        print(f"  [{line}]", file=sys.stderr)

ser.close()

samples = len(pcm) // 2
print(f"audio: {rate} Hz, {samples} samples ({samples / rate:.2f} s)", file=sys.stderr)

with wave.open(out, "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(rate)
    w.writeframes(bytes(pcm))

print(f"written {out} - {len(pcm) / 1024:.1f} KB", file=sys.stderr)
print("PASS")
