import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {webcrypto} from 'node:crypto';
import * as core from '../web-flasher/flash-core.mjs';

const source = fs.readFileSync(new URL('../web-flasher/installer.js', import.meta.url), 'utf8')
  .replace(/^import .*;\r?\n/, '').replace('import(ESPTOOL_URL)', 'loadEsptool(ESPTOOL_URL)');
const data = [new Uint8Array(32).fill(1), new Uint8Array(32).fill(2),
  new Uint8Array(8192).fill(255), new Uint8Array(4096).fill(4)];
const names = ['bootloader.bin', 'partition-table.bin', 'ota_data_initial.bin', 'CI-Lights.bin'];
const offsets = [0, 0x8000, 0x11000, 0x20000];
const manifest = {schema: 1, name: 'CI-Lights', version: '1.0.1001', layout: '4mb-ota-v1',
  release_url: 'https://github.com/WR-Design-Dev/CI-Lights/releases/tag/v1.0.1001',
  builds: [{chipFamily: 'ESP32-S3', parts: await Promise.all(data.map(async (bytes, index) => ({
    path: `firmware/v1.0.1001/${names[index]}`, offset: offsets[index], size: bytes.length,
    sha256: Buffer.from(await webcrypto.subtle.digest('SHA-256', bytes)).toString('hex'),
  })))}]};
const base = new URL('https://wr-design-dev.github.io/CI-Lights/install-manifest.json');

function setup({corrupt = false, firmware = true, chip = 'ESP32-S3', sizeId = 0x16,
                secure = true, consent = true} = {}) {
  const events = [];
  const elements = new Map();
  for (const id of ['firmware-version', 'installer-status', 'connect', 'flash', 'disconnect',
    'erase-device', 'flash-controls', 'erase', 'flash-progress', 'release-link']) {
    elements.set(`#${id}`, {disabled: true, hidden: false, checked: false, dataset: {},
      addEventListener(name, fn) { this[name] = fn; }});
  }
  class Transport {
    async disconnect() { events.push('disconnect'); }
  }
  class ESPLoader {
    constructor() {
      this.chip = {CHIP_NAME: chip};
      this.DETECTED_FLASH_SIZES = {0x15: '2MB', 0x16: '4MB'};
    }
    async main() { events.push('detect'); }
    async readFlashId() { return (sizeId << 16) | 0x2046; }
    flashSizeBytes(size) { return parseInt(size) * 1024 * 1024; }
    async eraseFlash() { events.push('erase'); }
    async writeFlash(options) {
      events.push(options);
      options.fileArray.forEach((file, index) => options.reportProgress(index, file.data.length, file.data.length));
    }
    async after() { events.push('reset'); }
  }
  const fetch = async (input) => {
    const url = String(input);
    events.push(url);
    if (url.endsWith('install-manifest.json')) return {ok: firmware, json: async () => structuredClone(manifest)};
    const index = names.findIndex(name => url.endsWith('/' + name));
    const bytes = data[index].slice();
    if (corrupt && index === 3) bytes[1] ^= 1;
    return {ok: true, arrayBuffer: async () => bytes.buffer};
  };
  globalThis.fetch = fetch;
  const context = vm.createContext({...core, URL, AbortSignal, fetch,
    document: {querySelector: id => elements.get(id)},
    navigator: {serial: {requestPort() { events.push('request-port'); return Promise.resolve({}); }}},
    window: {isSecureContext: secure, location: {href: base.href}, addEventListener() {},
      confirm(text) { events.push({confirmation: text}); return consent; }},
    loadEsptool: async url => { events.push(url); return {ESPLoader, Transport}; },
  });
  vm.runInContext(source, context);
  const settle = async () => { for (let i = 0; i < 10; i++) await new Promise(resolve => setImmediate(resolve)); };
  return {events, elements, settle, click: async id => { await settle(); await elements.get('#' + id).click(); }};
}

const wrongOffset = structuredClone(manifest);
wrongOffset.builds[0].parts[0].offset = 0x9000;
assert.throws(() => core.validateManifest(wrongOffset, base), /Flash-Adresse/);
const wrongUrl = structuredClone(manifest);
wrongUrl.builds[0].parts[3].path = 'https://example.com/CI-Lights.bin';
assert.throws(() => core.validateManifest(wrongUrl, base), /Flash-Adresse/);

let app = setup();
await app.settle();
assert.equal(app.events.filter(event => String(event).includes('unpkg.com')).length, 0, 'CDN loaded on page open');
await app.click('connect');
assert.ok(app.events.indexOf('request-port') < app.events.indexOf('https://unpkg.com/esptool-js@0.6.1/bundle.js'));
assert.equal(app.events.includes('erase'), false, 'Connecting erased flash');
await app.click('flash');
const write = app.events.find(event => event.fileArray);
assert.ok(write, 'Installation did not write');
assert.equal(write.eraseAll, false);
assert.equal(write.flashSize, 'keep');
assert.equal(write.flashMode, 'keep');
assert.equal(write.flashFreq, 'keep');
assert.deepEqual(write.fileArray.map(file => file.address), offsets);
assert.equal(app.events.includes('erase'), false, 'Default installation erased settings');
assert.equal(app.elements.get('#flash-progress').value, 100);

app = setup({corrupt: true});
await app.click('connect');
app.elements.get('#erase').checked = true;
await app.click('flash');
assert.equal(app.events.includes('erase'), false, 'Corrupt download erased device');
assert.equal(app.events.some(event => event.fileArray), false, 'Corrupt download was flashed');
assert.match(app.elements.get('#installer-status').textContent, /Prüfsumme/);

app = setup();
await app.click('connect');
app.elements.get('#erase').checked = true;
await app.click('flash');
assert.ok(app.events.indexOf('erase') > app.events.indexOf(base.origin + '/CI-Lights/firmware/v1.0.1001/CI-Lights.bin'));
assert.ok(app.events.indexOf('erase') < app.events.findIndex(event => event.fileArray));

app = setup({consent: false});
await app.click('connect');
await app.click('erase-device');
await app.click('flash');
assert.equal(app.events.includes('erase'), false, 'Cancelled deletion was performed');
assert.equal(app.events.some(event => event.fileArray), false, 'Cancelled installation was performed');

app = setup({firmware: false});
await app.click('connect');
assert.equal(app.elements.get('#flash').disabled, true);
assert.equal(app.elements.get('#erase-device').disabled, false);
await app.click('erase-device');
assert.equal(app.events.includes('erase'), true, 'Independent erase requires a firmware download');
assert.equal(app.events.some(event => event.fileArray), false, 'Independent erase installed firmware');
assert.match(app.elements.get('#installer-status').textContent, /vollständig gelöscht/);

for (const options of [{chip: 'ESP32'}, {sizeId: 0x15}, {sizeId: 0xff}]) {
  app = setup(options);
  await app.click('connect');
  assert.equal(app.elements.get('#flash').disabled, true);
  assert.equal(app.events.includes('disconnect'), true, 'Rejected device port remained open');
}
app = setup({secure: false});
await app.settle();
assert.equal(app.elements.get('#connect').disabled, true);
assert.equal(app.events.length, 0);
console.log('Browser installer checks passed: local settings preserved, confirmed optional erase, hashes, device checks, lazy UNPKG load.');
