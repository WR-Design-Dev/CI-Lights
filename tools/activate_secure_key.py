"""Explicitly select a registered secure release key and signed revocation policy."""
import argparse
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--index', type=int, choices=[0, 1, 2], required=True)
parser.add_argument('--revoke-mask', type=int, required=True)
args = parser.parse_args()
if not 0 <= args.revoke_mask <= 7 or args.revoke_mask & (1 << args.index):
    parser.error('The active key must remain trusted')
path = Path(__file__).resolve().parent.parent / 'security/secure-keyring.json'
registry = json.loads(path.read_text())
if args.index < registry['active_key'] or registry.get('revoke_mask', 0) & ~args.revoke_mask:
    parser.error('Do not move backwards or remove an existing revocation policy')
registry['active_key'] = args.index
registry['revoke_mask'] = args.revoke_mask
path.write_text(json.dumps(registry, indent=2) + '\n', encoding='utf-8')
print(f'Public signed release policy selected: K{args.index}, revocation mask {args.revoke_mask}. No device eFuses changed.')
