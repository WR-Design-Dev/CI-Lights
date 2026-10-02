const PARTS = [
  {offset: 0, name: 'bootloader.bin', limit: 0x8000},
  {offset: 0x8000, name: 'partition-table.bin', limit: 0x1000},
  {offset: 0x11000, name: 'ota_data_initial.bin', limit: 0x2000},
  {offset: 0x20000, name: 'CI-Lights.bin', limit: 0x1C0000},
];

export function validateManifest(manifest, manifestUrl) {
  if (manifest.schema !== 1 || manifest.name !== 'CI-Lights' || manifest.layout !== '4mb-ota-v1' ||
      !/^1\.0\.\d+$/.test(manifest.version) || manifest.builds?.length !== 1 ||
      manifest.builds[0].chipFamily !== 'ESP32-S3' || manifest.builds[0].parts?.length !== 4 ||
      manifest.release_url !== `https://github.com/WR-Design-Dev/CI-Lights/releases/tag/v${manifest.version}`) {
    throw new Error('Die Firmware-Informationen passen nicht zu dieser Ampel.');
  }
  const parts = manifest.builds[0].parts.map((part, index) => {
    const expected = PARTS[index];
    const url = new URL(part.path, manifestUrl);
    if (part.path !== `firmware/v${manifest.version}/${expected.name}` ||
        url.origin !== manifestUrl.origin || part.offset !== expected.offset ||
        !Number.isSafeInteger(part.size) || part.size <= 0 || part.size > expected.limit ||
        !/^[0-9a-f]{64}$/.test(part.sha256) ||
        (index === 2 && part.size !== 8192) || (index === 3 && part.size % 4096 !== 0)) {
      throw new Error('Ungültige Firmware-Datei oder Flash-Adresse.');
    }
    return {...part, url: url.href};
  });
  return {...manifest, parts};
}

export async function downloadFiles(manifest) {
  const files = [];
  for (const part of manifest.parts) {
    const response = await fetch(part.url, {cache: 'no-store', signal: AbortSignal.timeout(30000)});
    if (!response.ok) throw new Error(`Firmware-Datei konnte nicht geladen werden (HTTP ${response.status}).`);
    const data = new Uint8Array(await response.arrayBuffer());
    const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', data));
    const sha256 = Array.from(digest, byte => byte.toString(16).padStart(2, '0')).join('');
    if (data.length !== part.size || sha256 !== part.sha256) {
      throw new Error('Firmware-Prüfsumme oder Dateigröße stimmt nicht. Es wurde nichts geschrieben oder gelöscht.');
    }
    files.push({data, address: part.offset});
  }
  return files;
}

export async function checkDevice(loader) {
  if (loader.chip?.CHIP_NAME !== 'ESP32-S3') throw new Error('Diese Firmware benötigt einen ESP32-S3.');
  // Do not use detectFlashSize(): it defaults to 4MB for unknown chips.
  const id = await loader.readFlashId();
  const size = loader.DETECTED_FLASH_SIZES[(id >>> 16) & 0xff];
  if (!size || loader.flashSizeBytes(size) < 4 * 1024 * 1024) {
    throw new Error('Mindestens 4 MiB Flash müssen sicher erkannt werden.');
  }
  return size;
}

export async function writeFirmware(loader, files, eraseAll, onProgress, onErase) {
  // Call only after all downloads and hashes passed. Limit writes to the four
  // package regions; NVS and branding are outside every erased flash sector.
  await checkDevice(loader);
  if (eraseAll) {
    onErase();
    await loader.eraseFlash();
  }
  const weights = files.map(file => file.data.length);
  const total = weights.reduce((sum, weight) => sum + weight, 0);
  await loader.writeFlash({fileArray: files, flashSize: 'keep', flashMode: 'keep',
    flashFreq: 'keep', eraseAll: false, compress: true,
    reportProgress(index, written, size) {
      const previous = weights.slice(0, index).reduce((sum, weight) => sum + weight, 0);
      onProgress(Math.min(100, Math.floor(100 * (previous + weights[index] * written / size) / total)));
    }});
}
