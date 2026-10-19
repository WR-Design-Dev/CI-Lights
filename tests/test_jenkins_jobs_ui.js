const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

// Run the actual dashboard script embedded in the firmware page.
const firmware = fs.readFileSync(path.join(__dirname, '../main/app_config.c'), 'utf8');
const pageStart = firmware.indexOf('static esp_err_t status_page_handler(');
const page = firmware.slice(pageStart, firmware.indexOf('\n}', pageStart));
const html = [...page.matchAll(/"(?:[^"\\]|\\.)*"/g)].map(match => JSON.parse(match[0])).join('');
const script = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(match => match[1]).join('\n');
assert.ok(script.includes('function restoreJobs()'));
const flush = () => new Promise(resolve => setImmediate(resolve));
const cacheKey = username => 'ci-lights:jobs:' + username;
const savedJobs = {jobs: [{name: 'Folder / Build', url: 'https://jenkins.example/job/build/'}],
  selected_url: 'https://jenkins.example/job/build/'};

function createUi(saved) {
  const elements = new Map(), storage = new Map(), requests = [], jobReplies = [], intervals = [];
  if (saved !== undefined) storage.set(cacheKey('admin'), typeof saved === 'string' ? saved : JSON.stringify(saved));
  function element(selector) {
    if (!elements.has(selector)) {
      const classes = new Set();
      elements.set(selector, {value: '', disabled: false, hidden: false, open: false, dataset: {}, children: [],
        textContent: '', listeners: {}, attributes: {}, contentDocument: null,
        classList: {add: name => classes.add(name), remove: name => classes.delete(name),
          toggle(name, enabled) { if (enabled) classes.add(name); else classes.delete(name); },
          contains: name => classes.has(name)},
        setAttribute(name, value) { this.attributes[name] = String(value); },
        addEventListener(name, callback) { this.listeners[name] = callback; },
        querySelector: child => element(selector + ' ' + child),
        replaceChildren(...children) { this.children = children; },
        focus() {}, select() {}, append() {},
      });
    }
    return elements.get(selector);
  }
  const tabs = ['job', 'jenkins', 'manual'].map(tab => {
    const link = element('tab:' + tab); link.dataset.tab = tab; return link;
  });
  const response = (data = {}, error) => ({ok: !error, status: error ? 503 : 200,
    text: async () => error || 'Gespeichert.', json: async () => data});
  const fixtures = {
    '/api/poll-interval': {minutes: 5, job_list_timeout_seconds: 60, status_timeout_seconds: 90},
    '/api/site-title': {title: 'CI-Lights'}, '/api/cpu-mode': {mode: 'fixed160'},
    '/api/lights': {mode: 'auto', brightness: 50, build_effect: 'pulse'},
    '/api/wifi': {networks: [], connected_ssid: ''}, '/api/wifi-networks': {networks: [], details: []},
    '/api/branding': {favicon: {uploaded: false}},
  };
  const context = {
    document: {hidden: false, querySelector: element,
      querySelectorAll: selector => selector === '[data-tab]' ? tabs : [], addEventListener() {}},
    localStorage: {getItem: key => storage.get(key) ?? null, setItem: (key, value) => storage.set(key, value)},
    Option: function(text, value) { this.text = text; this.value = value; },
    FormData: class { *[Symbol.iterator]() { yield ['jenkins_url', 'https://jenkins.example']; } },
    Headers, TextEncoder, btoa,
    uiI18n: {t: text => text, response: text => text, start() {}, onChange() {}},
    ciLightsWifi: {forForm: () => ({setMode() {}, setNetworks() {}})},
    ciLightsOta: {start() {}}, ciLightsHostname: {start() {}},
    setTimeout() {}, clearTimeout() {}, setInterval(callback) { intervals.push(callback); },
    async fetch(url, options) {
      requests.push({url, options});
      if (url === '/api/jobs') {
        assert.ok(jobReplies.length, 'The job list must only be requested by an explicit refresh');
        return jobReplies.shift();
      }
      return response(fixtures[url]);
    },
  };
  context.window = context;
  vm.runInNewContext(script, context);
  async function fire(selector, eventName = 'submit') {
    const target = element(selector);
    await target.listeners[eventName]({target, preventDefault() {}});
    await flush();
  }
  return {element, storage, requests, jobReplies, response, fire, intervals,
    jobRequests: () => requests.filter(request => request.url === '/api/jobs'),
    async login(username = 'admin') {
      element('#login-username').value = username; element('#login-password').value = 'password';
      await fire('#login-form');
    },
    async tab(name) { await fire('tab:' + name, 'click'); },
  };
}

(async () => {
  const empty = createUi();
  await empty.login();
  assert.equal(empty.jobRequests().length, 0);
  assert.equal(empty.element('#job').disabled, true, 'An empty cache must wait for the refresh button');
  await empty.fire('#jenkins-form');
  assert.equal(empty.jobRequests().length, 0, 'Saving credentials must not load jobs');
  assert.equal(empty.element('#job').disabled, true);

  const ui = createUi(savedJobs);
  await ui.login();
  assert.equal(ui.element('#job').children[0].text, 'Folder / Build');
  assert.equal(ui.element('#job').disabled, false);
  assert.equal(ui.element('#job-list-timeout').value, 60);
  assert.equal(ui.element('#status-timeout').value, 90);
  for (const tab of ['jenkins', 'manual', 'job']) await ui.tab(tab);
  await ui.login();
  for (const refresh of ui.intervals) await refresh();
  await flush();
  assert.equal(ui.jobRequests().length, 0, 'Login, tabs, and periodic updates must use cached jobs');
  await ui.fire('#jenkins-form');
  assert.deepEqual(JSON.parse(ui.storage.get(cacheKey('admin'))), savedJobs, 'Credentials must preserve the local list');
  assert.equal(ui.element('#job').children[0].text, 'Folder / Build');
  ui.element('#job').value = savedJobs.selected_url;
  await ui.fire('#job-form');
  assert.equal(ui.jobRequests().length, 0, 'Showing a job must query its status without refreshing the list');
  assert.ok(ui.requests.some(request => request.url === '/api/job'));

  const refreshed = {jobs: [{name: 'Another job', url: 'https://jenkins.example/job/another/'}]};
  let finish;
  ui.jobReplies.push(new Promise(resolve => { finish = resolve; }));
  const requestInProgress = ui.fire('#refresh-jobs', 'click');
  await flush();
  assert.equal(ui.jobRequests().length, 1, 'The refresh button starts the request');
  assert.equal(ui.element('#refresh-jobs').disabled, true);
  await ui.fire('#refresh-jobs', 'click');
  assert.equal(ui.jobRequests().length, 1, 'A slow request cannot start a duplicate refresh');
  finish(ui.response(refreshed));
  await requestInProgress;
  assert.equal(ui.element('#job').children[0].text, 'Another job');
  assert.deepEqual(JSON.parse(ui.storage.get(cacheKey('admin'))), refreshed);
  assert.equal(ui.element('#refresh-jobs').disabled, false);
  ui.jobReplies.push(ui.response(null, 'Jenkins ist nicht erreichbar.'));
  await ui.fire('#refresh-jobs', 'click');
  assert.equal(ui.element('#job').children[0].text, 'Another job', 'A failed refresh preserves cached jobs');
  assert.deepEqual(JSON.parse(ui.storage.get(cacheKey('admin'))), refreshed);
  assert.equal(ui.element('#refresh-jobs').disabled, false);
  assert.equal(ui.element('#job-loading').classList.contains('show'), false);

  ui.element('#poll-interval').value = '7';
  ui.element('#job-list-timeout').value = '180';
  ui.element('#status-timeout').value = '120';
  await ui.fire('#poll-interval-form');
  const settingsPosts = () => ui.requests.filter(request => request.url === '/api/poll-interval' && request.options?.method === 'POST');
  assert.deepEqual(JSON.parse(settingsPosts().at(-1).options.body), {
    poll_interval_minutes: 7, job_list_timeout_seconds: 180, status_timeout_seconds: 120,
  });
  for (const selector of ['#status-timeout', '#job-list-timeout']) {
    for (const value of ['0', '-1', '601', '1.5', '', 'NaN']) {
      ui.element(selector).value = value;
      await ui.fire('#poll-interval-form');
    }
    ui.element(selector).value = '60';
  }
  assert.equal(settingsPosts().length, 1, 'Invalid timeouts must not be saved');
  ui.element('#job-list-timeout').value = '1';
  ui.element('#status-timeout').value = '600';
  await ui.fire('#poll-interval-form');
  assert.equal(settingsPosts().length, 2, 'Timeout boundaries must be accepted');
  assert.equal(ui.jobRequests().length, 2, 'Saving timeout settings never loads jobs');

  const reopened = createUi(JSON.parse(ui.storage.get(cacheKey('admin'))));
  await reopened.login();
  assert.equal(reopened.element('#job').children[0].text, 'Another job');
  assert.equal(reopened.jobRequests().length, 0, 'Reopening uses the persisted local list');
  await reopened.login('someone-else');
  assert.equal(reopened.element('#job').children.length, 0, 'Cached lists stay scoped to the logged-in user');
  assert.equal(reopened.jobRequests().length, 0);
  const corrupt = createUi('invalid JSON');
  await corrupt.login();
  assert.equal(corrupt.jobRequests().length, 0, 'A corrupt cache must wait for an explicit refresh');
  console.log('Jenkins UI passed: manual refresh only, persistent cache, slow/error recovery, and configurable timeouts');
})().catch(error => { console.error(error); process.exitCode = 1; });
