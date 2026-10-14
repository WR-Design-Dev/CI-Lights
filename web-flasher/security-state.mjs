import {localizedError} from './language.mjs';

// ESP32-S3 BLOCK0 and key read registers, ESP-IDF 6.1 / esptool esp32s3 definitions.
// These are READ registers. No eFuse programming command is sent by the browser.
const BASE = 0x60007000;
const CONTROL = BASE + 0x2c;
const KEY0 = BASE + 0x9c;
const protectedBits = [0, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 18, 19,
  23, 24, 25, 26, 27, 28];
const requiredWritable = protectedBits.reduce((mask, bit) => (mask | (1 << bit)) >>> 0, 0);

async function readWord(loader, address) {
  const value = await loader.readReg(address);
  if (!Number.isInteger(value) || value < -0x80000000 || value > 0xffffffff) {
    throw localizedError('securityUnknown');
  }
  return value >>> 0;
}

export async function requireUnsecuredDevice(loader) {
  if (typeof loader.readReg !== 'function') throw localizedError('securityUnknown');
  try {
    const words = [];
    for (let i = 0; i < 6; i++) words.push(await readWord(loader, CONTROL + i * 4));
    const secureBoot = Boolean(words[3] & (1 << 20));
    const cryptCounter = (words[2] >>> 18) & 7;
    if (secureBoot || cryptCounter !== 0 || (words[4] & 1)) {
      throw localizedError('deviceSecured');
    }
    const purposes = [words[2] >>> 24 & 15, words[2] >>> 28 & 15,
      words[3] & 15, words[3] >>> 4 & 15, words[3] >>> 8 & 15, words[3] >>> 12 & 15];
    if ((words[0] & requiredWritable) || (words[1] & 0x7f) ||
        (words[2] & (7 << 21)) || (words[3] & (1 << 21)) ||
        (words[1] & ((1 << 19) | (1 << 20))) ||
        (words[3] & ((1 << 22) | (1 << 23))) ||
        (words[4] & ((1 << 4) | (1 << 5) | (1 << 31))) || purposes.some(Boolean)) {
      throw localizedError('securityPartial');
    }
    for (let block = 0; block < 6; block++) {
      for (let word = 0; word < 8; word++) {
        if (await readWord(loader, KEY0 + block * 32 + word * 4)) {
          throw localizedError('securityPartial');
        }
      }
    }
    for (const offset of [0x17c, 0x180, 0x184, 0x188, 0x190, 0x1c0, 0x1c4]) {
      if (await readWord(loader, BASE + offset)) throw localizedError('securityUnknown');
    }
    return {state: 'unsecured'};
  } catch (error) {
    if (error?.translationKey) throw error;
    // Keep coded application errors as well as errors from the transport distinct.
    if (['deviceSecured', 'securityPartial', 'securityUnknown'].includes(error?.code)) throw error;
    throw localizedError('securityUnknown');
  }
}
