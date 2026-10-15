const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '../main/assets/hostname.js'), 'utf8');
const state = {configured: '', active: 'ci-lights-d25c', default_name: 'ci-lights-d25c',
  fallback: false, restart_required: false, restarting: false};
const flush = () => new Promise(resolve => setImmediate(resolve));

function createUi() {
  const nodes = [];
  const parent = {children: [], append(node) { this.children.push(node); }};
  const replies = [], requests = [], intervals = [], confirmations = [], answers = [];
  const context = vm.createContext({
    window: {confirm(text) { confirmations.push(text); assert.ok(answers.length, 'Unexpected confirmation'); return answers.shift(); }},
    AbortSignal,
    document: {hidden: false, querySelector: selector => selector === '.wifi-settings' ? parent : null,
      createElement(tag) { const node = {tag, style: {}, dataset: {}, children: [], value: '',
        append(...children) { this.children.push(...children); }, setAttribute() {}, reportValidity() { return true; },
        addEventListener(name, callback) { this[name] = callback; }}; nodes.push(node); return node; }},
    uiI18n: {t: text => text, response: text => text, onChange() {}},
    setInterval(callback) { intervals.push(callback); },
    async fetch(url, options) {
      requests.push({url, options});
      const next = replies.shift();
      if (next instanceof Error) throw next;
      assert.ok(next, 'Unexpected hostname HTTP request');
      return {ok: !next.error, text: async () => next.error, json: async () => next};
    },
  });
  vm.runInContext(source, context);
  const form = parent.children[0];
  const input = nodes.find(node => node.name === 'hostname');
  return {input, form, button: nodes.find(node => node.tag === 'button'),
    link: nodes.find(node => node.tag === 'a'), message: nodes.at(-1),
    replies, requests, intervals, confirmations, answers,
    async load(updated = state) { replies.push({...updated}); context.window.ciLightsHostname.start(); await flush(); },
    edit(value) { input.value = value; input.input(); },
    submit: () => form.submit({preventDefault() {}}),
    async poll() { intervals[0](); await flush(); },
  };
}

(async () => {
  const ui = createUi();
  assert.equal(ui.requests.length, 0, 'Must not request protected settings before login');
  const pattern = new RegExp('^(?:' + ui.input.pattern + ')$', 'v');
  for (const value of ['build-ampel', 'Build-Ampel', 'a', 'a'.repeat(32)]) assert.ok(pattern.test(value));
  for (const value of ['-ampel', 'ampel-', 'ampel.local', 'a'.repeat(33)]) assert.equal(pattern.test(value), false);
  await ui.load();
  assert.equal(ui.button.disabled, true, 'Unchanged settings must not allow a needless restart');
  await ui.submit();
  assert.equal(ui.requests.length, 1);
  assert.equal(ui.confirmations.length, 0);
  assert.equal(ui.link.href, 'http://ci-lights-d25c.local');

  ui.edit('Build-Ampel');
  assert.equal(ui.button.disabled, false);
  ui.answers.push(false);
  await ui.submit();
  assert.match(ui.confirmations.at(-1), /http:\/\/build-ampel\.local/);
  assert.match(ui.confirmations.at(-1), /MAC-Standardnamen/);
  assert.equal(ui.requests.length, 1, 'Cancelling must not save settings or request a restart');
  assert.equal(ui.input.value, 'Build-Ampel');
  assert.equal(ui.input.disabled, false);
  assert.equal(ui.link.href, 'http://ci-lights-d25c.local');

  ui.form.reportValidity = () => false;
  await ui.submit();
  assert.equal(ui.confirmations.length, 1, 'Invalid input must not trigger confirmation or save');
  ui.form.reportValidity = () => true;
  ui.answers.push(true);
  ui.replies.push({error: 'Gerätename konnte nicht gespeichert werden.'});
  await ui.submit();
  assert.equal(ui.message.dataset.error, 'true');
  assert.equal(ui.input.value, 'Build-Ampel', 'Save errors must preserve input for retry');
  assert.equal(ui.button.disabled, false);
  assert.equal(ui.input.disabled, false);
  assert.equal(ui.link.href, 'http://ci-lights-d25c.local', 'Save errors must not claim a changed address');

  ui.answers.push(true);
  ui.replies.push({...state, configured: 'build-ampel', restart_required: true, restarting: true});
  await ui.submit();
  assert.equal(ui.requests.at(-1).options.method, 'POST');
  assert.deepEqual(JSON.parse(ui.requests.at(-1).options.body), {hostname: 'Build-Ampel', restart: true});
  assert.equal(ui.input.value, 'build-ampel');
  assert.match(ui.message.textContent, /Ampel startet neu/);
  assert.match(ui.message.textContent, /ci-lights-d25c\.local/);
  assert.equal(ui.link.href, 'http://build-ampel.local');
  assert.equal(ui.button.disabled, true);
  assert.equal(ui.input.disabled, true);
  const restartingCount = ui.requests.length;
  await ui.submit();
  await ui.poll();
  assert.equal(ui.requests.length, restartingCount, 'No duplicate submit or polling while restarting');

  const pending = createUi();
  await pending.load({...state, configured: 'build-ampel', restart_required: true});
  assert.equal(pending.button.disabled, false, 'Names saved by an earlier version must still be applicable');
  assert.match(pending.message.textContent, /Neustart erforderlich/);
  pending.answers.push(true);
  pending.replies.push({...state, configured: 'build-ampel', restart_required: true, restarting: true});
  await pending.submit();
  assert.deepEqual(JSON.parse(pending.requests.at(-1).options.body), {hostname: 'build-ampel', restart: true});

  const mac = createUi();
  await mac.load({...state, configured: 'build-ampel', active: 'build-ampel'});
  mac.edit('BUILD-AMPEL');
  assert.equal(mac.button.disabled, true, 'Capitalization alone must not restart the device');
  mac.edit('');
  mac.answers.push(true);
  mac.replies.push({...state, restart_required: true, restarting: true});
  await mac.submit();
  assert.match(mac.confirmations[0], /http:\/\/ci-lights-d25c\.local/);
  assert.equal(mac.link.href, 'http://ci-lights-d25c.local');
  assert.deepEqual(JSON.parse(mac.requests.at(-1).options.body), {hostname: '', restart: true});

  const conflict = createUi();
  await conflict.load({...state, configured: 'build-ampel', active: 'ci-lights-d25c-2', fallback: true});
  assert.match(conflict.message.textContent, /MAC-Standardname wird verwendet/);
  assert.equal(conflict.link.href, 'http://ci-lights-d25c-2.local');
  conflict.edit('new-request');
  const count = conflict.requests.length;
  await conflict.poll();
  assert.equal(conflict.requests.length, count, 'Polling must preserve unsaved edits');
  conflict.answers.push(true);
  conflict.replies.push({error: 'Ungültiger Gerätename.'});
  await conflict.submit();
  assert.equal(conflict.input.value, 'new-request');
  assert.equal(conflict.message.dataset.error, 'true');
  console.log('Hostname UI passed: confirmation/cancel, save errors, delayed restart state, pending saved names, MAC default and conflict fallback.');
})().catch(error => { console.error(error); process.exitCode = 1; });
