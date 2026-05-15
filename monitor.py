#!/usr/bin/env python3
"""Simple serial monitor for BME680 debug output"""
import serial
import sys

PORT = '/dev/cu.usbmodem4401'  # Update this if your port differs
BAUD = 115200

try:
    ser = serial.Serial(PORT, BAUD, timeout=1)
    print(f'Listening to {PORT} at {BAUD} baud (Ctrl+C to stop)...\n')

    while True:
        line = ser.readline().decode('utf-8', errors='replace').strip()
        if line:
            print(line)

except serial.SerialException as e:
    print(f'Error opening serial port: {e}', file=sys.stderr)
    sys.exit(1)
except KeyboardInterrupt:
    print('\n\nStopped monitoring')
    ser.close()
