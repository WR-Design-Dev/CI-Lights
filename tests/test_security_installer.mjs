import assert from 'node:assert/strict';
import {requireUnsecuredDevice} from '../web-flasher/security-state.mjs';
import {checkDevice, validateManifest, writeFirmware} from '../web-flasher/flash-core.mjs';
import {md5} from '../web-flasher/md5.mjs';
import {createHash} from 'node:crypto';
import fs from 'node:fs';
import vm from 'node:vm';

for (const bytes of [new Uint8Array(), new TextEncoder().encode('abc'),
  ...[55, 56, 63, 64, 65, 1183744].map(length => Uint8Array.from({length}, (_, i) => i % 251))]) {
  assert.equal(md5(bytes), createHash('md5').update(bytes).digest('hex'));
}

const base = 0x60007000;
function device(registers = {}, caps = [2, 2]) {
  const calls = [];
  return {
    calls,
    chip: {CHIP_NAME: 'ESP32-S3', getMajorChipVersion: async () => 0, getMinorChipVersion: async () => 3,
      getFlashCap: async () => caps[0], getPsramCap: async () => caps[1]},
    DETECTED_FLASH_SIZES: {0x16: '4MB'}, readFlashId: async () => 0x162046,
    flashSizeBytes: () => 4 * 1024 * 1024,
    readReg: async address => { if (registers[address] instanceof Error) throw registers[address]; return registers[address] ?? 0; },
    eraseFlash: async () => calls.push('erase'), writeFlash: async options => calls.push(options),
  };
}
assert.deepEqual(await requireUnsecuredDevice(device()), {state: 'unsecured'});
// Factory calibration write protection may exist on an otherwise unused chip.
assert.equal(await checkDevice(device({[base + 0x2c]: 1 << 20}), 'secure'), '4MB');
const files = [0, 0x10000, 0x18000, 0x20000].map(address => ({address, data: new Uint8Array(32)}));
for (const [offset, value, reason] of [
  [0x38, 1 << 20, 'deviceSecured'], [0x34, 1 << 18, 'deviceSecured'], // Secure Boot, crypt counter
  [0x3c, 1, 'deviceSecured'], // ROM download disabled
  [0x2c, 1 << 23, 'securityPartial'], [0x30, 1, 'securityPartial'], // key write/read protection
  [0x34, 1 << 24, 'securityPartial'], [0x38, 1, 'securityPartial'], // key purpose
  [0x34, 1 << 21, 'securityPartial'], [0x38, 1 << 21, 'securityPartial'], // revocation/aggressive
  [0x30, 1 << 19, 'securityPartial'], [0x38, 1 << 22, 'securityPartial'], // JTAG
  [0x3c, (1 << 31), 'securityPartial'], // signed JS register value still handled
  [0x9c, 1, 'securityPartial'], [0x158, 1, 'securityPartial'], // first/last key register
  [0x190, 1, 'securityUnknown'], [0x1c4, 1, 'securityUnknown'], // ECC/repeat errors
  [0x2c, NaN, 'securityUnknown'], [0x2c, new Error('serial closed'), 'securityUnknown'],
]) {
  const loader = device({[base + offset]: value});
  await assert.rejects(writeFirmware(loader, files, true, () => {}, () => {}, 'secure'),
    error => error.translationKey === reason, `Unexpected result at ${offset.toString(16)}`);
  assert.equal(loader.calls.length, 0, 'A failed preflight must not erase or write');
}
const unknown = device();
delete unknown.readReg;
await assert.rejects(requireUnsecuredDevice(unknown), error => error.translationKey === 'securityUnknown');
for (const caps of [[2, 0], [2, 1], [1, 2]]) {
  const loader = device({}, caps);
  await assert.rejects(writeFirmware(loader, files, true, () => {}, () => {}, 'secure'),
    error => error.translationKey === 'unsupportedSecureBoard');
  assert.equal(loader.calls.length, 0);
}
let loader = device();
await assert.rejects(writeFirmware(loader, files, false, () => {}, () => {}, 'secure'),
  error => error.translationKey === 'secureEraseRequired');
assert.equal(loader.calls.length, 0);
loader = device();
await writeFirmware(loader, files, true, () => {}, () => {}, 'secure');
assert.equal(loader.calls[0], 'erase');
assert.deepEqual(loader.calls[1].fileArray.map(file => file.address), [0x10000, 0x18000, 0x20000, 0]);
assert.equal(loader.calls[1].flashMode, 'keep');
assert.equal(loader.calls[1].eraseAll, false);
assert.equal(loader.calls[1].calculateMD5Hash, md5, 'Flash readback verification must be enabled');

const url = new URL('https://example.github.io/CI-Lights/install-manifest-secure.json');
const manifest = {schema: 2, profile: 'secure', name: 'CI-Lights-Secure', layout: '4mb-secure-v1',
  version: '1.0.100', repository: 'example/CI-Lights', erase_required: true, ota_only: true, bootloader_signatures: 3,
  release_url: 'https://github.com/example/CI-Lights/releases/tag/v1.0.100',
  builds: [{chipFamily: 'ESP32-S3', parts: files.map((file, i) => ({offset: file.address,
    path: 'firmware/v1.0.100/secure/' + ['bootloader-secure.bin', 'partition-table-secure.bin', 'ota_data_initial-secure.bin', 'CI-Lights-Secure.bin'][i],
    size: [40960, 3072, 8192, 0x121000][i], sha256: 'a'.repeat(64)}))}]};
assert.equal(validateManifest(manifest, url, 'secure').profile, 'secure');
assert.throws(() => validateManifest(manifest, url, 'standard'));
for (const patch of [{profile: 'standard'}, {layout: '4mb-ota-v1'}, {schema: 1}, {erase_required: false},
  {bootloader_signatures: 1}, {ota_only: false}, {repository: '../wrong'}, {name: 'CI-Lights'}]) {
  assert.throws(() => validateManifest({...manifest, ...patch}, url, 'secure'));
}
for (const patch of [{offset: 0x8000}, {path: 'https://elsewhere.test/bootloader-secure.bin'}, {size: 0x10001}]) {
  const changed = structuredClone(manifest);
  Object.assign(changed.builds[0].parts[0], patch);
  assert.throws(() => validateManifest(changed, url, 'secure'));
}
// Opening device administration must not request protected status before login.
const elements = [];
let statusRequests = 0;
const ui = vm.createContext({AbortSignal,
  document: {readyState: 'complete', querySelector: () => ({append() {}}),
    createElement() { const element = {append() {}, addEventListener(name, callback) { this[name] = callback; }}; elements.push(element); return element; }},
  uiI18n: {t: text => text, onChange() {}},
  fetch: async () => { statusRequests++; return {ok: true, json: async () => ({profile: 'secure', ready: true,
    secure_boot: true, flash_encryption: true, nvs_encryption: true, rom_download_disabled: true, revoked_keys: 0, signing_key: 0})}; },
});
vm.runInContext(fs.readFileSync(new URL('../main/assets/security.js', import.meta.url), 'utf8'), ui);
assert.equal(statusRequests, 0);
elements[0].open = true;
await elements[0].toggle();
assert.equal(statusRequests, 1);
assert.match(elements[2].textContent, /Profil: secure/);
assert.match(elements[2].textContent, /Prüfung: bestanden/);
console.log('Secure installer checks passed: irreversible/partial/unknown states refused before erase or write, board compatibility, separate manifests, mandatory erase and bootloader last.');
