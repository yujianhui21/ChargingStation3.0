#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# espota_resume.py - 断点续传 OTA 上传工具
#
# 配合固件端断点续传 OTA 服务 (src/ota_service.cpp, TCP 3232) 使用。
# WiFi 信号不稳导致传输中断时，自动从上次已写入偏移处继续，直到完整传完。
#
# 用法:
#   python scripts/espota_resume.py -i <设备IP> -f <firmware.bin> [-p 3232]
#
# 协议（TCP，电脑端主动连接）:
#   电脑→设备: "S <总字节数> <固件MD5>\n"      请求开始 / 查询进度
#   设备→电脑: "O <已写入偏移>\n"              从该偏移续传（全新为 0）
#   电脑→设备: 固件二进制数据
#   设备→电脑: "A <已写入偏移>\n"              每块确认
#   设备→电脑: "OK\n" 或 "E <错误信息>\n"      结束
#
import socket
import sys
import os
import optparse
import hashlib
import time

DEFAULT_PORT = 3232
SOCKET_TIMEOUT = 20        # 单次收发超时 (秒)，超时即判定断线并续传
RETRY_DELAY = 2.0          # 重连续传间隔 (秒)
SEND_CHUNK = 8192          # 发送块大小


def read_line(sock):
    """读取一行（直到换行）"""
    data = b""
    while not data.endswith(b"\n"):
        chunk = sock.recv(1)
        if not chunk:
            raise ConnectionError("设备关闭连接")
        data += chunk
    return data.decode("utf-8", errors="replace").strip()


def upload(host, port, firmware):
    size = os.path.getsize(firmware)
    md5 = hashlib.md5(open(firmware, "rb").read()).hexdigest()
    print("固件: %s (%d 字节)" % (firmware, size))
    print("MD5 : %s" % md5)

    attempt = 0
    while True:
        attempt += 1
        sock = socket.create_connection((host, port), timeout=SOCKET_TIMEOUT)
        sock.settimeout(SOCKET_TIMEOUT)
        try:
            # 查询进度 / 开始上传：设备返回已写入偏移
            sock.sendall(("S %d %s\n" % (size, md5)).encode())
            line = read_line(sock)
            if not line.startswith("O "):
                print("设备响应异常: %s" % line)
                sock.close()
                return 1
            offset = int(line[2:])
            print("第 %d 次连接: 从偏移 %d (%d%%) 开始"
                  % (attempt, offset, offset * 100 // size))

            f = open(firmware, "rb")
            f.seek(offset)
            sent = offset
            last_ack = offset
            done = False
            try:
                while True:
                    chunk = f.read(SEND_CHUNK)
                    if not chunk:
                        break
                    sock.sendall(chunk)
                    sent += len(chunk)
                    # 读取确认，直到确认覆盖本次已发送的字节
                    while last_ack < sent:
                        ack = read_line(sock)
                        if ack.startswith("A "):
                            last_ack = int(ack[2:])
                        elif ack.startswith("OK"):
                            done = True
                            break
                        elif ack.startswith("E "):
                            print("设备错误: %s" % ack)
                            sock.close()
                            return 1
                    if done:
                        break
                    if sent // (256 * 1024) > (sent - len(chunk)) // (256 * 1024):
                        print("进度: %d%% (已确认 %d/%d)"
                              % (sent * 100 // size, last_ack, size))
            finally:
                f.close()

            # 发送完毕，等待最终结果
            if not done:
                while True:
                    line = read_line(sock)
                    if line.startswith("OK"):
                        done = True
                        break
                    if line.startswith("E "):
                        print("设备错误: %s" % line)
                        sock.close()
                        return 1
        except (socket.timeout, ConnectionError, OSError) as e:
            print("连接中断: %s" % e)
            sock.close()
            # 稍候让设备保存进度，再重连续传
            time.sleep(RETRY_DELAY)
            continue
        sock.close()

        if done:
            print("OTA 上传成功!")
            return 0
        return 1


def main(args):
    parser = optparse.OptionParser(
        usage="%prog -i <设备IP> -f <firmware.bin>",
        description="断点续传 OTA 上传工具 (配合固件端 ota_service.cpp 使用)"
    )
    parser.add_option("-i", "--ip", dest="esp_ip", action="store",
                      help="设备 IP 地址", default=None)
    parser.add_option("-f", "--file", dest="image", action="store",
                      help="固件文件 (firmware.bin)", default=None)
    parser.add_option("-p", "--port", dest="port", type="int",
                      help="设备 OTA 端口 (默认 3232)", default=DEFAULT_PORT)
    (options, args) = parser.parse_args(args)

    if not options.esp_ip or not options.image:
        parser.print_help()
        return 1
    if not os.path.exists(options.image):
        print("固件文件不存在: %s" % options.image)
        return 1

    return upload(options.esp_ip, options.port, options.image)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
