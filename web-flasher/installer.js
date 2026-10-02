import {validateManifest, downloadFiles, checkDevice, writeFirmware} from './flash-core.mjs';

// Load Espressif's fixed, self-contained bundle only on the user's Connect click.
const ESPTOOL_URL = 'https://unpkg.com/esptool-js@0.6.1/bundle.js';
const version = document.querySelector('#firmware-version');
const status = document.querySelector('#installer-status');
const connectButton = document.querySelector('#connect');
const flashButton = document.querySelector('#flash');
const disconnectButton = document.querySelector('#disconnect');
const eraseButton = document.querySelector('#erase-device');
const controls = document.querySelector('#flash-controls');
const erase = document.querySelector('#erase');
const progress = document.querySelector('#flash-progress');
const release = document.querySelector('#release-link');
let manifest;
let loader;
let transport;
let busy = false;
const supported = window.isSecureContext && Boolean(navigator.serial);

function message(text, error = false) {
  status.textContent = text;
  status.dataset.error = String(error);
}

function buttons() {
  connectButton.disabled = busy || Boolean(loader) || !supported;
  connectButton.hidden = Boolean(loader);
  controls.hidden = !loader;
  flashButton.disabled = busy || !loader || !manifest;
  disconnectButton.disabled = busy || !loader;
  eraseButton.disabled = busy || !loader;
  erase.disabled = busy;
}

async function closePort() {
  const active = transport;
  transport = undefined;
  loader = undefined;
  if (active) {
    try { await active.disconnect(); }
    catch { /* A resetting or unplugged USB device can already be closed. */ }
  }
}

async function connect() {
  if (busy || loader || !supported) return;
  busy = true;
  buttons();
  try {
    // Request the port before any await that could lose the click's user gesture.
    const port = await navigator.serial.requestPort();
    message('Flash-Werkzeug wird von unpkg.com geladen …');
    const {ESPLoader, Transport} = await import(ESPTOOL_URL);
    transport = new Transport(port, false);
    const candidate = new ESPLoader({transport, baudrate: 115200, debugLogging: false,
      terminal: {clean() {}, write() {}, writeLine() {}}});
    message('Ampel wird verbunden … Bei Bedarf BOOT halten und RESET drücken.');
    await candidate.main();
    const size = await checkDevice(candidate);
    loader = candidate;
    message(`ESP32-S3 mit ${size} verbunden. Installation oder Gerät löschen ist jetzt möglich.`);
  } catch (error) {
    await closePort();
    message(error.name === 'NotFoundError' ? 'Keine Ampel ausgewählt. Du kannst erneut verbinden.' :
      `Verbindung fehlgeschlagen: ${error.message}. Seriellen Monitor schließen; bei Bedarf BOOT und RESET verwenden.`, true);
  } finally {
    busy = false;
    buttons();
  }
}

async function flash() {
  if (busy || !loader || !manifest) return;
  const eraseAll = erase.checked;
  if (!window.confirm(eraseAll ?
    `Firmware ${manifest.version} installieren und ALLE WLAN-Daten, Zugangsdaten und Branding löschen?` :
    `Firmware ${manifest.version} installieren? Gespeicherte Einstellungen bleiben erhalten.`)) return;
  busy = true;
  progress.hidden = false;
  progress.value = 0;
  buttons();
  try {
    message('Firmware-Dateien werden geladen und mit SHA-256 geprüft …');
    const files = await downloadFiles(manifest);
    await writeFirmware(loader, files, eraseAll, (percent) => {
      progress.value = percent;
      message(`Firmware wird installiert … ${percent} %. USB-Kabel angeschlossen lassen.`);
    }, () => message('Alle Gerätedaten werden gelöscht … USB-Kabel angeschlossen lassen.'));
    progress.value = 100;
    let resetFailed = false;
    try { await loader.after('hard_reset'); }
    catch { resetFailed = true; }
    await closePort();
    message(resetFailed ? 'Firmware installiert. Bitte RESET drücken oder das USB-Kabel neu verbinden.' :
      'Firmware installiert. Die Ampel startet neu. Falls nötig RESET drücken.');
  } catch (error) {
    await closePort();
    message(`Installation fehlgeschlagen: ${error.message}. Bitte erneut verbinden und installieren.`, true);
  } finally {
    busy = false;
    buttons();
  }
}

async function eraseDevice() {
  if (busy || !loader) return;
  if (!window.confirm('GESAMTEN Flash löschen? Firmware, WLAN-Daten, Zugangsdaten und Branding werden unwiderruflich entfernt. Danach muss eine Firmware neu installiert werden.')) return;
  busy = true;
  progress.hidden = true;
  buttons();
  try {
    await checkDevice(loader);
    message('Gesamter Flash wird gelöscht … USB-Kabel angeschlossen lassen.');
    await loader.eraseFlash();
    message('Gerät vollständig gelöscht. Du kannst jetzt Firmware installieren oder die Verbindung trennen.');
  } catch (error) {
    await closePort();
    message(`Löschen fehlgeschlagen: ${error.message}. Bitte erneut verbinden.`, true);
  } finally {
    busy = false;
    buttons();
  }
}

async function disconnect() {
  if (busy) return;
  busy = true;
  buttons();
  try {
    if (loader) { try { await loader.after('hard_reset'); } catch {} }
    await closePort();
    message('Verbindung getrennt. Bei Bedarf RESET an der Ampel drücken.');
  } finally {
    busy = false;
    buttons();
  }
}

async function start() {
  if (!supported) {
    version.textContent = 'USB-Flashen nicht verfügbar';
    message('Bitte diese Seite über HTTPS in Chrome oder Edge auf einem Computer öffnen.', true);
    return;
  }
  try {
    const manifestUrl = new URL('install-manifest.json', window.location.href);
    const response = await fetch(manifestUrl, {cache: 'no-store', signal: AbortSignal.timeout(15000)});
    if (!response.ok) throw new Error('Keine veröffentlichte Firmware gefunden. Bitte später erneut versuchen.');
    manifest = validateManifest(await response.json(), manifestUrl);
    version.textContent = manifest.version;
    release.href = manifest.release_url;
    message('USB-Kabel anschließen und Ampel verbinden.');
  } catch (error) {
    manifest = undefined;
    version.textContent = 'noch nicht verfügbar';
    message(error.message, true);
  } finally {
    // A missing download must not prevent the independent erase function.
    buttons();
  }
}

connectButton.addEventListener('click', connect);
flashButton.addEventListener('click', flash);
disconnectButton.addEventListener('click', disconnect);
eraseButton.addEventListener('click', eraseDevice);
window.addEventListener('beforeunload', (event) => {
  if (busy) { event.preventDefault(); event.returnValue = ''; }
});
start();
