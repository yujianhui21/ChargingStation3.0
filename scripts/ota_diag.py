#!/usr/bin/env python
# 综合诊断: 读设备串口 + 同时触发 OTA 上传, 捕获设备端 OTA 错误
import serial, socket, threading, time, hashlib, os

FW = ".pio/build/esp32-s3-devkitc-1/firmware.bin"
HOST = "192.168.1.11"
DEV = "192.168.1.247"
PORT = 6666

serial_log = []
ser = serial.Serial("COM6", 115200, timeout=0.2)
ser.reset_input_buffer()

def read_serial():
    end = time.time() + 60
    while time.time() < end:
        b = ser.read(512)
        if b:
            serial_log.append(b.decode("utf-8", errors="replace"))
threading.Thread(target=read_serial, daemon=True).start()

md5 = hashlib.md5(open(FW, "rb").read()).hexdigest()
size = os.path.getsize(FW)
print("fw size:", size, "md5:", md5)

srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind((HOST, PORT)); srv.listen(1)
print("TCP server on", HOST, PORT)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((HOST, 0))
msg = "0 %d %d %s\n" % (PORT, size, md5)
sock.settimeout(5); sock.sendto(msg.encode(), (DEV, 3232))
try:
    print("UDP reply:", sock.recvfrom(37)[0])
except Exception as e:
    print("no UDP reply:", e)

srv.settimeout(10)
try:
    conn, addr = srv.accept()
    print("TCP connected:", addr)
    conn.settimeout(10)
    # 发送固件
    f = open(FW, "rb")
    total = 0
    while True:
        chunk = f.read(4096)
        if not chunk: break
        try:
            conn.sendall(chunk)
            res = conn.recv(10)
            total += len(chunk)
            if total % 65536 < 4096:
                print("progress:", round(total*100.0/size, 1), "%", "lastACK:", res[:6])
        except Exception as e:
            print("SEND FAIL at", round(total*100.0/size, 1), "%:", e)
            break
    f.close()
    conn.close()
except Exception as e:
    print("no TCP:", e)
srv.close()
time.sleep(2)
print("=== 设备串口日志 ===")
print("".join(serial_log) if serial_log else "(无)")
ser.close()
