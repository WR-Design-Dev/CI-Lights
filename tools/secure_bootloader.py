"""Approve a three-key bootloader offline, or verify an existing public approval."""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    from .prepare_secure_keys import DEFAULT_DIRECTORY, load_key, pem
    from .security_images import verified_signatures
except ImportError:
    from prepare_secure_keys import DEFAULT_DIRECTORY, load_key, pem
    from security_images import verified_signatures

ROOT = Path(__file__).resolve().parent.parent
SECURITY_CONFIG = ['IDF_TARGET', 'ESPTOOLPY_FLASHSIZE', 'ESPTOOLPY_FLASHMODE', 'ESPTOOLPY_FLASHFREQ',
                   'PARTITION_TABLE_OFFSET', 'SECURE_BOOT', 'SECURE_BOOT_V2_ENABLED',
                   'SECURE_SIGNED_APPS_RSA_SCHEME', 'SECURE_FLASH_ENC_ENABLED',
                   'SECURE_FLASH_ENCRYPTION_AES256', 'SECURE_FLASH_ENCRYPTION_MODE_RELEASE',
                   'SECURE_DISABLE_ROM_DL_MODE', 'SECURE_BOOT_ENABLE_AGGRESSIVE_KEY_REVOKE',
                   'SECURE_BOOT_ALLOW_UNUSED_DIGEST_SLOTS', 'SECURE_BOOT_V2_ALLOW_EFUSE_RD_DIS',
                   'SECURE_BOOT_ALLOW_JTAG', 'SECURE_BOOT_INSECURE', 'BOOTLOADER_LOG_LEVEL',
                   'BOOTLOADER_COMPILER_OPTIMIZATION_SIZE', 'EFUSE_VIRTUAL']


def verify_approval(build_dir, keyring=None):
    registry = json.loads((keyring or ROOT / 'security/secure-keyring.json').read_text())
    approval = json.loads((ROOT / 'security/secure-bootloader.json').read_text())
    image = ROOT / 'security/secure-bootloader.bin'
    config = json.loads((build_dir / 'config/sdkconfig.json').read_text())
    if approval['schema'] != 1 or approval['idf_version'] != 'v6.1' or approval['keys'] != registry['keys']:
        raise ValueError('Secure bootloader trust or IDF approval differs')
    if approval['config'] != {name: config.get(name, False) for name in SECURITY_CONFIG}:
        raise ValueError('Secure bootloader configuration changed; offline reapproval required')
    if hashlib.sha256(image.read_bytes()).hexdigest() != approval['sha256']:
        raise ValueError('Approved secure bootloader hash differs')
    identities = verified_signatures(image)
    if identities != [{name: key[name] for name in ('spki_sha256', 'efuse_digest')} for key in registry['keys']]:
        raise ValueError('Bootloader must be signed by K0, K1 and K2 in their fixed eFuse order')
    if image.stat().st_size > config['PARTITION_TABLE_OFFSET']:
        raise ValueError('Approved bootloader overlaps partition table')
    return image


def approve(build_dir, directory, development=False):
    if (ROOT / 'security/secure-bootloader.json').exists() and not development:
        verify_approval(build_dir)
        print('Existing offline bootloader approval reused; reserves not opened')
        return
    config = json.loads((build_dir / 'config/sdkconfig.json').read_text())
    registry_path = ROOT / ('secrets/development-keyring.json' if development else 'security/secure-keyring.json')
    registry = json.loads(registry_path.read_text())
    source = build_dir / 'bootloader/bootloader-unsigned.bin'
    target_dir = ROOT / ('secrets' if development else 'security')
    target_dir.mkdir(exist_ok=True)
    output = target_dir / ('development-bootloader.bin' if development else 'secure-bootloader.bin')
    parent = ROOT / 'secrets' if development else directory
    with tempfile.TemporaryDirectory(prefix='offline-sign-', dir=parent) as temporary:
        if not Path(temporary).resolve().is_relative_to(parent.resolve()):
            raise ValueError('Temporary signer directory escaped the private-key directory')
        if os.name == 'nt':
            subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                            str(ROOT / 'tools/protect_key_directory.ps1'), temporary], check=True)
        command = [sys.executable, '-m', 'espsecure', 'sign-data', '--version', '2']
        for index in range(3):
            key_path = Path(temporary) / f'K{index}.pem'
            key_path.write_bytes((ROOT / f'secrets/development-K{index}.pem').read_bytes() if development else pem(load_key(directory, index)))
            command.extend(['--keyfile', str(key_path)])
        command.extend(['--output', str(output), str(source)])
        subprocess.run(command, check=True)
    identities = verified_signatures(output)
    if identities != [{name: key[name] for name in ('spki_sha256', 'efuse_digest')} for key in registry['keys']]:
        raise ValueError('Offline signature verification failed')
    if output.stat().st_size > config['PARTITION_TABLE_OFFSET']:
        raise ValueError('Bootloader does not fit before the partition table')
    if not development:
        metadata = {'schema': 1, 'idf_version': 'v6.1', 'sha256': hashlib.sha256(output.read_bytes()).hexdigest(),
                    'keys': registry['keys'], 'config': {name: config.get(name, False) for name in SECURITY_CONFIG}}
        (ROOT / 'security/secure-bootloader.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    print('Three bootloader signatures verified; private temporary files removed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build-secure')
    parser.add_argument('--directory', type=Path, default=DEFAULT_DIRECTORY)
    parser.add_argument('--approve', action='store_true')
    parser.add_argument('--development', action='store_true')
    args = parser.parse_args()
    if args.approve:
        approve(args.build_dir, args.directory, args.development)
    else:
        print(f'Offline bootloader approval verified: {verify_approval(args.build_dir)}')


if __name__ == '__main__':
    main()
