// OpenDeck device plugin for the TFT35 Deck.
//
// OpenDeck starts this with:
//   node plugin.cjs -port <port> -pluginUUID <uuid> -registerEvent registerPlugin -info <json>
// It registers over OpenDeck's WebSocket, finds the bridge on USB serial
// (lib/deck.cjs) and registers it as a 5x3 key + 1 encoder device in the "td"
// namespace. Key presses and the knob go to OpenDeck; OpenDeck's key images
// (144x144 JPEG) are scaled to 80x80 RGB565 and sent to the TFT.
//
// Optional config: config.json next to this file, e.g.
//   { "port": "COM8", "rleTolerance": 1 }   (0 = lossless image compression)
// or the TFT35DECK_PORT environment variable, to skip auto-detection.
// Output goes to OpenDeck's plugin log (logs/plugins/<uuid>.log).
const fs = require('fs');
const path = require('path');
const WebSocket = require('ws');
const { Deck } = require('./lib/deck.cjs');
const { keyImageFromDataUrl } = require('./lib/image.cjs');
const { KEY } = require('./lib/protocol.cjs');

const NAMESPACE = 'td';  // must match DeviceNamespace in manifest.json

function log(msg) {
  console.log(`${new Date().toISOString()} ${msg}`);
}

function parseArgs(argv) {
  const args = {};
  for (let i = 0; i < argv.length - 1; i++) {
    if (argv[i].startsWith('-')) args[argv[i].slice(1)] = argv[i + 1];
  }
  return args;
}

function loadConfig() {
  const config = {};
  try {
    Object.assign(config, JSON.parse(fs.readFileSync(path.join(__dirname, 'config.json'), 'utf8')));
  } catch (e) { /* optional */ }
  if (process.env.TFT35DECK_PORT) config.port = process.env.TFT35DECK_PORT;
  return config;
}

const args = parseArgs(process.argv.slice(2));
if (!args.port || !args.pluginUUID) {
  console.error('usage: node plugin.cjs -port <port> -pluginUUID <uuid> -registerEvent <event> -info <json>');
  process.exit(1);
}
const config = loadConfig();

// ---- OpenDeck connection -------------------------------------------------------

const ws = new WebSocket(`ws://127.0.0.1:${args.port}`);
const outbox = [];

function send(event, payload) {
  const msg = JSON.stringify(payload === undefined ? { event } : { event, payload });
  if (ws.readyState === WebSocket.OPEN) ws.send(msg);
  else outbox.push(msg);
}

ws.on('open', () => {
  ws.send(JSON.stringify({ event: args.registerEvent || 'registerPlugin', uuid: args.pluginUUID }));
  outbox.splice(0).forEach((m) => ws.send(m));
  log('registered with OpenDeck');
  deck.start();
});

ws.on('close', () => {
  log('OpenDeck closed the connection, exiting');
  deck.stop();
  setTimeout(() => process.exit(0), 300);
});

ws.on('error', (e) => log(`websocket error: ${e.message}`));

ws.on('message', (data) => {
  let msg;
  try {
    msg = JSON.parse(data.toString());
  } catch (e) {
    return;
  }
  if (!deviceId || msg.device !== deviceId) return;

  if (msg.event === 'setImage') {
    if (msg.controller == null && msg.position == null) {
      imageCache.clear();
      deck.clearAll();
    } else if (msg.controller === 'Keypad' && msg.position < keyCount) {
      if (!msg.image) {
        imageCache.set(msg.position, null);
        deck.setImage(msg.position, null);
      } else {
        try {
          const pixels = keyImageFromDataUrl(msg.image);
          imageCache.set(msg.position, pixels);
          deck.setImage(msg.position, pixels);
        } catch (e) {
          log(`key ${msg.position}: ${e.message}`);
        }
      }
    }
    // Encoder (dial) images: the TFT has no display for the knob
  } else if (msg.event === 'setBrightness') {
    deck.setBrightness(msg.brightness);
  }
});

// ---- Deck ------------------------------------------------------------------------

const deck = new Deck({ log, port: config.port || null, rleTolerance: config.rleTolerance ?? 1 });
let deviceId = null;
let keyCount = 0;
let tftReady = false;
const imageCache = new Map();  // key -> last RGB565 image (or null = blank), for refreshes
let refreshTimer = null;

function requestImages() {
  if (deviceId) send('rerenderImages', deviceId);
}

// The TFT entered host mode (boot, reconnect) and shows blank keys: resend
// every cached image right away, then ask OpenDeck to re-render in case
// anything changed meanwhile. Debounced: the TFT may ask twice in a row.
function refreshAll(reason) {
  clearTimeout(refreshTimer);
  refreshTimer = setTimeout(() => {
    log(`refreshing all keys (${reason})`);
    for (const [key, pixels] of imageCache) deck.setImage(key, pixels);
    setTimeout(requestImages, 300);
  }, 200);
}

deck.on('connected', (info) => {
  deviceId = `${NAMESPACE}-${String(info.serial).replace(/[^A-Za-z0-9]/g, '')}`;
  keyCount = info.rows * info.columns;
  tftReady = info.tftOnline && info.tftFramed;
  send('registerDevice', {
    id: deviceId,
    name: 'TFT35 Deck',
    rows: info.rows,
    columns: info.columns,
    encoders: info.dials,
    type: 0,
  });
  log(`registered device ${deviceId}`);
  // give the TFT a moment to switch to host mode, then ask for all key images
  setTimeout(requestImages, 500);
});

// HOST_INFO arrives when the TFT comes online or asks for all keys (HELLO)
deck.on('info', (info) => {
  tftReady = info.tftOnline && info.tftFramed;
  if (tftReady) refreshAll('TFT ready');
});

deck.on('disconnected', () => {
  if (deviceId) send('deregisterDevice', deviceId);
  deviceId = null;
  imageCache.clear();
});

deck.on('key', ({ key, state }) => {
  if (!deviceId || key >= keyCount) return;
  send(state === KEY.PRESSED ? 'keyDown' : 'keyUp', { device: deviceId, position: key });
});

deck.on('dial', ({ dial, ticks }) => {
  if (deviceId) send('encoderChange', { device: deviceId, position: dial, ticks });
});

deck.on('dialButton', ({ dial }) => {
  if (!deviceId) return;
  send('encoderDown', { device: deviceId, position: dial });
  send('encoderUp', { device: deviceId, position: dial });
});

process.on('SIGINT', () => {
  deck.stop();
  setTimeout(() => process.exit(0), 300);
});
