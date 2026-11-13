// Local preview of the real firmware page and assets; no device or GitHub writes.
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const read = name => fs.readFileSync(path.join(root, name), 'utf8');
const literals = source => [...source.matchAll(/"(?:[^"\\]|\\.)*"/g)].map(match => JSON.parse(match[0])).join('');
function macro(source, name) {
  const lines = source.split(/\r?\n/);
  const start = lines.findIndex(line => line.startsWith('#define ' + name + ' '));
  if (start < 0) throw new Error('Missing preview macro: ' + name);
  let index = start;
  while (lines[index].endsWith('\\')) ++index;
  return literals(lines.slice(start, index + 1).join('\n'));
}
function page() {
  const source = read('main/app_config.c');
  const start = source.indexOf('static esp_err_t status_page_handler(');
  const body = source.slice(start, source.indexOf('\n}', start));
  const macros = {
    LANGUAGE_SWITCH_HTML: macro(source, 'LANGUAGE_SWITCH_HTML'),
    WIFI_AUTH_FIELDS_HTML: macro(source, 'WIFI_AUTH_FIELDS_HTML'),
    OTA_AUTOMATION_HTML: macro(read('main/app_ota_ui.h'), 'OTA_AUTOMATION_HTML'),
    OTA_AUTOMATION_STYLE: macro(read('main/app_ota_ui.h'), 'OTA_AUTOMATION_STYLE'),
    CONFIG_TRANSFER_STYLE: macro(read('main/app_backup_ui.h'), 'CONFIG_TRANSFER_STYLE'),
    CONFIG_IMPORT_HTML: macro(read('main/app_backup_ui.h'), 'CONFIG_IMPORT_HTML'),
    CONFIG_EXPORT_HTML: macro(read('main/app_backup_ui.h'), 'CONFIG_EXPORT_HTML'),
  };
  const tokens = body.match(/"(?:[^"\\]|\\.)*"|\b[A-Z][A-Z_]+\b/g) || [];
  let html = tokens.map(token => token.startsWith('"') ? JSON.parse(token) : (macros[token] || '')).join('');
  const banner = '<div style="padding:.65rem 1rem;background:#fff1bf;color:#604615;text-align:center;font:600 13px system-ui">Lokale GUI-Vorschau · Beispieldaten · Keine Verbindung zu einer Ampel</div>';
  html = html.replace('<body>', '<body>' + banner);
  const setup = `<script>
    document.querySelector('#login-username').value='Vorschau';
    document.querySelector('#login-password').value='local-preview';
    document.querySelector('#login-form').dispatchEvent(new Event('submit',{bubbles:true,cancelable:true}));
    document.querySelector('#ota-details').open=true;
    setTimeout(()=>document.querySelector('#ota-details').scrollIntoView({block:'start'}),250);
  </script>`;
  return html.replace('</body>', setup + '</body>');
}

const names = [...read('main/app_timezones.inc').matchAll(/\{"([^"]+)", \d+\}/g)].map(match => match[1]);
const automation = {mode: 'off', interval_hours: 24, schedule: 'daily', install_time: '03:00',
  time_zone: 'Europe/Berlin', timezone_auto: true, clock_synced: true, storage_ok: true, running: true,
  next_check_in_seconds: 14340, last_result: 'available', last_installed_version: '1.0.1017',
  blocked_version: '', version_blocked: false};
const security = {profile: 'secure', ready: true, secure_boot: true, flash_encryption: true,
  nvs_encryption: true, rom_download_disabled: true, revoked_keys: 0, key_rotation: 'none'};
let language = 'de';
const local = stamp => new Intl.DateTimeFormat('sv-SE', {timeZone: automation.time_zone || 'UTC',
  year: 'numeric', month: '2-digit', day: '2-digit', hour: '2-digit', minute: '2-digit', second: '2-digit',
  hourCycle: 'h23'}).format(new Date(stamp * 1000)).replace(' ', 'T');
function ota() {
  const now = Math.floor(Date.now() / 1000);
  return {phase: 'available', message: 'Neue Firmware ist verfügbar.', current_version: '1.0.1017',
    latest_version: '1.0.1018', busy: false, boot_pending: false, configured: true, bytes: 0, total: 1253376,
    security, automation: {...automation, device_time: now, local_time: local(now),
      last_check_at: now - 60, last_check_local: local(now - 60), next_install_at: now + 36000,
      next_install_local: local(now + 36000), last_install_at: now - 86400, last_install_local: local(now - 86400)}};
}
const fixtures = {
  '/api/security': security,
  '/api/site-title': {title: 'CI-Lights'},
  '/api/cpu-mode': {mode: 'fixed160'},
  '/api/poll-interval': {minutes: 5, job_list_timeout_seconds: 60, status_timeout_seconds: 60},
  '/api/lights': {mode: 'auto', red: false, yellow: false, green: true, brightness: 50,
    red_color: '#ff0000', yellow_color: '#ffff00', green_color: '#00ff00', build_effect: 'pulse', disco_effect: 'rainbow'},
  '/api/jenkins-request': {in_progress: false},
  '/api/hostname': {configured: 'set-ampel', active: 'set-ampel', default_name: 'ci-lights-c08c',
    fallback: false, fallback_pending: false, restart_required: false, restarting: false},
  '/api/wifi': {networks: [], connected_ssid: 'IoT'}, '/api/wifi-networks': {networks: [], details: []},
  '/api/branding': {favicon: {uploaded: false}},
};
const assets = new Set(['localization.js', 'ota.js', 'wifi.js', 'hostname.js', 'security.js', 'backup.js']);
const server = http.createServer(async (request, response) => {
  try {
    const url = new URL(request.url, 'http://127.0.0.1');
    const route = url.pathname;
    response.setHeader('Cache-Control', 'no-store');
    if (route === '/') { response.setHeader('Content-Type', 'text/html; charset=utf-8'); response.end(page()); return; }
    if (assets.has(route.slice(1))) {
      response.setHeader('Content-Type', 'application/javascript; charset=utf-8');
      response.end(read('main/assets/' + route.slice(1))); return;
    }
    if (route === '/traffic-light.svg') {
      response.setHeader('Content-Type', 'image/svg+xml'); response.end(read('main/assets/TrafficLight.svg')); return;
    }
    let body = '';
    for await (const chunk of request) { body += chunk; if (body.length > 4096) throw new Error('Preview body too large'); }
    response.setHeader('Content-Type', 'application/json; charset=utf-8');
    if (route === '/api/login') { response.end('{}'); return; }
    if (route.startsWith('/api/config/')) {
      response.statusCode = 501;
      response.end('Konfigurationssicherungen sind nur auf einer Ampel verfügbar. Diese lokale Vorschau zeigt die Oberfläche.');
      return;
    }
    if (route === '/api/language') {
      if (request.method === 'POST') language = JSON.parse(body).language;
      response.end(JSON.stringify({language})); return;
    }
    if (route === '/api/ota/timezones') { response.end(JSON.stringify({zones: names})); return; }
    if (route === '/api/ota/settings') {
      const value = JSON.parse(body);
      if (!['off', 'check', 'install'].includes(value.mode) || ![1, 6, 12, 24].includes(value.interval_hours)) throw new Error('Invalid preview settings');
      Object.assign(automation, value);
      response.end(JSON.stringify(ota())); return;
    }
    if (route === '/api/ota' || route === '/api/ota/check' || route === '/api/ota/install') {
      response.end(JSON.stringify(ota())); return;
    }
    if (Object.hasOwn(fixtures, route)) { response.end(JSON.stringify(fixtures[route])); return; }
    response.statusCode = 404; response.end('{}');
  } catch (error) { response.statusCode = 500; response.end(String(error.message)); }
});
server.listen(4177, '127.0.0.1', () => console.log('OTA GUI preview: http://127.0.0.1:4177/'));
