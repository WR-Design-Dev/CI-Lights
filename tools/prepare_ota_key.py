"""Prepare the OTA signing key without ever printing its private contents."""

import argparse
import hashlib
import os
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--generate', action='store_true', help='Generate the first local key, preserving existing keys')
    source.add_argument('--from-env', action='store_true', help='Read OTA_SIGNING_KEY_PEM, used by GitHub Actions')
    source.add_argument('--development', action='store_true', help='Use a disposable key in a clean non-production CI checkout')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    key_path = root / 'secrets' / 'ota_signing_key.pem'
    fingerprint_path = root / 'ota_signing_public_key.sha256'
    if args.development:
        if key_path.exists():
            parser.error('Development builds require a clean checkout without a production key')
        key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
        pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                serialization.NoEncryption())
    elif args.from_env:
        pem = os.environ.get('OTA_SIGNING_KEY_PEM', '').encode('utf-8')
        if not pem:
            parser.error('GitHub secret OTA_SIGNING_KEY_PEM is missing')
        key = serialization.load_pem_private_key(pem, password=None)
    elif key_path.exists():
        pem = key_path.read_bytes()
        key = serialization.load_pem_private_key(pem, password=None)
    else:
        key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
        pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                serialization.NoEncryption())
    if not isinstance(key, rsa.RSAPrivateKey) or key.key_size != 3072:
        parser.error('OTA requires the existing RSA-3072 signing key')
    public = key.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    fingerprint = hashlib.sha256(public).hexdigest()
    if args.development:
        print('Development build: disposable signing key, not accepted by production devices')
    elif fingerprint_path.exists():
        if fingerprint_path.read_text(encoding='ascii').strip() != fingerprint:
            parser.error('Signing key differs from the registered OTA key; refusing key rotation')
    elif args.from_env:
        parser.error('Public key fingerprint is missing from the source repository')
    else:
        fingerprint_path.write_text(fingerprint + '\n', encoding='ascii')
    key_path.parent.mkdir(mode=0o700, exist_ok=True)
    if key_path.exists():
        existing = serialization.load_pem_private_key(key_path.read_bytes(), password=None)
        if existing.private_numbers() != key.private_numbers():
            parser.error('Refusing to overwrite an existing signing key')
    else:
        with os.fdopen(os.open(key_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'wb') as output:
            output.write(pem)
    print(f'OTA signing key ready: {key_path}')
    print(f'Public key SHA-256: {fingerprint}')


if __name__ == '__main__':
    main()
