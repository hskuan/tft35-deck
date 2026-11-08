// Copy this plugin into OpenDeck's plugin folder. Run `npm install` first,
// then `npm run deploy`, then restart OpenDeck.
const fs = require('fs');
const os = require('os');
const path = require('path');

const name = path.basename(__dirname);  // com.tft35deck.bridge.sdPlugin = the plugin UUID

function pluginsDir() {
  switch (process.platform) {
    case 'win32':
      return path.join(process.env.APPDATA, 'opendeck', 'plugins');
    case 'darwin':
      return path.join(os.homedir(), 'Library', 'Application Support', 'opendeck', 'plugins');
    default:
      return path.join(process.env.XDG_CONFIG_HOME || path.join(os.homedir(), '.config'), 'opendeck', 'plugins');
  }
}

if (!fs.existsSync(path.join(__dirname, 'node_modules'))) {
  console.error('node_modules missing: run `npm install` in this folder first');
  process.exit(1);
}

const dir = pluginsDir();
if (!fs.existsSync(dir)) {
  console.error(`OpenDeck plugin folder not found: ${dir} (is OpenDeck installed and started once?)`);
  process.exit(1);
}

const target = path.join(dir, name);
fs.rmSync(target, { recursive: true, force: true });
fs.cpSync(__dirname, target, {
  recursive: true,
  filter: (src) => !src.includes(`${path.sep}test${path.sep}`) && !src.endsWith(`${path.sep}test`),
});
console.log(`installed to ${target}\nrestart OpenDeck to load it`);
