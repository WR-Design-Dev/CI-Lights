import {validateManifest, downloadFiles, checkDevice, writeFirmware} from './flash-core.mjs';
import {initLanguage, t, localizedError, onLanguageChange} from './language.mjs';

initLanguage();

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
const lightPreview = document.querySelector('#traffic-light-preview');
const lightPhases = [
  {lights: ['red'], duration: 3000},
  {lights: ['yellow'], duration: 900},
  {lights: ['green'], duration: 3000},
  {lights: ['yellow'], duration: 900},
];
let lightTimer;
function lightPreviewReady() {
  clearTimeout(lightTimer);
  const lamps = ['red', 'yellow', 'green'].map(color =>
    lightPreview.contentDocument?.getElementById(color));
  if (lamps.some(lamp => !lamp)) return;
  let phaseIndex = 0;
  function nextPhase() {
    const phase = lightPhases[phaseIndex];
    for (const lamp of lamps) {
      lamp.classList.toggle('is-on', phase.lights.includes(lamp.id));
    }
    if (!window.matchMedia('(prefers-reduced-motion: reduce)').matches) {
      phaseIndex = (phaseIndex + 1) % lightPhases.length;
      lightTimer = setTimeout(nextPhase, phase.duration);
    }
  }
  nextPhase();
}
if (lightPreview) {
  lightPreview.addEventListener('load', lightPreviewReady);
  window.matchMedia('(prefers-reduced-motion: reduce)').addEventListener('change', lightPreviewReady);
  lightPreviewReady();
}
let manifest;
let loader;
let transport;
let busy = false;
const supported = window.isSecureContext && Boolean(navigator.serial);
let currentMessage = {key: 'checkingFiles', values: {}, error: false};
let versionLabel = 'loading';

function message(key, values = {}, error = false) {
  currentMessage = {key, values, error};
  status.textContent = t(key, values);
  status.dataset.error = String(error);
}

function renderVersion() {
  version.textContent = manifest ? manifest.version : t(versionLabel);
}

onLanguageChange(() => {
  renderVersion();
  message(currentMessage.key, currentMessage.values, currentMessage.error);
});

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
    message('loadingTool');
    const {ESPLoader, Transport} = await import(ESPTOOL_URL);
    transport = new Transport(port, false);
    const candidate = new ESPLoader({transport, baudrate: 115200, debugLogging: false,
      terminal: {clean() {}, write() {}, writeLine() {}}});
    message('connecting');
    await candidate.main();
    const size = await checkDevice(candidate);
    loader = candidate;
    message('connected', {size});
  } catch (error) {
    await closePort();
    message(error.name === 'NotFoundError' ? 'noPort' : 'connectionFailed', {error}, true);
  } finally {
    busy = false;
    buttons();
  }
}

async function flash() {
  if (busy || !loader || !manifest) return;
  const eraseAll = erase.checked;
  if (!window.confirm(t(eraseAll ? 'confirmInstallErase' : 'confirmInstallKeep', {version: manifest.version}))) return;
  busy = true;
  progress.hidden = false;
  progress.value = 0;
  buttons();
  try {
    message('downloading');
    const files = await downloadFiles(manifest);
    await writeFirmware(loader, files, eraseAll, (percent) => {
      progress.value = percent;
      message('installing', {percent});
    }, () => message('erasingData'));
    progress.value = 100;
    let resetFailed = false;
    try { await loader.after('hard_reset'); }
    catch { resetFailed = true; }
    await closePort();
    message(resetFailed ? 'installedManualReset' : 'installed');
  } catch (error) {
    await closePort();
    message('installFailed', {error}, true);
  } finally {
    busy = false;
    buttons();
  }
}

async function eraseDevice() {
  if (busy || !loader) return;
  if (!window.confirm(t('confirmErase'))) return;
  busy = true;
  progress.hidden = true;
  buttons();
  try {
    await checkDevice(loader);
    message('erasingFlash');
    await loader.eraseFlash();
    message('erased');
  } catch (error) {
    await closePort();
    message('eraseFailed', {error}, true);
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
    message('disconnected');
  } finally {
    busy = false;
    buttons();
  }
}

async function start() {
  if (!supported) {
    versionLabel = 'unsupportedVersion';
    renderVersion();
    message('unsupported', {}, true);
    return;
  }
  try {
    const manifestUrl = new URL('install-manifest.json', window.location.href);
    const response = await fetch(manifestUrl, {cache: 'no-store', signal: AbortSignal.timeout(15000)});
    if (!response.ok) throw localizedError('noFirmware');
    manifest = validateManifest(await response.json(), manifestUrl);
    renderVersion();
    release.href = manifest.release_url;
    message('ready');
  } catch (error) {
    manifest = undefined;
    versionLabel = 'unavailable';
    renderVersion();
    message('installFailed', {error}, true);
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
