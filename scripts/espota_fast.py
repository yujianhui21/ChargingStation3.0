#!/usr/bin/env python
#
# espota_fast.py - 快速 OTA 上传工具 (chunk=8192, 比原版快 ~8x)
# 用法: python scripts/espota_fast.py -i <设备IP> -f <firmware.bin> [-I <本机IP>]
#
# 基于 PlatformIO espota.py, chunk 从 1024 增大到 8192 以降低 ACK 往返开销

from __future__ import print_function
import socket
import sys
import os
import optparse
import logging
import hashlib
import random

FLASH = 0
SPIFFS = 100
AUTH = 200
PROGRESS = False
TIMEOUT = 10

def update_progress(progress):
  if (PROGRESS):
    barLength = 60
    status = ""
    if progress >= 1:
      progress = 1
      status = "Done...\r\n"
    block = int(round(barLength*progress))
    text = "\rUploading: [{0}] {1}% {2}".format("="*block + " "*(barLength-block), int(progress*100), status)
    sys.stderr.write(text)
    sys.stderr.flush()
  else:
    pass

def serve(remoteAddr, localAddr, remotePort, localPort, password, filename, command = FLASH):
  sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
  server_address = (localAddr, localPort)
  logging.info('Starting on %s:%s', str(server_address[0]), str(server_address[1]))
  try:
    sock.bind(server_address)
    sock.listen(1)
  except:
    logging.error("Listen Failed - try -I <本机局域网IP> to bind a specific IP")
    return 1

  content_size = os.path.getsize(filename)
  f = open(filename,'rb')
  file_md5 = hashlib.md5(f.read()).hexdigest()
  f.close()
  logging.info('Upload size: %d', content_size)
  message = '%d %d %d %s\n' % (command, localPort, content_size, file_md5)

  # send invitation via UDP
  inv_trys = 0
  data = ''
  msg = 'Sending invitation to %s ' % (remoteAddr)
  sys.stderr.write(msg)
  sys.stderr.flush()
  while (inv_trys < 10):
    inv_trys += 1
    sock2 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    remote_address = (remoteAddr, int(remotePort))
    try:
      sent = sock2.sendto(message.encode(), remote_address)
    except:
      sys.stderr.write('failed\n')
      sock2.close()
      logging.error('Host %s Not Found', remoteAddr)
      return 1
    sock2.settimeout(TIMEOUT)
    try:
      data = sock2.recv(37).decode()
      break
    except:
      sys.stderr.write('.')
      sys.stderr.flush()
      sock2.close()
  sys.stderr.write('\n')
  if (inv_trys == 10):
    logging.error('No response from the ESP')
    return 1
  if (data != "OK"):
    if(data.startswith('AUTH')):
      nonce = data.split()[1]
      cnonce_text = '%s%u%s%s' % (filename, content_size, file_md5, remoteAddr)
      cnonce = hashlib.md5(cnonce_text.encode()).hexdigest()
      passmd5 = hashlib.md5(password.encode()).hexdigest()
      result_text = '%s:%s:%s' % (passmd5 ,nonce, cnonce)
      result = hashlib.md5(result_text.encode()).hexdigest()
      sys.stderr.write('Authenticating...')
      message = '%d %s %s\n' % (AUTH, cnonce, result)
      sock2.sendto(message.encode(), remote_address)
      sock2.settimeout(10)
      try:
        data = sock2.recv(32).decode()
      except:
        logging.error('No Answer to our Authentication')
        sock2.close()
        return 1
      if (data != "OK"):
        logging.error('%s', data)
        sock2.close()
        return 1
    else:
      logging.error('Bad Answer: %s', data)
      sock2.close()
      return 1
  sock2.close()

  logging.info('Waiting for device...')
  try:
    sock.settimeout(10)
    connection, client_address = sock.accept()
    sock.settimeout(None)
    connection.settimeout(None)
  except:
    logging.error('No response from device')
    sock.close()
    return 1

  try:
    f = open(filename, "rb")
    if (PROGRESS):
      update_progress(0)
    else:
      sys.stderr.write('Uploading')
      sys.stderr.flush()
    offset = 0
    while True:
      chunk = f.read(4096)   # fast chunk: 4KB (original: 1KB)
      if not chunk: break
      offset += len(chunk)
      if (PROGRESS): update_progress(offset/float(content_size))
      connection.settimeout(10)
      try:
        connection.sendall(chunk)
        res = connection.recv(10)
        lastResponseContainedOK = 'OK' in res.decode()
      except:
        sys.stderr.write('\n')
        logging.error('Error Uploading')
        connection.close()
        f.close()
        sock.close()
        return 1

    if lastResponseContainedOK:
      logging.info('Success')
      connection.close()
      f.close()
      sock.close()
      return 0

    sys.stderr.write('\n')
    logging.info('Waiting for result...')
    try:
      count = 0
      while True:
        count=count+1
        connection.settimeout(60)
        data = connection.recv(32).decode()
        logging.info('Result: %s' ,data)
        if "OK" in data:
          logging.info('Success')
          connection.close()
          f.close()
          sock.close()
          return 0
        if count == 5:
          logging.error('Error response from device')
          connection.close()
          f.close()
          sock.close()
          return 1
    except:
      logging.error('No Result!')
      connection.close()
      f.close()
      sock.close()
      return 1

  finally:
    connection.close()
    f.close()

  sock.close()
  return 1


def parser(unparsed_args):
  parser = optparse.OptionParser(
    usage = "%prog [options]",
    description = "Fast OTA upload (8KB chunk) for ESP32 ArduinoOTA"
  )
  group = optparse.OptionGroup(parser, "Destination")
  group.add_option("-i", "--ip", dest="esp_ip", action="store", help="ESP32 IP Address.", default=False)
  group.add_option("-I", "--host_ip", dest="host_ip", action="store", help="Host IP Address.", default="0.0.0.0")
  group.add_option("-p", "--port", dest="esp_port", type="int", help="ESP32 ota Port. Default 3232", default=3232)
  group.add_option("-P", "--host_port", dest="host_port", type="int", help="Host server ota Port. Default random 10000-60000", default=random.randint(10000,60000))
  parser.add_option_group(group)

  group = optparse.OptionGroup(parser, "Authentication")
  group.add_option("-a", "--auth", dest="auth", help="Set authentication password.", action="store", default="")
  parser.add_option_group(group)

  group = optparse.OptionGroup(parser, "Image")
  group.add_option("-f", "--file", dest="image", help="Image file.", metavar="FILE", default=None)
  group.add_option("-s", "--spiffs", dest="spiffs", action="store_true", help="SPIFFS image", default=False)
  parser.add_option_group(group)

  group = optparse.OptionGroup(parser, "Output")
  group.add_option("-d", "--debug", dest="debug", help="Show debug output.", action="store_true", default=False)
  group.add_option("-r", "--progress", dest="progress", help="Show progress output.", action="store_true", default=False)
  group.add_option("-t", "--timeout", dest="timeout", type="int", help="Timeout for invitation", default=10)
  parser.add_option_group(group)

  (options, args) = parser.parse_args(unparsed_args)
  return options


def main(args):
  options = parser(args)
  loglevel = logging.WARNING
  if (options.debug): loglevel = logging.DEBUG
  logging.basicConfig(level = loglevel, format = '%(asctime)-8s [%(levelname)s]: %(message)s', datefmt = '%H:%M:%S')
  logging.debug("Options: %s", str(options))

  global PROGRESS
  PROGRESS = options.progress
  global TIMEOUT
  TIMEOUT = options.timeout

  if (not options.esp_ip or not options.image):
    logging.critical("Not enough arguments. Use -i <IP> -f <firmware.bin>")
    return 1

  command = FLASH
  if (options.spiffs): command = SPIFFS
  return serve(options.esp_ip, options.host_ip, options.esp_port, options.host_port, options.auth, options.image, command)


if __name__ == '__main__':
  sys.exit(main(sys.argv))
