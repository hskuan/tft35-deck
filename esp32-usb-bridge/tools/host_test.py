#!/usr/bin/env python3
"""Stand-in for the OpenDeck plugin: attach to the bridge's USB serial port,
customise the TFT's keys and print deck events.

    pip install pyserial
    python host_test.py COM7          (Windows; see Device Manager > Ports)
    python host_test.py /dev/ttyACM0  (Linux)

While this runs the TFT is in host mode:
  - on attach, all 15 keys get a colour and a label
  - pressing a key gives it the next colour
  - clicking the knob sends a test image (80x80 gradient) to key 0
  - turning the knob sets the TFT's backlight brightness
Ctrl+C detaches; the TFT goes back to its own pages and the bridge to
keyboard mode.
"""
import struct
import sys
import time

import serial

SYNC = 0xA5
EV_KEY, EV_DIAL, EV_DIAL_BUTTON = 0x02, 0x03, 0x04
HOST_HELLO, HOST_INFO, HOST_BYE = 0x40, 0x41, 0x42
CMD_BRIGHTNESS, CMD_SET_KEY, CMD_KEY_IMAGE, CMD_SOUND = 0x82, 0x83, 0x85, 0x87

KEY_STATES = {0: "released", 1: "pressed", 2: "cancelled"}
DIAL_KINDS = {1: "click", 2: "long press"}
IMG = 80             # host key images are 80x80 RGB565
IMG_CHUNK = 400      # pixels per KEY_IMAGE frame (payload <= 1024 bytes)


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


COLOURS = [rgb565(*c) for c in [(200, 40, 40), (40, 150, 70), (40, 90, 170), (190, 120, 20),
                                 (120, 50, 160), (30, 130, 140)]]


def crc8(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def frame(ftype, payload=b""):
    body = bytes([ftype]) + struct.pack("<H", len(payload)) + payload
    return bytes([SYNC]) + body + bytes([crc8(body)])


def set_key(port, key, bg, label, fg=0xFFFF):
    port.write(frame(CMD_SET_KEY, struct.pack("<BHH", key, bg, fg) + label.encode()))


def send_image(port, key):
    """80x80 test gradient: red left->right, blue top->bottom."""
    pixels = bytearray()
    for y in range(IMG):
        for x in range(IMG):
            pixels += struct.pack("<H", rgb565(x * 255 // (IMG - 1), 60, y * 255 // (IMG - 1)))
    for off in range(0, IMG * IMG, IMG_CHUNK):
        chunk = pixels[off * 2:(off + IMG_CHUNK) * 2]
        port.write(frame(CMD_KEY_IMAGE, struct.pack("<BH", key, off) + chunk))


def frames(port):
    """Yield (type, payload) for every valid frame; (None, None) when idle."""
    buf = bytearray()
    while True:
        buf += port.read(port.in_waiting or 1)
        while True:
            start = buf.find(SYNC)
            if start < 0:
                buf.clear()
                break
            del buf[:start]
            if len(buf) < 5:
                break
            length = buf[2] | buf[3] << 8
            if len(buf) < 5 + length:
                break
            body, crc = bytes(buf[1:4 + length]), buf[4 + length]
            if crc == crc8(body):
                yield body[0], body[3:]
                del buf[:5 + length]
            else:
                del buf[:1]  # resync
        yield None, None


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    port = serial.Serial(sys.argv[1], 115200, timeout=0.2)
    print(f"attached to {sys.argv[1]}, Ctrl+C to quit")
    last_hello = 0.0
    colour_of = {}
    brightness = 100
    try:
        for ftype, payload in frames(port):
            now = time.monotonic()
            if now - last_hello >= 1.0:
                port.write(frame(HOST_HELLO))
                last_hello = now
            if ftype is None:
                continue

            if ftype == HOST_INFO:
                proto, flags, cols, rows, dials = payload[:5]
                fw = payload[5:].decode(errors="replace")
                print(f"bridge: proto {proto}, {cols}x{rows} keys, {dials} dial, "
                      f"tft {'online' if flags & 1 else 'offline'}"
                      f"{', protocol' if flags & 2 else ', text log only'}{', fw ' + fw if fw else ''}")
                time.sleep(0.3)  # let the TFT switch to host mode first
                for key in range(cols * rows):
                    colour_of[key] = key % len(COLOURS)
                    set_key(port, key, COLOURS[colour_of[key]], f"Key\n{key + 1}")
            elif ftype == EV_KEY:
                page, key, state = payload[:3]
                print(f"key {key:2} {KEY_STATES.get(state, state)} (page {page})")
                if state == 0:  # released: the press counts
                    colour_of[key] = (colour_of.get(key, 0) + 1) % len(COLOURS)
                    set_key(port, key, COLOURS[colour_of[key]], f"Key\n{key + 1}")
            elif ftype == EV_DIAL:
                page, dial, ticks = struct.unpack("<BBb", payload[:3])
                brightness = max(5, min(100, brightness + ticks * 5))
                port.write(frame(CMD_BRIGHTNESS, bytes([brightness])))
                print(f"dial {ticks:+} (page {page}) -> brightness {brightness}%")
            elif ftype == EV_DIAL_BUTTON:
                page, dial, kind = payload[:3]
                print(f"dial {DIAL_KINDS.get(kind, kind)} (page {page})")
                if kind == 1:
                    print("sending test image to key 0 ...")
                    send_image(port, 0)
            else:
                print(f"frame 0x{ftype:02X}: {payload.hex()}")
    except KeyboardInterrupt:
        port.write(frame(HOST_BYE))
        print("detached")


if __name__ == "__main__":
    main()
