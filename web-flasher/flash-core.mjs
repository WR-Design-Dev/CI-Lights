import {localizedError} from './language.mjs';
import {requireUnsecuredDevice} from './security-state.mjs';
import {md5} from './md5.mjs';

const PARTS = [
  {offset: 0, name: 'bootloader.bin', limit: 0x8000},
  {offset: 0x8000, name: 'partition-table.bin', limit: 0x1000},
  {offset: 0x11000, name: 'ota_data_initial.bin', limit: 0x2000},
  {offset: 0x20000, name: 'CI-Lights.bin', limit: 0x1C0000},
];

export function validateManifest(manifest, manifestUrl, expectedProfile = 'standard') {
  const secure = expectedProfile === 'secure';
  const profile = manifest.profile || 'standard';
  const expectedLayout = secure ? '4mb-secure-v1' : '4mb-ota-v1';
  const expectedName = secure ? 'CI-Lights-Secure' : 'CI-Lights';
  const repository = manifest.repository || 'WR-Design-Dev/CI-Lights';
  const names = secure ? ['bootloader-secure.bin', 'partition-table-secure.bin',
    'ota_data_initial-secure.bin', 'CI-Lights-Secure.bin'] : PARTS.map(part => part.name);
  if (!/^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/.test(repository) ||
      ![1, 2].includes(manifest.schema) || profile !== expectedProfile ||
      manifest.name !== expectedName || manifest.layout !== expectedLayout ||
      (secure && (manifest.schema !== 2 || manifest.erase_required !== true || manifest.ota_only !== true ||
        manifest.bootloader_signatures !== 3)) ||
      !/^1\.0\.\d+$/.test(manifest.version) || manifest.builds?.length !== 1 ||
      manifest.builds[0].chipFamily !== 'ESP32-S3' || manifest.builds[0].parts?.length !== 4 ||
      manifest.release_url !== `https://github.com/${repository}/releases/tag/v${manifest.version}`) {
    throw localizedError('manifestMismatch');
  }
  const parts = manifest.builds[0].parts.map((part, index) => {
    const expected = PARTS[index];
    const offset = secure ? [0, 0x10000, 0x18000, 0x20000][index] : expected.offset;
    const limit = secure && index === 0 ? 0x10000 : expected.limit;
    const url = new URL(part.path, manifestUrl);
    if (part.path !== `firmware/v${manifest.version}/${secure ? 'secure/' : ''}${names[index]}` ||
        url.origin !== manifestUrl.origin || part.offset !== offset ||
        !Number.isSafeInteger(part.size) || part.size <= 0 || part.size > limit ||
        !/^[0-9a-f]{64}$/.test(part.sha256) ||
        (index === 2 && part.size !== 8192) || (index === 3 && part.size % 4096 !== 0)) {
      throw localizedError('invalidPart');
    }
    return {...part, url: url.href};
  });
  return {...manifest, profile, parts};
}

export async function downloadFiles(manifest) {
  const files = [];
  for (const part of manifest.parts) {
    const response = await fetch(part.url, {cache: 'no-store', signal: AbortSignal.timeout(30000)});
    if (!response.ok) throw localizedError('downloadFailed', {status: response.status});
    const data = new Uint8Array(await response.arrayBuffer());
    const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', data));
    const sha256 = Array.from(digest, byte => byte.toString(16).padStart(2, '0')).join('');
    if (data.length !== part.size || sha256 !== part.sha256) {
      throw localizedError('checksumMismatch');
    }
    files.push({data, address: part.offset});
  }
  return files;
}

export async function checkDevice(loader, profile = 'standard') {
  if (loader.chip?.CHIP_NAME !== 'ESP32-S3') throw localizedError('wrongChip');
  if (typeof loader.chip.getMajorChipVersion !== 'function' ||
      typeof loader.chip.getMinorChipVersion !== 'function') throw localizedError('securityUnknown');
  const major = await loader.chip.getMajorChipVersion(loader);
  const minor = await loader.chip.getMinorChipVersion(loader);
  if (!Number.isInteger(major) || !Number.isInteger(minor) || major < 0 || major > 3 || minor < 0 || minor > 15) {
    throw localizedError('securityUnknown');
  }
  // Do not use detectFlashSize(): it defaults to 4MB for unknown chips.
  const id = await loader.readFlashId();
  const size = loader.DETECTED_FLASH_SIZES[(id >>> 16) & 0xff];
  if (!size || loader.flashSizeBytes(size) < 4 * 1024 * 1024) {
    throw localizedError('insufficientFlash');
  }
  if (profile === 'secure') {
    // The immutable bootloader is approved for the ESP32-S3-Zero's FH4R2 chip.
    // Refuse other packages before permanently removing the recovery path.
    if (typeof loader.chip.getFlashCap !== 'function' || typeof loader.chip.getPsramCap !== 'function' ||
        await loader.chip.getFlashCap(loader) !== 2 || await loader.chip.getPsramCap(loader) !== 2) {
      throw localizedError('unsupportedSecureBoard');
    }
  }
  await requireUnsecuredDevice(loader);
  return size;
}

export async function writeFirmware(loader, files, eraseAll, onProgress, onErase, profile = 'standard') {
  // Call only after all downloads and hashes passed. Limit writes to the four
  // package regions; NVS and branding are outside every erased flash sector.
  await checkDevice(loader, profile);
  if (!['standard', 'secure'].includes(profile)) throw localizedError('manifestMismatch');
  if (profile === 'secure' && !eraseAll) throw localizedError('secureEraseRequired');
  if (eraseAll) {
    onErase();
    await loader.eraseFlash();
  }
  // No reset until all verified files have been written. Secure bootloader last.
  const ordered = profile === 'secure' ? [...files.filter(file => file.address !== 0),
    ...files.filter(file => file.address === 0)] : files;
  const weights = ordered.map(file => file.data.length);
  const total = weights.reduce((sum, weight) => sum + weight, 0);
  await loader.writeFlash({fileArray: ordered, flashSize: 'keep', flashMode: 'keep',
    flashFreq: 'keep', eraseAll: false, compress: true,
    calculateMD5Hash: md5,
    reportProgress(index, written, size) {
      const previous = weights.slice(0, index).reduce((sum, weight) => sum + weight, 0);
      onProgress(Math.min(100, Math.floor(100 * (previous + weights[index] * written / size) / total)));
    }});
}
