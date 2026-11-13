// Exercise saved settings, unsaved edits and failure recovery without hardware.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '../main/assets/ota.js'), 'utf8');
const translationContext = vm.createContext({});
const translations = vm.runInContext(fs.readFileSync(path.join(__dirname, '../main/assets/localization.js'), 'utf8')
  .split('const uiI18n =')[0] + '\nuiTranslations;', translationContext);
const flush = () => new Promise(resolve => setImmediate(resolve));

async function setup(overrides = {}) {
  const elements = new Map();
  for (const id of [...source.matchAll(/querySelector\('#([^']+)'\)/g)].map(match => match[1])) {
    elements.set('#' + id, {value: '', textContent: '', hidden: false, disabled: false,
      dataset: {}, listeners: {}, addEventListener(name, callback) { this.listeners[name] = callback; },
      replaceChildren(...children) { this.children = children; }});
  }
  const el = id => elements.get('#ota-' + id);
  const calls = [];
  let interval;
  let languageChanged;
  let now = 100000;
  let saveFailure = false;
  let server = {phase: 'available', message: 'ready', current_version: '1.0.10', latest_version: '1.0.11',
    busy: false, boot_pending: false, configured: true, bytes: 0, total: 1000,
    automation: {mode: 'off', interval_hours: 24, schedule: 'daily', install_time: '03:00',
      time_zone: '', timezone_auto: false, running: true, storage_ok: true, clock_synced: true,
      local_time: '2026-11-13T12:30:00', last_result: 'never', next_check_in_seconds: 300, ...overrides}};
  const context = {document: {querySelector: id => elements.get(id), hidden: false}, window: {location: {reload() {}}},
    uiI18n: {language: 'de', t: text => context.uiI18n.language === 'en' ? translations.get(text) || text : text,
      response: text => text, onChange(callback) { languageChanged = callback; }},
    Intl: {DateTimeFormat: function(locale, options) {
      return options ? new Intl.DateTimeFormat(locale, options) : {resolvedOptions: () => ({timeZone: 'Europe/Berlin'})};
    }},
    Option: function(text, value) { this.text = text; this.value = value; },
    Date: class extends Date { static now() { return now; } },
    AbortController, setTimeout, clearTimeout, setInterval(callback) { interval = callback; }, confirm: () => true,
    async fetch(route, options) {
      calls.push({route, options});
      assert.ok(options.signal);
      if (route.endsWith('/timezones')) return {ok: true, json: async () => ({zones: ['Europe/Berlin', 'America/New_York', 'Etc/UTC']})};
      if (route.endsWith('/settings')) {
        if (saveFailure) return {ok: false, text: async () => 'Save failed'};
        Object.assign(server.automation, JSON.parse(options.body));
      }
      return {ok: true, json: async () => structuredClone(server)};
    }};
  vm.createContext(context);
  vm.runInContext(source, context);
  context.window.ciLightsOta.start();
  await flush();
  return {el, calls, context, get server() { return server; },
    setFailure(value) { saveFailure = value; },
    edit(id, value) { el(id).value = value; el(id).listeners.input(); },
    save: () => el('automation-form').listeners.submit({preventDefault() {}}),
    refresh: () => context.window.ciLightsOta.refresh(),
    setLanguage(value) { context.uiI18n.language = value; languageChanged(); },
    async tick() { now += 16000; interval(); await flush(); }};
}

(async () => {
  const first = await setup();
  assert.equal(first.el('auto-timezone').value, 'Europe/Berlin');
  assert.equal(first.el('auto-save').disabled, false, 'Detected zone requires explicit save');
  assert.equal(first.calls.filter(call => call.options.method === 'POST').length, 0, 'Opening the page must not save settings');
  await first.save();
  const saved = JSON.parse(first.calls.find(call => call.route.endsWith('/settings')).options.body);
  assert.deepEqual(saved, {mode: 'off', interval_hours: 24, schedule: 'daily', install_time: '03:00',
    time_zone: 'Europe/Berlin', timezone_auto: true});
  assert.equal(first.el('auto-save').disabled, true);
  first.setLanguage('en');
  assert.equal(first.el('auto-timezone').children.find(option => option.value === 'Europe/Berlin').text, 'Germany / Berlin');
  assert.equal(first.el('auto-timezone').value, 'Europe/Berlin', 'Language changes preserve the selected timezone');

  const header = fs.readFileSync(path.join(__dirname, '../main/app_ota_ui.h'), 'utf8');
  const html = [...header.matchAll(/"(?:[^"\\]|\\.)*"/g)].map(match => JSON.parse(match[0])).join('');
  for (const [, text] of html.matchAll(/>([^<>]+)</g)) {
    if (text !== '–') assert.ok(translations.has(text), 'Missing UI translation: ' + text);
  }

  const existing = await setup({mode: 'install', time_zone: 'America/New_York', timezone_auto: true});
  assert.equal(existing.el('auto-timezone').value, 'America/New_York', 'Keep saved timezone when another browser visits');
  assert.equal(existing.el('time-field').hidden, false);
  existing.edit('auto-time', '04:45');
  existing.edit('auto-timezone', 'Etc/UTC');
  existing.server.automation.install_time = '05:00';
  await existing.refresh();
  assert.equal(existing.el('auto-time').value, '04:45', 'Polling preserves unsaved edits');
  assert.equal(existing.el('auto-timezone').value, 'Etc/UTC');
  existing.setFailure(true);
  await existing.save();
  assert.equal(existing.el('auto-time').value, '04:45', 'Failed saves preserve drafts');
  assert.equal(existing.el('settings-message').dataset.error, 'true');
  assert.equal(existing.el('auto-save').disabled, false, 'Allow retry');
  existing.setFailure(false);
  await existing.save();
  assert.equal(existing.server.automation.timezone_auto, false, 'Manual selection clears browser source flag');
  existing.el('browser-timezone').listeners.click();
  await existing.save();
  assert.equal(existing.server.automation.time_zone, 'Europe/Berlin');
  assert.equal(existing.server.automation.timezone_auto, true);

  existing.edit('auto-schedule', 'immediate');
  assert.equal(existing.el('time-field').hidden, true);
  assert.equal(existing.el('auto-time').required, false);
  await existing.save();
  existing.server.automation.clock_synced = false;
  await existing.refresh();
  assert.match(existing.el('auto-state').textContent, /Internetzeit/);
  existing.server.automation.clock_synced = true;
  existing.server.automation.blocked_version = '1.0.11';
  existing.server.automation.version_blocked = true;
  await existing.refresh();
  assert.equal(existing.el('blocked-version').hidden, false);
  assert.match(existing.el('auto-state').textContent, /übersprungen/);
  assert.equal(existing.el('install').disabled, false, 'Rollback quarantine allows manual retry');

  existing.server.busy = true;
  await existing.refresh();
  assert.equal(existing.el('auto-mode').disabled, true);
  assert.equal(existing.el('auto-save').disabled, true);
  existing.server.busy = false;
  await existing.refresh();
  const before = existing.calls.length;
  await existing.tick();
  assert.ok(existing.calls.length > before, 'Automatic mode refreshes status while idle');
  console.log('OTA automation UI passed: detection, persistence, drafts, save failures, schedule, clock and rollback');
})().catch(error => { console.error(error); process.exitCode = 1; });
