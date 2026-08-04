#!/usr/bin/env python
# -*- coding: utf-8 -*-
# 抓取设备串口日志，带时间戳，写入文件
import serial, time, sys

port = "COM6"
baud = 115200
out = sys.argv[1] if len(sys.argv) > 1 else "serial_capture.log"
duration = float(sys.argv[2]) if len(sys.argv) > 2 else 90

ser = serial.Serial(port, baud, timeout=0.1)
ser.reset_input_buffer()
t0 = time.time()
with open(out, "w", encoding="utf-8") as f:
    while time.time() - t0 < duration:
        b = ser.read(1024)
        if b:
            ts = time.time() - t0
            text = b.decode("utf-8", errors="replace")
            for line in text.splitlines():
                if line.strip():
                    f.write("[%7.2f] %s\n" % (ts, line))
            f.flush()
ser.close()
print("capture done ->", out)
