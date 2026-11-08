// OpenDeck key image (data URL, 144x144 JPEG) -> TFT key image (80x80 RGB565 LE)
const jpeg = require('jpeg-js');

const KEY_IMG = 80;  // TFT host key image size (DECK_KEY_IMG)

function decodeDataUrl(dataUrl) {
  const m = /^data:([^;,]+)(;base64)?,(.*)$/s.exec(dataUrl);
  if (!m) throw new Error('not a data URL');
  const body = m[2] ? Buffer.from(m[3], 'base64') : Buffer.from(decodeURIComponent(m[3]), 'latin1');
  return { mime: m[1], body };
}

// Box-filter resize of RGBA pixels to size x size, then pack as RGB565 LE.
function toRgb565(rgba, width, height, size = KEY_IMG) {
  const out = Buffer.alloc(size * size * 2);
  for (let dy = 0; dy < size; dy++) {
    const sy0 = Math.floor((dy * height) / size);
    const sy1 = Math.max(sy0 + 1, Math.floor(((dy + 1) * height) / size));
    for (let dx = 0; dx < size; dx++) {
      const sx0 = Math.floor((dx * width) / size);
      const sx1 = Math.max(sx0 + 1, Math.floor(((dx + 1) * width) / size));
      let r = 0, g = 0, b = 0, n = 0;
      for (let y = sy0; y < sy1; y++) {
        for (let x = sx0; x < sx1; x++) {
          const i = (y * width + x) * 4;
          r += rgba[i];
          g += rgba[i + 1];
          b += rgba[i + 2];
          n++;
        }
      }
      r = Math.round(r / n);
      g = Math.round(g / n);
      b = Math.round(b / n);
      out.writeUInt16LE(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3), (dy * size + dx) * 2);
    }
  }
  return out;
}

// Returns an 80x80 RGB565 LE buffer, or throws for unsupported images.
function keyImageFromDataUrl(dataUrl) {
  const { mime, body } = decodeDataUrl(dataUrl);
  if (mime !== 'image/jpeg') throw new Error(`unsupported image type ${mime}`);
  const img = jpeg.decode(body, { useTArray: true, formatAsRGBA: true });
  return toRgb565(img.data, img.width, img.height);
}

// Run-length encode RGB565 LE pixels as [count, colour] runs (count 1-255).
// Pixels within `tolerance` steps per channel of the run's first pixel join
// the run: JPEG noise would otherwise break flat areas into single pixels.
// tolerance 0 = lossless. Returns [{ offset, count, color }] runs.
function rleRuns(rgb565, tolerance = 1) {
  const n = rgb565.length / 2;
  const runs = [];
  let i = 0;
  while (i < n) {
    const color = rgb565.readUInt16LE(i * 2);
    const r = color >> 11, g = (color >> 5) & 0x3f, b = color & 0x1f;
    let len = 1;
    while (i + len < n && len < 255) {
      const c = rgb565.readUInt16LE((i + len) * 2);
      if (Math.abs((c >> 11) - r) > tolerance || Math.abs(((c >> 5) & 0x3f) - g) > tolerance * 2 ||
          Math.abs((c & 0x1f) - b) > tolerance) break;
      len++;
    }
    runs.push({ offset: i, count: len, color });
    i += len;
  }
  return runs;
}

module.exports = { KEY_IMG, decodeDataUrl, toRgb565, keyImageFromDataUrl, rleRuns };
