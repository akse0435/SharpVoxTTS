#!/usr/bin/env python3
# Repeatedly speak a phrase on the RP2040 so a speaker can be tested by ear.
# Uses the SHVX_OUTPUT_I2S firmware: the board plays each utterance on the DAC
# while still streaming the PCM back, which this script reads and discards.
#
# Usage: speaker_loop.py ["text"] [port] [gap_seconds]
# Defaults: text "Testing, one two three.", port /dev/ttyACM0, gap 0.7
# Stop with Ctrl-C.

import sys
import time
import serial

text = sys.argv[1] if len(sys.argv) > 1 else "Testing, one two three."
port = sys.argv[2] if len(sys.argv) > 2 else "/dev/ttyACM0"
gap  = float(sys.argv[3]) if len(sys.argv) > 3 else 0.7

ser = serial.Serial(port, 115200, timeout=10)
ser.dtr = True

def read_line():
    line = ser.readline()
    if not line:
        raise TimeoutError("serial read timed out waiting for a line")
    return line.rstrip(b"\r\n").decode("ascii", "replace")

def read_exact(n):
    got = 0
    while got < n:
        part = ser.read(n - got)
        if not part:
            raise TimeoutError(f"serial read timed out ({got}/{n} bytes)")
        got += len(part)

def speak_once():
    ser.reset_input_buffer()
    ser.write((text + "\n").encode("ascii"))
    ser.flush()
    samples = 0
    while True:
        line = read_line()
        if line.startswith("SHVX CHUNK "):
            n = int(line.split()[2])
            read_exact(n * 2)
            samples += n
        elif line == "SHVX END":
            return samples

print(f"port {port}: looping {text!r} (Ctrl-C to stop)", file=sys.stderr)
i = 0
try:
    while True:
        i += 1
        n = speak_once()
        print(f"  [{i}] {n} samples ({n / 22050:.2f} s)", file=sys.stderr)
        time.sleep(gap)
except KeyboardInterrupt:
    print("\nstopped", file=sys.stderr)
finally:
    ser.close()
