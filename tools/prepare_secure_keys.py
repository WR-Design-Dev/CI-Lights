"""Manage K0/K1/K2 without displaying private material or changing existing keys."""
import argparse
import ctypes
import getpass
import json
import os
import subprocess
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa
try:
    from .security_images import key_identity
except ImportError:
    from security_images import key_identity

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_DIRECTORY = ROOT / 'secrets'


class Blob(ctypes.Structure):
    _fields_ = [('size', ctypes.c_uint32), ('data', ctypes.POINTER(ctypes.c_ubyte))]


def dpapi(data, decrypt=False):
    if os.name != 'nt':
        raise ValueError('The private vault uses Windows DPAPI; use an exported encrypted backup on other systems')
    buffer = ctypes.create_string_buffer(data)
    source = Blob(len(data), ctypes.cast(buffer, ctypes.POINTER(ctypes.c_ubyte)))
    result = Blob()
    crypt = ctypes.WinDLL('crypt32', use_last_error=True)
    method = crypt.CryptUnprotectData if decrypt else crypt.CryptProtectData
    if not method(ctypes.byref(source), None, None, None, None, 1, ctypes.byref(result)):
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        return ctypes.string_at(result.data, result.size)
    finally:
        kernel = ctypes.WinDLL('kernel32')
        kernel.LocalFree.argtypes = [ctypes.c_void_p]
        kernel.LocalFree(ctypes.cast(result.data, ctypes.c_void_p))


def load_key(directory, index):
    return serialization.load_pem_private_key(dpapi((directory / f'K{index}.pem.dpapi').read_bytes(), True), None)


def pem(key):
    return key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                             serialization.NoEncryption())


def create(directory):
    registry = ROOT / 'security' / 'secure-keyring.json'
    expected = json.loads(registry.read_text()) if registry.exists() else None
    directory.mkdir(parents=True, mode=0o700, exist_ok=True)
    records = []
    for index in range(3):
        path = directory / f'K{index}.pem.dpapi'
        if path.exists():
            key = load_key(directory, index)
        elif expected:
            raise ValueError(f'K{index} is missing. Restore its backup; existing trust must never be replaced')
        else:
            key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
            with path.open('xb') as output:
                output.write(dpapi(pem(key)))
        subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                        str(ROOT / 'tools/protect_key_file.ps1'), str(path)], check=True)
        identity = {'index': index, **key_identity(key)}
        if expected and identity != expected['keys'][index]:
            raise ValueError(f'K{index} differs from the registered key')
        if key_identity(load_key(directory, index)) != key_identity(key):
            raise ValueError('Vault restoration check failed')
        records.append(identity)
        public_path = ROOT / 'security' / f'K{index}.pub.pem'
        public_path.parent.mkdir(exist_ok=True)
        public_bytes = key.public_key().public_bytes(serialization.Encoding.PEM,
                                                     serialization.PublicFormat.SubjectPublicKeyInfo)
        if public_path.exists() and public_path.read_bytes() != public_bytes:
            raise ValueError('Refusing to replace a registered public key')
        public_path.write_bytes(public_bytes)
        print(f'K{index}: encrypted vault file verified; public fingerprint {identity["spki_sha256"]}')
    metadata = {'schema': 1, 'profile': 'secure', 'active_key': 0, 'keys': records}
    if expected and expected['keys'] != records:
        raise ValueError('Existing key registry cannot be changed automatically')
    if not expected:
        registry.write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=['init', 'stage', 'export', 'restore', 'upload', 'development', 'from-env'])
    parser.add_argument('--directory', type=Path, default=DEFAULT_DIRECTORY)
    parser.add_argument('--index', type=int, choices=[0, 1, 2])
    parser.add_argument('--output', type=Path)
    parser.add_argument('--input', type=Path, help='Portable password-encrypted PKCS8 backup to restore')
    parser.add_argument('--gh', default='gh')
    args = parser.parse_args()
    if args.operation == 'init':
        create(args.directory)
        return
    if args.operation == 'development':
        directory = ROOT / 'secrets'
        directory.mkdir(mode=0o700, exist_ok=True)
        records = []
        for index in range(3):
            key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
            path = directory / f'development-K{index}.pem'
            with path.open('xb') as output:
                output.write(pem(key))
            records.append({'index': index, **key_identity(key)})
        (directory / 'development-keyring.json').write_text(json.dumps({'schema': 1, 'profile': 'secure',
                                                                       'active_key': 0, 'keys': records}))
        (directory / 'secure_signing_key.pem').write_bytes((directory / 'development-K0.pem').read_bytes())
        return
    registry = json.loads((ROOT / 'security/secure-keyring.json').read_text())
    if args.index is None:
        args.index = registry['active_key']
    if args.operation == 'restore':
        if args.input is None:
            parser.error('--input is required')
        key = serialization.load_pem_private_key(args.input.read_bytes(),
                                                 getpass.getpass('Backup password: ').encode())
    elif args.operation == 'from-env':
        material = os.environ.get('SECURE_SIGNING_KEY_PEM', '').encode()
        if not material:
            raise ValueError('Protected environment secret SECURE_SIGNING_KEY_PEM is missing')
        key = serialization.load_pem_private_key(material, None)
    else:
        key = load_key(args.directory, args.index)
    registered = json.loads((ROOT / 'security/secure-keyring.json').read_text())['keys'][args.index]
    if key_identity(key) != {name: registered[name] for name in ('spki_sha256', 'efuse_digest')}:
        raise ValueError('Private key does not match public registry')
    if args.operation == 'restore':
        target = args.directory / f'K{args.index}.pem.dpapi'
        args.directory.mkdir(parents=True, mode=0o700, exist_ok=True)
        with target.open('xb') as output:
            output.write(dpapi(pem(key)))
        subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                        str(ROOT / 'tools/protect_key_file.ps1'), str(target)], check=True)
        if key_identity(load_key(args.directory, args.index)) != key_identity(key):
            raise ValueError('Restored vault identity differs')
        print(f'K{args.index}: backup restored and identity verified; private contents not displayed')
        return
    if args.operation == 'upload':
        if args.index != registry['active_key']:
            raise ValueError('Activate the public release key policy before uploading a reserve')
        from repository_config import repository_name
        subprocess.run([args.gh, 'secret', 'set', 'SECURE_SIGNING_KEY_PEM', '--env', 'secure-firmware-signing',
                        '--repo', repository_name()], input=pem(key), check=True)
        return
    if args.output is None:
        parser.error('--output is required')
    args.output.parent.mkdir(parents=True, mode=0o700, exist_ok=True)
    if args.operation == 'export':
        password = getpass.getpass('Password for the portable encrypted backup: ')
        if len(password) < 16 or password != getpass.getpass('Repeat password: '):
            raise ValueError('Use a matching password of at least 16 characters')
        material = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                     serialization.BestAvailableEncryption(password.encode()))
        restored = serialization.load_pem_private_key(material, password.encode())
        if key_identity(restored) != key_identity(key):
            raise ValueError('Backup restoration check failed')
    else:
        material = pem(key)
    with os.fdopen(os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'wb') as output:
        output.write(material)
    print(f'K{args.index}: {args.operation} complete; private contents not displayed')


if __name__ == '__main__':
    main()
