// Offline checks: protocol framing and image conversion (no hardware needed).
//   npm test
const assert = require('assert');
const jpeg = require('jpeg-js');
const { T, encode, Parser, crc8 } = require('../lib/protocol.cjs');
const { keyImageFromDataUrl, KEY_IMG } = require('../lib/image.cjs');

// Known frame from the firmware tests: KEY page 0, key 4, pressed
assert.strictEqual(encode(T.EV_KEY, Buffer.from([0, 4, 1])).toString('hex'), 'a5020300000401a7');

// Parser: garbage, valid frame, corrupted frame, valid frame split across chunks
const good = encode(T.EV_KEY, Buffer.from([0, 4, 1]));
const bad = Buffer.from(encode(T.EV_DIAL, Buffer.from([0, 0, 2])));
bad[bad.length - 1] ^= 0xff;
const stream = Buffer.concat([Buffer.from('noise'), good, bad, good]);
const parser = new Parser();
const frames = [...parser.push(stream.subarray(0, 9)), ...parser.push(stream.subarray(9))];
assert.deepStrictEqual(frames.map((f) => f.payload.toString('hex')), ['000401', '000401']);
assert.strictEqual(parser.errors, 1);

// crc8 matches the C implementation's check value for "123456789" (CRC-8/SMBUS = 0xF4)
assert.strictEqual(crc8(Buffer.from('123456789')), 0xf4);

// Image: 144x144 JPEG, left half red, right half blue -> 80x80 RGB565
const W = 144;
const rgba = Buffer.alloc(W * W * 4);
for (let y = 0; y < W; y++) {
  for (let x = 0; x < W; x++) {
    const i = (y * W + x) * 4;
    rgba[i] = x < W / 2 ? 255 : 0;
    rgba[i + 2] = x < W / 2 ? 0 : 255;
    rgba[i + 3] = 255;
  }
}
const url = 'data:image/jpeg;base64,' + jpeg.encode({ data: rgba, width: W, height: W }, 95).data.toString('base64');
const px = keyImageFromDataUrl(url);
assert.strictEqual(px.length, KEY_IMG * KEY_IMG * 2);
const at = (x, y) => px.readUInt16LE((y * KEY_IMG + x) * 2);
const red = at(10, 40), blue = at(70, 40);
assert.ok(red >> 11 > 28 && (red & 0x1f) < 4, `left should be red, got 0x${red.toString(16)}`);
assert.ok((blue & 0x1f) > 28 && blue >> 11 < 4, `right should be blue, got 0x${blue.toString(16)}`);

assert.throws(() => keyImageFromDataUrl('data:image/png;base64,AAAA'), /unsupported/);

// RLE: decode the plugin's frames the way the TFT does (ui.cpp onKeyImage*)
const { Deck } = require('../lib/deck.cjs');
const { T: TT } = require('../lib/protocol.cjs');

function tftDecode(frames) {
  const img = new Uint16Array(KEY_IMG * KEY_IMG);
  let next = 0;
  for (const f of new Parser().push(Buffer.concat(frames))) {
    const off = f.payload.readUInt16LE(1);
    assert.strictEqual(off, next, 'frames must continue where the previous one ended');
    if (f.type === TT.CMD_KEY_IMAGE_RLE) {
      for (let i = 3; i + 3 <= f.payload.length; i += 3) {
        for (let n = f.payload[i]; n > 0; n--) img[next++] = f.payload.readUInt16LE(i + 1);
      }
    } else {
      for (let i = 3; i + 2 <= f.payload.length; i += 2) img[next++] = f.payload.readUInt16LE(i);
    }
  }
  assert.strictEqual(next, KEY_IMG * KEY_IMG, 'image must be complete');
  return img;
}

// A typical OpenDeck key: flat colour, white "text" strokes, through JPEG like OpenDeck's
const key = Buffer.alloc(W * W * 4);
for (let y = 0; y < W; y++) {
  for (let x = 0; x < W; x++) {
    const i = (y * W + x) * 4;
    const text = y > 60 && y < 84 && x > 20 && x < 124 && (x % 12 < 3 || y % 12 < 3);
    key[i] = text ? 255 : 40;
    key[i + 1] = text ? 255 : 110;
    key[i + 2] = text ? 255 : 200;
    key[i + 3] = 255;
  }
}
const keyPx = keyImageFromDataUrl('data:image/jpeg;base64,' +
  jpeg.encode({ data: key, width: W, height: W }, 92).data.toString('base64'));

for (const tolerance of [0, 1]) {
  const deck = new Deck({ rleTolerance: tolerance });
  const frames = [...deck.imageFrames(3, keyPx)];
  const out = tftDecode(frames);
  const bytes = frames.reduce((a, f) => a + f.length, 0);
  let maxErr = 0;
  for (let i = 0; i < out.length; i++) {
    const a = keyPx.readUInt16LE(i * 2), b = out[i];
    maxErr = Math.max(maxErr, Math.abs((a >> 11) - (b >> 11)), Math.abs((a & 0x1f) - (b & 0x1f)));
  }
  if (tolerance === 0) assert.strictEqual(maxErr, 0, 'tolerance 0 must be lossless');
  else assert.ok(maxErr <= tolerance, `error ${maxErr} above tolerance`);
  console.log(`rle tolerance ${tolerance}: ${frames.length} frames, ${bytes} bytes ` +
    `(raw ${KEY_IMG * KEY_IMG * 2 + 5 * 16}), ${(bytes / (KEY_IMG * KEY_IMG * 2) * 100).toFixed(0)}%`);
}

// Noisy image: falls back to raw frames when RLE would be larger
const noise = Buffer.alloc(KEY_IMG * KEY_IMG * 2);
for (let i = 0; i < noise.length; i += 2) noise.writeUInt16LE((i * 2654435761) & 0xffff, i);
const noiseFrames = [...new Deck({ rleTolerance: 0 }).imageFrames(0, noise)];
assert.ok(noiseFrames.every((f) => f[1] === TT.CMD_KEY_IMAGE), 'noisy image should use raw frames');
tftDecode(noiseFrames);

console.log('self-test passed');
