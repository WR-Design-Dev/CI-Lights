import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {webcrypto} from 'node:crypto';
import * as core from '../web-flasher/flash-core.mjs';
import * as language from '../web-flasher/language.mjs';
import {messages} from '../web-flasher/translations.mjs';

const source = fs.readFileSync(new URL('../web-flasher/installer.js', import.meta.url), 'utf8')
  .replace(/^import .*;\r?\n/gm, '').replace('import(ESPTOOL_URL)', 'loadEsptool(ESPTOOL_URL)');
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
let unsubscribe;

assert.deepEqual(Object.keys(messages.en).sort(), Object.keys(messages.de).sort());
for (const name of ['index.html', 'impressum.html', 'datenschutz.html']) {
  const html = fs.readFileSync(new URL('../web-flasher/' + name, import.meta.url), 'utf8');
  assert.match(html, /<html lang="en">/, `${name} does not default to English`);
  assert.match(html, /data-language="en"/);
  assert.match(html, /data-language="de"/);
  for (const match of html.matchAll(/data-i18n(?:-html|-content|-aria-label|-title|-href)?="([^"]+)"/g)) {
    assert.ok(messages.en[match[1]] && messages.de[match[1]], `${name}: missing ${match[1]}`);
  }
}

function setup({corrupt = false, firmware = true, chip = 'ESP32-S3', sizeId = 0x16,
                secure = true, consent = true, query = ''} = {}) {
  unsubscribe?.();
  const events = [];
  const elements = new Map();
  function element(attributes = {}) {
    return {disabled: true, hidden: false, checked: false, dataset: {}, attributes,
      getAttribute(name) { return this.attributes[name]; },
      setAttribute(name, value) { this.attributes[name] = value; },
      addEventListener(name, fn) { this[name] = fn; }};
  }
  for (const id of ['firmware-version', 'installer-status', 'connect', 'flash', 'disconnect',
    'erase-device', 'flash-controls', 'erase', 'flash-progress', 'release-link']) {
    elements.set(`#${id}`, element());
  }
  const en = element({'data-language': 'en'});
  const de = element({'data-language': 'de'});
  elements.set('#language-en', en);
  elements.set('#language-de', de);
  elements.get('#flash').setAttribute('data-i18n', 'flashButton');
  elements.get('#firmware-version').setAttribute('data-i18n', 'loading');
  elements.get('#installer-status').setAttribute('data-i18n', 'checkingFiles');
  const privacy = element({href: 'datenschutz.html'});
  const legal = element({'data-i18n': 'legalLink', href: 'impressum.html'});
  const doc = {documentElement: {lang: 'en'}, querySelector: id => elements.get(id),
    querySelectorAll(selector) {
      if (selector === '[data-language]') return [en, de];
      const all = [...elements.values(), privacy, legal];
      if (selector === 'a[href]') return [privacy, legal];
      const attribute = selector.slice(1, -1);
      return all.filter(item => item.getAttribute(attribute) !== undefined);
    }};
  const win = {isSecureContext: secure, location: {href: base.href + query, origin: base.origin},
    addEventListener() {},
    history: {replaceState(_, __, href) { win.location.href = href; }},
    confirm(text) { events.push({confirmation: text}); return consent; }};
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
  const context = vm.createContext({...core, ...language, URL, AbortSignal, fetch,
    initLanguage: () => language.initLanguage(doc, win),
    onLanguageChange(callback) { unsubscribe = language.onLanguageChange(callback); },
    document: doc,
    navigator: {language: 'de-DE', serial: {requestPort() { events.push('request-port'); return Promise.resolve({}); }}},
    window: win,
    loadEsptool: async url => { events.push(url); return {ESPLoader, Transport}; },
  });
  vm.runInContext(source, context);
  const settle = async () => { for (let i = 0; i < 10; i++) await new Promise(resolve => setImmediate(resolve)); };
  return {events, elements, doc, win, privacy, legal, settle,
    click: async id => { await settle(); await elements.get('#' + id).click(); }};
}

const wrongOffset = structuredClone(manifest);
wrongOffset.builds[0].parts[0].offset = 0x9000;
assert.throws(() => core.validateManifest(wrongOffset, base), /flash address/);
const wrongUrl = structuredClone(manifest);
wrongUrl.builds[0].parts[3].path = 'https://example.com/CI-Lights.bin';
assert.throws(() => core.validateManifest(wrongUrl, base), /flash address/);

let app = setup();
await app.settle();
assert.equal(app.doc.documentElement.lang, 'en', 'Browser preference overrode English default');
assert.equal(app.elements.get('#flash').textContent, 'Install firmware');
assert.equal(app.elements.get('#language-en').getAttribute('aria-pressed'), 'true');
assert.equal(app.events.filter(event => String(event).includes('unpkg.com')).length, 0, 'CDN loaded on page open');
await app.click('connect');
assert.ok(app.events.indexOf('request-port') < app.events.indexOf('https://unpkg.com/esptool-js@0.6.1/bundle.js'));
assert.equal(app.events.includes('erase'), false, 'Connecting erased flash');
await app.click('language-de');
assert.equal(app.doc.documentElement.lang, 'de');
assert.equal(app.elements.get('#flash').textContent, 'Firmware installieren');
assert.match(app.elements.get('#installer-status').textContent, /verbunden/);
assert.equal(app.elements.get('#firmware-version').textContent, manifest.version, 'Language switch lost firmware version');
assert.match(app.privacy.getAttribute('href'), /lang=de/);
assert.match(app.legal.getAttribute('href'), /lang=de/);
assert.match(app.win.location.href, /lang=de/);
assert.equal(app.events.includes('disconnect'), false, 'Language switch closed the USB port');
assert.equal(app.events.includes('erase'), false, 'Language switch erased the device');
await app.click('flash');
const write = app.events.find(event => event.fileArray);
assert.match(app.events.find(event => event.confirmation).confirmation, /Gespeicherte Einstellungen/);
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
assert.match(app.elements.get('#installer-status').textContent, /checksum/);
await app.click('language-de');
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
assert.match(app.elements.get('#installer-status').textContent, /fully erased/);

app = setup({query: '?lang=de'});
await app.click('connect');
await app.click('erase-device');
assert.equal(app.doc.documentElement.lang, 'de');
assert.match(app.events.find(event => event.confirmation).confirmation, /GESAMTEN Flash/);
assert.match(app.elements.get('#installer-status').textContent, /vollständig gelöscht/);
await app.click('language-en');
assert.match(app.elements.get('#installer-status').textContent, /fully erased/);
assert.equal(app.elements.get('#language-de').getAttribute('aria-pressed'), 'false');

app = setup({query: '?lang=unexpected'});
await app.settle();
assert.equal(app.doc.documentElement.lang, 'en');

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
await app.click('language-de');
assert.match(app.elements.get('#firmware-version').textContent, /nicht verfügbar/);
assert.match(app.elements.get('#installer-status').textContent, /Bitte diese Seite/);
unsubscribe?.();
console.log('Browser installer checks passed: English default, German flag switch, language links, translated status and confirmation, preserved settings, optional erase and SHA-256.');
