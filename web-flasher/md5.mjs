// esptool's flash readback checksum. Download authenticity uses SHA-256/signatures.
const constants = Array.from({length: 64}, (_, i) => Math.floor(Math.abs(Math.sin(i + 1)) * 0x100000000) >>> 0);
const shifts = [7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21];

export function md5(bytes) {
  const padded = new Uint8Array(Math.ceil((bytes.length + 9) / 64) * 64);
  padded.set(bytes);
  padded[bytes.length] = 0x80;
  const view = new DataView(padded.buffer);
  view.setUint32(padded.length - 8, (bytes.length * 8) >>> 0, true);
  view.setUint32(padded.length - 4, Math.floor(bytes.length / 0x20000000), true);
  const state = [0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476];
  for (let offset = 0; offset < padded.length; offset += 64) {
    let [a, b, c, d] = state;
    for (let i = 0; i < 64; i++) {
      let f, word;
      if (i < 16) { f = (b & c) | (~b & d); word = i; }
      else if (i < 32) { f = (d & b) | (~d & c); word = (5 * i + 1) % 16; }
      else if (i < 48) { f = b ^ c ^ d; word = (3 * i + 5) % 16; }
      else { f = c ^ (b | ~d); word = (7 * i) % 16; }
      const sum = (a + f + constants[i] + view.getUint32(offset + word * 4, true)) >>> 0;
      const shift = shifts[Math.floor(i / 16) * 4 + i % 4];
      const next = (b + ((sum << shift) | (sum >>> (32 - shift)))) >>> 0;
      a = d; d = c; c = b; b = next;
    }
    for (const [i, value] of [a, b, c, d].entries()) state[i] = (state[i] + value) >>> 0;
  }
  const result = new Uint8Array(16);
  const output = new DataView(result.buffer);
  state.forEach((value, i) => output.setUint32(i * 4, value, true));
  return Array.from(result, byte => byte.toString(16).padStart(2, '0')).join('');
}
