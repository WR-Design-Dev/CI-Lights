"""Generate public eFuse trust identities for a secure application build."""
import argparse
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--keyring', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
registry = json.loads(args.keyring.read_text())
if registry.get('schema') != 1 or registry.get('profile') != 'secure' or len(registry['keys']) != 3:
    raise ValueError('Invalid secure key registry')
rows = []
for index, key in enumerate(registry['keys']):
    digest = bytes.fromhex(key['efuse_digest'])
    if key['index'] != index or len(digest) != 32:
        raise ValueError('Invalid eFuse digest registry')
    rows.append('{' + ','.join(f'0x{byte:02x}' for byte in digest) + '}')
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text('#pragma once\n#include <stdint.h>\nstatic const uint8_t app_secure_digests[3][32] = {\n' +
                       ',\n'.join(rows) + '\n};\n', encoding='ascii')
