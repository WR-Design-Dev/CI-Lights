const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '../main/assets/hostname.js'), 'utf8');
const nodes = [];
const parent = {children: [], append(node) { this.children.push(node); }};
const replies = [];
const requests = [];
const intervals = [];
const context = vm.createContext({window: {}, AbortSignal,
  document: {hidden: false, querySelector: selector => selector === '.wifi-settings' ? parent : null,
    createElement(tag) { const node = {tag, style: {}, dataset: {}, children: [], value: '',
      append(...children) { this.children.push(...children); }, setAttribute() {}, reportValidity() { return true; },
      addEventListener(name, callback) { this[name] = callback; }}; nodes.push(node); return node; }},
  uiI18n: {t: text => text, response: text => text, onChange() {}},
  setInterval(callback) { intervals.push(callback); },
  async fetch(url, options) { requests.push({url, options}); const next = replies.shift();
    if (next instanceof Error) throw next;
    assert.ok(next, 'Unexpected hostname HTTP request');
    return {ok: !next.error, text: async () => next.error, json: async () => next}; },
});
vm.runInContext(source, context);
const form = parent.children[0];
const input = nodes.find(node => node.name === 'hostname');
const button = nodes.find(node => node.tag === 'button');
const link = nodes.find(node => node.tag === 'a');
const message = nodes.at(-1);
const state = {configured: '', active: 'ci-lights-d25c', default_name: 'ci-lights-d25c', fallback: false, restart_required: false};
const flush = () => new Promise(resolve => setImmediate(resolve));
(async () => {
  assert.equal(requests.length, 0, 'Must not request protected settings before login');
  const pattern = new RegExp('^(?:' + input.pattern + ')$', 'v');
  for (const value of ['build-ampel', 'Build-Ampel', 'a', 'a'.repeat(32)]) assert.ok(pattern.test(value));
  for (const value of ['-ampel', 'ampel-', 'ampel.local', 'a'.repeat(33)]) assert.equal(pattern.test(value), false);
  replies.push({...state}); context.window.ciLightsHostname.start(); await flush();
  assert.equal(button.disabled, false);
  assert.equal(link.href, 'http://ci-lights-d25c.local');
  input.value = 'Build-Ampel'; input.input();
  replies.push({...state, configured: 'build-ampel', restart_required: true});
  await form.submit({preventDefault() {}});
  assert.equal(requests.at(-1).options.method, 'POST');
  assert.equal(JSON.parse(requests.at(-1).options.body).hostname, 'Build-Ampel');
  assert.equal(input.value, 'build-ampel');
  assert.match(message.textContent, /nächsten Neustart/);
  assert.equal(link.href, 'http://ci-lights-d25c.local', 'Save must not pretend the name already changed');
  replies.push({...state, configured: 'build-ampel', active: 'ci-lights-d25c-2', fallback: true});
  await intervals[0](); await flush();
  assert.match(message.textContent, /MAC-Standardname wird verwendet/);
  assert.equal(link.href, 'http://ci-lights-d25c-2.local');
  input.value = 'new-request'; input.input();
  const count = requests.length; await intervals[0](); await flush();
  assert.equal(requests.length, count, 'Polling must preserve unsaved edits');
  replies.push({error: 'Ungültiger Gerätename.'});
  await form.submit({preventDefault() {}});
  assert.equal(input.value, 'new-request');
  assert.equal(message.dataset.error, 'true');
  console.log('Hostname UI passed: authenticated load, pending restart, MAC conflict fallback, active address and preserved edits.');
})().catch(error => { console.error(error); process.exitCode = 1; });
