// Run the plugin against a fake OpenDeck, with the real bridge + TFT attached.
//   node test/mock-opendeck.cjs [seconds]
// It starts a WebSocket server like OpenDeck's, launches plugin.cjs the same way
// OpenDeck does, prints everything the plugin sends, and answers rerenderImages
// with numbered test images for all 15 keys plus a brightness change.
const path = require('path');
const { spawn } = require('child_process');
const jpeg = require('jpeg-js');
const { WebSocketServer } = require('ws');

const seconds = Number(process.argv[2] || 40);
const pluginDir = path.join(__dirname, '..');
const uuid = path.basename(pluginDir);

const HUES = [[220, 60, 60], [60, 170, 80], [60, 110, 210], [220, 160, 40], [150, 70, 200]];

// 144x144 JPEG: coloured background with a white bar code of the key number
function testImage(key) {
  const W = 144;
  const [r, g, b] = HUES[key % HUES.length];
  const rgba = Buffer.alloc(W * W * 4);
  for (let y = 0; y < W; y++) {
    for (let x = 0; x < W; x++) {
      const i = (y * W + x) * 4;
      const bar = y > 100 && y < 124 && x > 16 && x < 16 + (key + 1) * 8 && (x - 16) % 8 < 6;
      const border = x < 6 || y < 6 || x >= W - 6 || y >= W - 6;
      rgba[i] = bar || border ? 255 : r;
      rgba[i + 1] = bar || border ? 255 : g;
      rgba[i + 2] = bar || border ? 255 : b;
      rgba[i + 3] = 255;
    }
  }
  return 'data:image/jpeg;base64,' + jpeg.encode({ data: rgba, width: W, height: W }, 90).data.toString('base64');
}

const wss = new WebSocketServer({ host: '127.0.0.1', port: 0 }, () => {
  const port = wss.address().port;
  console.log(`[mock] OpenDeck stand-in on port ${port}, running for ${seconds}s`);
  const child = spawn(process.execPath, ['plugin.cjs', '-port', String(port), '-pluginUUID', uuid,
    '-registerEvent', 'registerPlugin', '-info', '{}'], { cwd: pluginDir, stdio: ['ignore', 'pipe', 'pipe'] });
  child.stdout.on('data', (d) => process.stdout.write(`[plugin] ${d}`));
  child.stderr.on('data', (d) => process.stdout.write(`[plugin:err] ${d}`));
  child.on('exit', (code) => console.log(`[mock] plugin exited (${code})`));
  setTimeout(() => {
    console.log('[mock] closing');
    wss.clients.forEach((c) => c.close());
    wss.close();
    setTimeout(() => process.exit(0), 1000);
  }, seconds * 1000);
});

wss.on('connection', (sock) => {
  sock.on('message', (data) => {
    const msg = JSON.parse(data.toString());
    console.log(`[mock] <- ${JSON.stringify(msg)}`);
    if (msg.event === 'registerDevice') {
      sock.send(JSON.stringify({ event: 'setBrightness', device: msg.payload.id, brightness: 80 }));
    } else if (msg.event === 'rerenderImages') {
      const device = msg.payload;
      sock.send(JSON.stringify({ event: 'setImage', device, controller: null, position: null, image: null }));
      for (let key = 0; key < 15; key++) {
        sock.send(JSON.stringify({ event: 'setImage', device, controller: 'Keypad', position: key, image: testImage(key) }));
      }
      console.log('[mock] -> sent 15 key images');
    }
  });
});
