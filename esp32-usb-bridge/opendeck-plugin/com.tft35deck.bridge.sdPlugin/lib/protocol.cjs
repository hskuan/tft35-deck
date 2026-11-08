// TFT35 Deck wire protocol, JavaScript side. Mirrors include/deck_protocol.h
// in the bridge firmware: keep the constants in sync.
//
// Frame: 0xA5 | type | len (u16 LE) | payload | crc8 (poly 0x07 over type..payload)

const SYNC = 0xa5;
const MAX_PAYLOAD = 1024;

const T = {
  // TFT -> bridge -> host
  EV_HELLO: 0x01,
  EV_KEY: 0x02,
  EV_DIAL: 0x03,
  EV_DIAL_BUTTON: 0x04,
  EV_LOG: 0x05,
  EV_PONG: 0x06,
  // host <-> bridge
  HOST_HELLO: 0x40,
  HOST_INFO: 0x41,
  HOST_BYE: 0x42,
  // host -> bridge -> TFT
  CMD_PING: 0x80,
  CMD_STATUS: 0x81,
  CMD_BRIGHTNESS: 0x82,
  CMD_SET_KEY: 0x83,
  CMD_CLEAR_KEYS: 0x84,
  CMD_KEY_IMAGE: 0x85,
  CMD_KEY_IMAGE_RLE: 0x86,
};

const KEY = { RELEASED: 0, PRESSED: 1, CANCELLED: 2 };
const DIAL = { CLICK: 1, LONG: 2 };
const INFO = { TFT_ONLINE: 0x01, TFT_FRAMED: 0x02 };

function crc8(buf, start = 0, end = buf.length) {
  let crc = 0;
  for (let i = start; i < end; i++) {
    crc ^= buf[i];
    for (let b = 0; b < 8; b++) crc = crc & 0x80 ? ((crc << 1) ^ 0x07) & 0xff : (crc << 1) & 0xff;
  }
  return crc;
}

function encode(type, payload = Buffer.alloc(0)) {
  if (payload.length > MAX_PAYLOAD) throw new Error(`payload too large: ${payload.length}`);
  const out = Buffer.alloc(payload.length + 5);
  out[0] = SYNC;
  out[1] = type;
  out.writeUInt16LE(payload.length, 2);
  payload.copy(out, 4);
  out[4 + payload.length] = crc8(out, 1, 4 + payload.length);
  return out;
}

// Incremental parser: push() bytes, get complete valid frames back.
class Parser {
  constructor() {
    this.buf = Buffer.alloc(0);
    this.errors = 0;
  }

  push(chunk) {
    this.buf = Buffer.concat([this.buf, chunk]);
    const frames = [];
    for (;;) {
      const start = this.buf.indexOf(SYNC);
      if (start < 0) {
        this.buf = Buffer.alloc(0);
        break;
      }
      if (start) this.buf = this.buf.subarray(start);
      if (this.buf.length < 5) break;
      const len = this.buf.readUInt16LE(2);
      if (len > MAX_PAYLOAD) {
        this.errors++;
        this.buf = this.buf.subarray(1);
        continue;
      }
      if (this.buf.length < len + 5) break;
      if (crc8(this.buf, 1, 4 + len) === this.buf[4 + len]) {
        frames.push({ type: this.buf[1], payload: Buffer.from(this.buf.subarray(4, 4 + len)) });
        this.buf = this.buf.subarray(len + 5);
      } else {
        this.errors++;
        this.buf = this.buf.subarray(1);  // resync
      }
    }
    return frames;
  }
}

module.exports = { T, KEY, DIAL, INFO, MAX_PAYLOAD, crc8, encode, Parser };
