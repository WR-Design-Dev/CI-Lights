// Exercise the administration update flow without a connected ESP or browser.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '../main/assets/ota.js'), 'utf8');

const elements = new Map();
for (const id of ['ota-check', 'ota-install', 'ota-message', 'ota-progress', 'ota-current', 'ota-latest']) {
  elements.set(`#${id}`, {dataset: {}, hidden: false, disabled: false, listeners: {},
    addEventListener(name, callback) { this.listeners[name] = callback; }});
}
const responses = [];
const requests = [];
let now = 100000;
let reloads = 0;
const base = {phase: 'idle', message: 'ready', current_version: '1.0.10', latest_version: '',
  busy: false, boot_pending: false, configured: true, bytes: 0, total: 1000};
const context = {
  document: {querySelector: id => elements.get(id)},
  window: {location: {reload() { reloads++; }}},
  uiI18n: {t: value => value, response: value => value, onChange() {}},
  confirm: () => true, AbortController, setTimeout, clearTimeout, setInterval() {},
  Date: {now: () => now},
  async fetch(path, options) {
    requests.push({path, options});
    assert.ok(options.signal, 'Requests must be cancellable');
    const result = responses.shift();
    if (result instanceof Error) throw result;
    assert.ok(result, 'Unexpected HTTP request');
    return {ok: true, json: async () => ({...base, ...result})};
  },
};
vm.createContext(context);
vm.runInContext(source, context);
const flush = () => new Promise(resolve => setImmediate(resolve));

(async () => {
  responses.push({});
  context.window.ciLightsOta.start();
  await flush();
  assert.equal(elements.get('#ota-check').disabled, false);
  assert.equal(elements.get('#ota-install').hidden, true);

  responses.push({phase: 'available', latest_version: '1.0.11'});
  await elements.get('#ota-check').listeners.click();
  assert.equal(elements.get('#ota-install').hidden, false);

  responses.push({phase: 'checking', busy: true, latest_version: '1.0.11'});
  elements.get('#ota-install').listeners.click();
  await flush();
  const install = requests.find(request => request.path === '/api/ota/install');
  assert.deepEqual(JSON.parse(install.options.body), {version: '1.0.11'});
  assert.equal(install.options.headers['Content-Type'], 'application/json');

  responses.push({phase: 'rebooting', busy: true, latest_version: '1.0.11'});
  await context.window.ciLightsOta.refresh();
  responses.push({phase: 'boot_validation', current_version: '1.0.11', boot_pending: true});
  await context.window.ciLightsOta.refresh();
  assert.equal(reloads, 0, 'Do not report success before the new firmware confirms startup');
  responses.push({current_version: '1.0.11'});
  await context.window.ciLightsOta.refresh();
  assert.equal(reloads, 1, 'Reload once startup is confirmed');

  responses.push({phase: 'rebooting', busy: true, latest_version: '1.0.12'});
  // A real reload resets this module. Create a fresh context for a second update.
  vm.runInContext(source, context);
  await context.window.ciLightsOta.refresh();
  now += 121000;
  responses.push(new Error('Device unreachable'));
  await context.window.ciLightsOta.refresh();
  assert.equal(elements.get('#ota-message').dataset.error, 'true');
  assert.match(elements.get('#ota-message').textContent, /noch nicht erreichbar/);
  assert.equal(elements.get('#ota-check').disabled, false, 'Allow recovery after a reboot timeout');
  console.log('OTA UI flow passed: version selection, startup confirmation, and reboot timeout');
})().catch(error => { console.error(error); process.exitCode = 1; });
