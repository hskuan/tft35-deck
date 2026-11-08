// Connection to the TFT35 Deck USB bridge over its USB serial port.
//
//  - Finds the bridge among serial ports with a known USB vendor ID (Espressif
//    ESP32-S3, Raspberry Pi Pico) and confirms it with the HOST_HELLO ->
//    HOST_INFO handshake. Unknown ports are never probed unless configured.
//  - Keeps the host attached with HOST_HELLO every second.
//  - Emits: connected(info), info(info), key({key, state}), dial({dial, ticks}),
//    dialButton({dial, kind}), disconnected().
//  - Paces writes: key images are sent one at a time (only the newest image
//    per key is kept), with keepalives slipped in between, because the
//    bridge -> TFT UART needs up to ~35 ms per 80x80 image at 3.75 Mbaud
//    (less with run-length encoding, used whenever it is smaller).
const { EventEmitter } = require('events');
const { SerialPort } = require('serialport');
const { T, INFO, encode, Parser } = require('./protocol.cjs');
const { KEY_IMG, rleRuns } = require('./image.cjs');

const KNOWN_VIDS = ['303a', '2e8a'];  // Espressif, Raspberry Pi
const PROBE_TIMEOUT_MS = 1500;
const HELLO_INTERVAL_MS = 1000;
const SCAN_INTERVAL_MS = 3000;
const IMG_CHUNK = 400;  // pixels per KEY_IMAGE frame (payload 803 bytes)
const RLE_CHUNK = 300;  // runs per KEY_IMAGE_RLE frame (payload 903 bytes)

function parseInfo(p) {
  return {
    proto: p[0],
    tftOnline: !!(p[1] & INFO.TFT_ONLINE),
    tftFramed: !!(p[1] & INFO.TFT_FRAMED),
    columns: p[2],
    rows: p[3],
    dials: p[4],
    firmware: p.subarray(5).toString('utf8'),
  };
}

class Deck extends EventEmitter {
  constructor({ log = console.log, port = null, rleTolerance = 1 } = {}) {
    super();
    this.rleTolerance = rleTolerance;
    this.stats = { images: 0, bytes: 0, rawBytes: 0 };
    this.log = log;
    this.forcedPath = port;
    this.port = null;
    this.portInfo = null;
    this.parser = new Parser();
    this.control = [];             // small frames, sent first
    this.images = new Map();       // key -> RGB565 buffer, or null to blank the key
    this.writing = false;
    this.timers = [];
  }

  start() {
    this.scan();
    this.timers.push(setInterval(() => this.scan(), SCAN_INTERVAL_MS));
    this.timers.push(setInterval(() => this.queue(encode(T.HOST_HELLO)), HELLO_INTERVAL_MS));
  }

  stop() {
    this.timers.forEach(clearInterval);
    const port = this.port;
    if (!port) return;
    this.port = null;  // stops the writer loop
    this.control = [];
    this.images.clear();
    port.removeAllListeners();
    port.on('error', () => {});
    try {
      port.write(encode(T.HOST_BYE), () => port.drain(() => port.close(() => {})));
    } catch (e) { /* closing anyway */ }
  }

  get connected() { return !!this.port; }

  // ---- discovery ------------------------------------------------------------

  async scan() {
    if (this.port || this.scanning) return;
    this.scanning = true;
    try {
      const ports = await SerialPort.list();
      const candidates = this.forcedPath
        ? ports.filter((p) => p.path === this.forcedPath).concat(
            ports.some((p) => p.path === this.forcedPath) ? [] : [{ path: this.forcedPath }])
        : ports.filter((p) => KNOWN_VIDS.includes((p.vendorId || '').toLowerCase()));
      for (const info of candidates) {
        if (await this.probe(info)) break;
      }
    } catch (e) {
      this.log(`scan failed: ${e.message}`);
    } finally {
      this.scanning = false;
    }
  }

  probe(info) {
    return new Promise((resolve) => {
      const port = new SerialPort({ path: info.path, baudRate: 115200, autoOpen: false });
      const parser = new Parser();
      let done = false;
      const finish = (ok, hostInfo) => {
        if (done) return;
        done = true;
        clearTimeout(timer);
        port.removeListener('data', onData);
        if (!ok) {
          if (port.isOpen) port.close(() => {});
          return resolve(false);
        }
        this.attach(port, info, hostInfo);
        resolve(true);
      };
      const onData = (chunk) => {
        for (const f of parser.push(chunk)) {
          if (f.type === T.HOST_INFO) return finish(true, parseInfo(f.payload));
        }
      };
      const timer = setTimeout(() => finish(false), PROBE_TIMEOUT_MS);
      port.open((err) => {
        if (err) return finish(false);
        port.on('data', onData);
        port.write(encode(T.HOST_HELLO));
      });
    });
  }

  attach(port, portInfo, info) {
    this.port = port;
    this.portInfo = portInfo;
    this.parser = new Parser();
    this.info = info;
    port.on('data', (chunk) => this.onData(chunk));
    port.on('close', () => this.detach('port closed'));
    port.on('error', (e) => this.detach(`port error: ${e.message}`));
    this.log(`bridge on ${portInfo.path}: ${info.columns}x${info.rows} keys, ${info.dials} dial(s), ` +
      `tft ${info.tftOnline ? 'online' : 'offline'}${info.tftFramed ? '' : ' (no protocol)'}` +
      `${info.firmware ? `, fw '${info.firmware}'` : ''}`);
    this.emit('connected', { ...info, serial: portInfo.serialNumber || portInfo.path });
  }

  detach(reason) {
    if (!this.port) return;
    this.log(`bridge disconnected: ${reason}`);
    this.port.removeAllListeners();
    if (this.port.isOpen) this.port.close(() => {});
    this.port = null;
    this.control = [];
    this.images.clear();
    this.writing = false;
    this.emit('disconnected');
  }

  // ---- input -------------------------------------------------------------------

  onData(chunk) {
    for (const { type, payload: p } of this.parser.push(chunk)) {
      switch (type) {
        case T.HOST_INFO:
          this.info = parseInfo(p);
          this.emit('info', this.info);
          break;
        case T.EV_KEY:
          this.emit('key', { page: p[0], key: p[1], state: p[2] });
          break;
        case T.EV_DIAL:
          this.emit('dial', { page: p[0], dial: p[1], ticks: p.readInt8(2) });
          break;
        case T.EV_DIAL_BUTTON:
          this.emit('dialButton', { page: p[0], dial: p[1], kind: p[2] });
          break;
        default:
          break;
      }
    }
  }

  // ---- output ------------------------------------------------------------------

  setBrightness(percent) {
    this.queue(encode(T.CMD_BRIGHTNESS, Buffer.from([Math.max(0, Math.min(100, percent))])));
  }

  setSound(on) {
    this.queue(encode(T.CMD_SOUND, Buffer.from([on ? 1 : 0])));
  }

  clearAll() {
    this.images.clear();
    this.queue(encode(T.CMD_CLEAR_KEYS));
  }

  setImage(key, rgb565) {  // rgb565: KEY_IMG x KEY_IMG pixels, or null to blank the key
    if (!this.port) return;
    this.images.delete(key);  // re-insert so keys are sent in arrival order
    this.images.set(key, rgb565);
    this.pump();
  }

  queue(frame) {
    if (!this.port) return;
    this.control.push(frame);
    this.pump();
  }

  async pump() {
    if (this.writing || !this.port) return;
    this.writing = true;
    try {
      while (this.port) {
        if (this.control.length) {
          await this.write(Buffer.concat(this.control.splice(0)));
        } else if (this.images.size) {
          const [key, pixels] = this.images.entries().next().value;
          this.images.delete(key);
          for (const f of this.imageFrames(key, pixels)) {
            await this.write(f);
            if (this.control.length) await this.write(Buffer.concat(this.control.splice(0)));
          }
        } else {
          break;
        }
      }
    } catch (e) {
      this.log(`write failed: ${e.message}`);
    } finally {
      this.writing = false;
    }
  }

  *imageFrames(key, pixels) {
    if (!pixels) {  // blank key: empty dark label
      const p = Buffer.alloc(5);
      p[0] = key;
      p.writeUInt16LE(0x10a3, 1);  // bg: the TFT's empty-key grey, rgb565(22, 22, 28)
      p.writeUInt16LE(0xffff, 3);  // fg
      yield encode(T.CMD_SET_KEY, p);
      return;
    }
    const total = KEY_IMG * KEY_IMG;
    const runs = rleRuns(pixels, this.rleTolerance);
    this.stats.images++;
    this.stats.rawBytes += total * 2;
    if (runs.length * 3 < total * 2) {  // run-length encoding is smaller
      this.stats.bytes += runs.length * 3;
      for (let r = 0; r < runs.length; r += RLE_CHUNK) {
        const chunk = runs.slice(r, r + RLE_CHUNK);
        const p = Buffer.alloc(3 + chunk.length * 3);
        p[0] = key;
        p.writeUInt16LE(chunk[0].offset, 1);
        chunk.forEach((run, j) => {
          p[3 + j * 3] = run.count;
          p.writeUInt16LE(run.color, 4 + j * 3);
        });
        yield encode(T.CMD_KEY_IMAGE_RLE, p);
      }
      return;
    }
    this.stats.bytes += total * 2;
    for (let off = 0; off < total; off += IMG_CHUNK) {
      const n = Math.min(IMG_CHUNK, total - off);
      const p = Buffer.alloc(3 + n * 2);
      p[0] = key;
      p.writeUInt16LE(off, 1);
      pixels.copy(p, 3, off * 2, (off + n) * 2);
      yield encode(T.CMD_KEY_IMAGE, p);
    }
  }

  write(buf) {
    return new Promise((resolve, reject) => {
      const port = this.port;
      if (!port) return resolve();
      port.write(buf, (err) => {
        if (err) return reject(err);
        port.drain((err2) => (err2 ? reject(err2) : resolve()));
      });
    });
  }
}

module.exports = { Deck, parseInfo };
