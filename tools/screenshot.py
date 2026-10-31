"""ถ่ายภาพหน้าจอ CYD ผ่าน Serial แล้วบันทึกเป็น PNG (ไม่ต้องติดตั้งไลบรารีเพิ่ม ใช้แค่ pyserial ที่มากับ PlatformIO)

วิธีใช้ (ปิด Serial monitor ก่อน):
    ~/.platformio/penv/Scripts/python.exe tools/screenshot.py COM7 shot.png [คำสั่งก่อนถ่าย เช่น "" ]

CYD ต้องรันเฟิร์มแวร์ที่มีคำสั่ง S (ส่งไบต์ RGB565 ดิบ 320x240 ทีละแถว)
"""
import struct
import sys
import time
import zlib

import serial


def save_png(path, w, h, rgb565):
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter: none
        row = rgb565[y * w * 2:(y + 1) * w * 2]
        for i in range(0, len(row), 2):
            v = (row[i] << 8) | row[i + 1]
            r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
            raw += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM7"
    out = sys.argv[2] if len(sys.argv) > 2 else "screenshot.png"
    wait = float(sys.argv[3]) if len(sys.argv) > 3 else 0.3   # รอกี่วินาทีก่อนถ่าย (การเปิดพอร์ตมักรีเซ็ตบอร์ด)
    before = sys.argv[4] if len(sys.argv) > 4 else ""         # คำสั่งก่อนถ่าย เช่น "#tab 2"
    s = serial.Serial(port, 115200, timeout=0.2)
    s.dtr = False  # ไม่รีเซ็ตบอร์ดตอนเปิดพอร์ต (ถ้าไดรเวอร์ยอม)
    s.rts = False
    time.sleep(wait)
    if before:
        s.write(before.encode() + b"\n")
        time.sleep(1.0)
    s.reset_input_buffer()
    s.write(b"S")
    buf = b""
    t0 = time.time()
    while b"#SCREENSHOT" not in buf:
        buf += s.read(4096)
        if time.time() - t0 > 5:
            sys.exit("no #SCREENSHOT header (wrong port? firmware without S command?)")
    head, rest = buf.split(b"#SCREENSHOT", 1)
    line, data = rest.split(b"\n", 1)
    w, h = (int(x) for x in line.split())
    need = w * h * 2
    while len(data) < need:
        d = s.read(8192)
        if not d and time.time() - t0 > 40:
            sys.exit("timeout, got %d of %d bytes" % (len(data), need))
        data += d
    save_png(out, w, h, data[:need])
    print("saved", out, w, "x", h, "in %.1f s" % (time.time() - t0))


if __name__ == "__main__":
    main()
