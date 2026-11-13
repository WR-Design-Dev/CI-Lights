const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const script = fs.readFileSync(path.join(__dirname, '../main/assets/backup.js'), 'utf8');
const header = fs.readFileSync(path.join(__dirname, '../main/app_backup_ui.h'), 'utf8');
const html = [...header.matchAll(/"(?:[^"\\]|\\.)*"/g)].map(match => JSON.parse(match[0])).join('');
const translations = vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../main/assets/localization.js'), 'utf8')
  .split('const uiI18n =')[0] + '\nuiTranslations;');
const flush = () => new Promise(resolve => setImmediate(resolve));
const sealed = {format: 'ci-lights-backup', schema: 1, data: 'encrypted'};
const info = {title: '<img src=x onerror=bad()>', hostname: 'build-ampel', admin_username: 'source-admin',
  networks: ['IoT', 'Hotspot'], jenkins_url: 'https://jenkins.example', job_path: '/job/build/',
  import_id: 'a'.repeat(64), expires_in_seconds: 600};
function setup(initial = false) {
  const elements = new Map(), calls = [], replies = [], downloads = [], timers = new Map(), blobs = [];
  let nextTimer = 0, now = 0;
  for (const [, id] of html.matchAll(/id="([^"]+)"/g)) elements.set('#' + id,
    {value: '', checked: false, disabled: false, hidden: false, dataset: {}, files: [], textContent: '',
      listeners: {}, addEventListener(name, fn) { this.listeners[name] = fn; },
      showModal() { assert.equal(this.open, undefined); this.open = true; }, close() { this.open = undefined; },
      querySelectorAll() { return [...elements.entries()].filter(([key]) => key.startsWith(id.includes('export') ? '#config-export' : '#config-import') || key === '#config-validate').map(([, value]) => value); }});
  const el = id => elements.get('#config-' + id);
  if (initial) elements.delete('#config-export-form');
  const context = {document: {querySelector: id => elements.get(id), body: {append() {}},
    createElement() { return {click() { downloads.push({href: this.href, filename: this.download}); }, remove() {}}; }},
    uiI18n: {t: text => text, response: text => text, onChange() {}}, TextEncoder, AbortController, Blob, TypeError,
    Date: {now: () => now},
    URL: {createObjectURL(blob) { blobs.push(blob); return 'blob:encrypted'; }, revokeObjectURL() {}},
    setTimeout(fn, delay) { const id = ++nextTimer; timers.set(id, {fn, delay}); return id; }, clearTimeout(id) { timers.delete(id); },
    async fetch(route, options) {
      calls.push({route, options});
      const reply = replies.length ? await replies.shift() : null;
      if (reply instanceof Error) throw reply;
      if (reply?.error) return {ok: false, text: async () => reply.error};
      return {ok: true, status: reply?.status || 200,
        json: async () => reply?.data || (route.endsWith('/export') ? sealed : info)};
    }};
  vm.runInNewContext(script, context);
  return {el, calls, replies, downloads, timers, blobs, advance(ms) { now += ms; },
    async poll() {
      const timer = [...timers.entries()].find(([, value]) => value.delay === 1000);
      assert.ok(timer, 'The next status poll must be scheduled');
      timers.delete(timer[0]); timer[1].fn(); await flush();
    },
    async fire(id, event = 'submit') { await el(id).listeners[event]({preventDefault() {}}); await flush(); },
    select(text = JSON.stringify(sealed), size = text.length) { el('import-file').files = [{size, text: async () => text}]; },
  };
}
(async () => {
  const ui = setup();
  ui.el('export-password').value = 'backup-password-123'; ui.el('export-confirm').value = 'wrong';
  await ui.fire('export-form'); assert.equal(ui.calls.length, 0, 'Password confirmation is mandatory');
  ui.el('export-confirm').value = ui.el('export-password').value;
  let finishExport;
  ui.replies.push(new Promise(resolve => { finishExport = resolve; }));
  const exporting = ui.fire('export-form'); await flush();
  assert.equal(ui.el('import-password').disabled, true);
  assert.equal(ui.el('busy').open, true, 'Encryption shows a modal spinner');
  assert.match(ui.el('busy-detail').textContent, /60 Sekunden/);
  await ui.fire('export-form'); assert.equal(ui.calls.length, 1, 'Do not start duplicate exports');
  finishExport(null); await exporting;
  assert.ok(!ui.el('busy').open);
  assert.equal(ui.downloads[0].filename, 'CI-Lights-Konfiguration.json');
  assert.equal(ui.el('export-password').value, ''); assert.equal(ui.el('export-confirm').value, '');
  assert.equal(ui.el('export-message').dataset.error, 'false');

  ui.select(); ui.el('import-password').value = 'backup-password-123';
  let finishCheck;
  ui.replies.push(new Promise(resolve => { finishCheck = resolve; }));
  const checking = ui.fire('import-form'); await flush();
  assert.equal(ui.el('busy').open, true, 'Validation shows a modal spinner');
  assert.match(ui.el('busy-detail').textContent, /60 Sekunden/);
  let prevented = false;
  ui.el('busy').listeners.cancel({preventDefault() { prevented = true; }});
  assert.equal(prevented, true, 'Escape cannot dismiss an active operation');
  finishCheck(null); await checking;
  assert.ok(!ui.el('busy').open);
  assert.equal(ui.calls.at(-1).route, '/api/config/validate');
  assert.equal(ui.el('import-review').hidden, false);
  assert.ok(ui.el('import-summary').textContent.includes(info.title), 'Summary uses text, not HTML');
  assert.equal(ui.el('import-apply').disabled, true);
  assert.equal(ui.el('import-direct').hidden, true);
  await ui.fire('import-apply', 'click');
  assert.ok(!ui.calls.some(call => call.route.endsWith('/import')), 'Checking a file never imports it');
  ui.el('import-confirm').checked = true; await ui.fire('import-confirm', 'change');
  assert.equal(ui.el('import-apply').disabled, false);
  ui.el('import-password').value = 'different-password'; await ui.fire('import-password', 'input');
  assert.equal(ui.el('import-apply').disabled, true, 'Changing the password invalidates the review');
  assert.equal(ui.el('import-review').hidden, true);
  await ui.fire('import-form');
  ui.el('import-confirm').checked = true; await ui.fire('import-confirm', 'change');
  ui.replies.push({error: 'Speicherfehler'}); await ui.fire('import-apply', 'click');
  assert.equal(ui.el('import-message').dataset.error, 'true');
  assert.equal(ui.el('import-apply').hidden, true, 'A failed prepared import invalidates the consumed snapshot');
  assert.equal(ui.el('import-direct').hidden, false, 'A failed import can be checked or imported directly again');
  assert.ok(!ui.el('busy').open, 'Failure closes the spinner');
  await ui.fire('import-form');
  ui.el('import-confirm').checked = true; await ui.fire('import-confirm', 'change');
  let finishImport;
  ui.replies.push(new Promise(resolve => { finishImport = resolve; }));
  const importing = ui.fire('import-apply', 'click'); await flush();
  assert.equal(ui.el('busy').open, true, 'Prepared import also shows a spinner');
  assert.ok(!ui.el('busy-detail').textContent.includes('60'), 'Prepared import does not promise another minute of decryption');
  finishImport(null); await importing;
  const imported = JSON.parse(ui.calls.at(-1).options.body);
  assert.deepEqual(imported, {confirmed: true, import_id: info.import_id}, 'Prepared import sends only its ID and confirmation');
  assert.equal(ui.el('import-password').value, '');
  assert.equal(ui.el('import-apply').disabled, true);
  assert.equal(ui.el('export-password').disabled, true, 'Keep controls disabled while the device restarts');

  const initial = setup(true);
  initial.select(); initial.el('import-password').value = 'backup-password-123';
  await initial.fire('import-form');
  assert.equal(initial.el('import-review').hidden, false, 'Initial setup supports the same import flow');
  initial.select(); await initial.fire('import-file', 'change');
  assert.equal(initial.el('import-review').hidden, true, 'Changing the file invalidates the review');
  initial.select('{broken'); await initial.fire('import-form');
  assert.equal(initial.el('import-message').dataset.error, 'true');
  const before = initial.calls.length;
  initial.select('x', 160 * 1024 + 1); await initial.fire('import-form');
  assert.equal(initial.calls.length, before, 'Oversized files are rejected before upload');
  initial.select(); initial.el('import-password').value = 'short'; await initial.fire('import-form');
  assert.equal(initial.calls.length, before, 'Short backup passwords are rejected');
  initial.el('import-password').value = 'backup-password-123'; initial.replies.push({error: 'Sicherungspasswort falsch oder Datei beschädigt.'});
  await initial.fire('import-form'); assert.equal(initial.el('import-review').hidden, true);
  assert.equal(initial.el('import-apply').disabled, true);

  for (const initialSetup of [false, true]) {
    const direct = setup(initialSetup);
    direct.select(); direct.el('import-password').value = 'backup-password-123';
    await direct.fire('import-direct', 'click');
    assert.equal(direct.calls.length, 0, 'Direct import requires explicit replacement confirmation');
    direct.el('import-confirm').checked = true; await direct.fire('import-confirm', 'change');
    let finish;
    direct.replies.push(new Promise(resolve => { finish = resolve; }));
    const pending = direct.fire('import-direct', 'click'); await flush();
    assert.equal(direct.calls.length, 1);
    assert.equal(direct.calls[0].route, '/api/config/import', 'Direct import does not run a separate validation request');
    assert.deepEqual(JSON.parse(direct.calls[0].options.body), {confirmed: true, password: 'backup-password-123', backup: sealed});
    assert.equal(direct.el('busy').open, true); assert.match(direct.el('busy-detail').textContent, /60 Sekunden/);
    await direct.fire('import-direct', 'click'); assert.equal(direct.calls.length, 1, 'No duplicate direct imports');
    finish(null); await pending;
    assert.ok(!direct.el('busy').open); assert.equal(direct.el('import-password').value, '');
  }
  const expired = setup();
  expired.select(); expired.el('import-password').value = 'backup-password-123';
  await expired.fire('import-form');
  const expiry = [...expired.timers.values()].find(timer => timer.delay === 600000);
  assert.ok(expiry, 'The UI expires the prepared snapshot after ten minutes');
  expiry.fn();
  assert.equal(expired.el('import-review').hidden, true);
  assert.equal(expired.el('import-direct').hidden, false);
  assert.match(expired.el('import-message').textContent, /abgelaufen/);
  const invalid = setup(true);
  invalid.select('{broken'); invalid.el('import-password').value = 'backup-password-123'; invalid.el('import-confirm').checked = true;
  await invalid.fire('import-direct', 'click');
  assert.equal(invalid.calls.length, 0); assert.ok(!invalid.el('busy').open);
  invalid.select(); invalid.replies.push({error: 'Sicherungspasswort falsch oder Datei beschädigt.'});
  await invalid.fire('import-direct', 'click');
  assert.equal(invalid.el('import-message').dataset.error, 'true'); assert.ok(!invalid.el('busy').open);

  const operationId = 'b'.repeat(64);
  for (const initialSetup of [false, true]) {
    const background = setup(initialSetup);
    background.select(); background.el('import-password').value = 'backup-password-123';
    background.replies.push({status: 202, data: {operation_id: operationId}},
      {data: {pending: true}}, new TypeError('Failed to fetch'), {data: {done: true, result: info}});
    const checking = background.fire('import-form'); await flush();
    assert.equal(background.calls.length, 1, 'Only the initial POST sends the backup');
    await background.poll();
    assert.equal(background.el('busy').open, true, 'The spinner stays visible while the background job runs');
    await background.poll();
    assert.equal(background.el('busy').open, true, 'A transient polling connection error does not abort the operation');
    await background.poll(); await checking;
    assert.equal(background.el('import-review').hidden, false);
    assert.equal(background.calls.filter(call => call.options.method === 'POST').length, 1, 'Reconnect never starts a second decryption');
    for (const call of background.calls.slice(1)) {
      assert.equal(call.route, '/api/config/transfer?operation_id=' + operationId);
      assert.equal(call.options.method, 'GET'); assert.equal(call.options.body, undefined);
    }
    background.el('import-confirm').checked = true;
    background.replies.push({status: 202, data: {operation_id: operationId}}, {data: {done: true, result: {restarting: true}}});
    const applying = background.fire('import-apply', 'click'); await flush();
    assert.deepEqual(JSON.parse(background.calls.at(-1).options.body), {confirmed: true, import_id: info.import_id});
    await background.poll(); await applying;
    assert.ok(!background.el('busy').open); assert.equal(background.el('import-password').value, '');
  }
  const backgroundError = setup(true);
  backgroundError.select(); backgroundError.el('import-password').value = 'backup-password-123';
  backgroundError.el('import-confirm').checked = true;
  backgroundError.replies.push({status: 202, data: {operation_id: operationId}},
    {data: {done: true, error: 'Sicherungspasswort falsch oder Datei beschädigt.'}});
  const rejectedImport = backgroundError.fire('import-direct', 'click'); await flush();
  await backgroundError.poll(); await rejectedImport;
  assert.ok(!backgroundError.el('busy').open);
  assert.match(backgroundError.el('import-message').textContent, /Sicherungspasswort falsch/);
  assert.equal(backgroundError.el('import-password').disabled, false, 'A failed background operation can be retried');
  const backgroundExport = setup();
  backgroundExport.el('export-password').value = backgroundExport.el('export-confirm').value = 'backup-password-123';
  backgroundExport.replies.push({status: 202, data: {operation_id: operationId}}, {data: {done: true, result: sealed}});
  const downloading = backgroundExport.fire('export-form'); await flush();
  await backgroundExport.poll(); await downloading;
  assert.deepEqual(JSON.parse(await backgroundExport.blobs[0].text()), sealed);
  const timeout = setup(true);
  timeout.select(); timeout.el('import-password').value = 'backup-password-123';
  timeout.replies.push({status: 202, data: {operation_id: operationId}});
  const timedOut = timeout.fire('import-form'); await flush();
  timeout.advance(180000); await timeout.poll(); await timedOut;
  assert.match(timeout.el('import-message').textContent, /zu lange gedauert/);
  assert.ok(!timeout.el('busy').open);
  const disconnected = setup(true);
  disconnected.select(); disconnected.el('import-password').value = 'backup-password-123';
  disconnected.replies.push(new TypeError('Failed to fetch'));
  await disconnected.fire('import-form');
  assert.match(disconnected.el('import-message').textContent, /WLAN-Verbindung prüfen/);
  assert.equal(disconnected.calls.length, 1, 'A failed POST is not repeated automatically');
  for (const [, text] of html.matchAll(/>([^<>]+)</g)) assert.ok(translations.has(text), 'Missing translation: ' + text);
  console.log('Backup UI passed: background operations, reconnects, spinners, setup/admin imports, expiry, confirmation and failures');
})().catch(error => { console.error(error); process.exitCode = 1; });
