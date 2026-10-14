"""Initialize a fresh Windows fork with independent keys; never rotate an existing installation."""
import argparse
import json
import os
import re
import subprocess
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa
try:
    from .prepare_secure_keys import dpapi, load_key, pem
    from .security_images import key_identity
except ImportError:
    from prepare_secure_keys import dpapi, load_key, pem
    from security_images import key_identity

ROOT = Path(__file__).resolve().parent.parent
UPSTREAM = 'WR-Design-Dev/CI-Lights'


def validate_target(repository, remote, existing_repository, has_private_keys):
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository) or repository.lower() == UPSTREAM.lower():
        raise ValueError('Use an independent fork, never the upstream production repository')
    match = re.fullmatch(r'(?:https://github\.com/|git@github\.com:|ssh://git@github\.com/)([^/]+/[^/]+?)(?:\.git)?/?', remote)
    if not match or match[1].lower() != repository.lower():
        raise ValueError('origin must point to the requested fork')
    if existing_repository != UPSTREAM or has_private_keys:
        raise ValueError('Only a fresh fork without existing private keys may be initialized. Restore existing keys instead.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', required=True, help='The new fork owner/name')
    parser.add_argument('--initialize-new-device-trust', action='store_true', required=True,
                        help='Explicitly replace inherited public trust for new devices in this fork')
    args = parser.parse_args()
    if os.name != 'nt':
        raise ValueError('Run the first fork key setup on Windows; CI reads the staged GitHub secrets afterwards')
    remote = subprocess.check_output(['git', 'remote', 'get-url', 'origin'], cwd=ROOT, text=True).strip()
    directory = ROOT / 'secrets'
    validate_target(args.repository, remote, (ROOT / 'firmware_repository.txt').read_text().strip(),
                    directory.exists() and any(path.suffix in ('.pem', '.dpapi') for path in directory.iterdir()))
    directory.mkdir(exist_ok=True)
    keys = [rsa.generate_private_key(public_exponent=65537, key_size=3072) for _ in range(4)]
    for index, key in enumerate(keys[:3]):
        path = directory / f'K{index}.pem.dpapi'
        with path.open('xb') as output:
            output.write(dpapi(pem(key)))
        subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                        str(ROOT / 'tools/protect_key_file.ps1'), str(path)], check=True)
        if key_identity(load_key(directory, index)) != key_identity(key):
            raise ValueError('Fork key restoration check failed')
    standard = directory / 'ota_signing_key.pem'
    with os.fdopen(os.open(standard, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'wb') as output:
        output.write(pem(keys[3]))
    subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                    str(ROOT / 'tools/protect_key_file.ps1'), str(standard)], check=True)
    for index, key in enumerate(keys[:3]):
        (ROOT / 'security' / f'K{index}.pub.pem').write_bytes(key.public_key().public_bytes(
            serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
    (ROOT / 'security/secure-keyring.json').write_text(json.dumps({
        'schema': 1, 'profile': 'secure', 'active_key': 0,
        'keys': [{'index': index, **key_identity(key)} for index, key in enumerate(keys[:3])]
    }, indent=2) + '\n', encoding='utf-8')
    (ROOT / 'ota_signing_public_key.sha256').write_text(key_identity(keys[3])['spki_sha256'] + '\n', encoding='ascii')
    # These public upstream artifacts authorize different keys and cannot be reused.
    for name in ('secure-bootloader.bin', 'secure-bootloader.json'):
        (ROOT / 'security' / name).unlink(missing_ok=True)
    (ROOT / 'firmware_repository.txt').write_text(args.repository + '\n', encoding='ascii')
    print('Independent fork keys created and restored successfully; no private material displayed.')
    print('Next: build both profiles with --approve-bootloader, make encrypted offline backups, then configure both signing environments.')
    print('Existing upstream devices cannot install this fork firmware: their trust is different.')


if __name__ == '__main__':
    main()
