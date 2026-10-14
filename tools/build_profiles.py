"""Build isolated standard/secure firmware with one shared version; never flash hardware."""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from prepare_secure_keys import DEFAULT_DIRECTORY, load_key, pem
from repository_config import repository_name

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=['standard', 'secure', 'both'], default='both')
    parser.add_argument('--version')
    parser.add_argument('--directory', type=Path, default=DEFAULT_DIRECTORY)
    parser.add_argument('--approve-bootloader', action='store_true')
    parser.add_argument('--development', action='store_true')
    parser.add_argument('--ci', action='store_true', help='Use the already staged active GitHub signing key')
    args = parser.parse_args()
    idf = Path(os.environ['IDF_PATH']) / 'tools/idf.py'
    sdk_version = subprocess.check_output([sys.executable, str(idf), '--version'], text=True).strip()
    if not re.search(r'ESP-IDF v6\.1(?:[.\s-]|$)', sdk_version):
        raise ValueError(f'Approved profiles require ESP-IDF 6.1; found {sdk_version}')
    previous = (ROOT / 'version.txt').read_text().strip()
    version = args.version or os.environ.get('CI_LIGHTS_RELEASE_VERSION') or f'1.0.{int(previous.split(".")[-1]) + 1}'
    if not re.fullmatch(r'1\.0\.[0-9]+', version):
        raise ValueError('Invalid release version')
    env = {**os.environ, 'CI_LIGHTS_RELEASE_VERSION': version, 'IDF_TARGET': 'esp32s3'}
    registry_path = ROOT / ('secrets/development-keyring.json' if args.development else 'security/secure-keyring.json')
    registry = json.loads(registry_path.read_text()) if args.profile != 'standard' else None
    active = registry['active_key'] if registry else 0
    policy = registry.get('revoke_mask', 0) if registry else 0
    if active not in (0, 1, 2) or not 0 <= policy <= 7 or policy & (1 << active):
        raise ValueError('Invalid signed key rotation policy')
    for profile in (['standard', 'secure'] if args.profile == 'both' else [args.profile]):
        build = ROOT / f'build-{profile}'
        build.mkdir(exist_ok=True)
        temporary = None
        try:
            if profile == 'secure' and not (args.ci or args.development):
                temporary = tempfile.TemporaryDirectory(prefix='app-sign-', dir=args.directory)
                if not Path(temporary.name).resolve().is_relative_to(args.directory.resolve()):
                    raise ValueError('Temporary signer directory escaped the private-key directory')
                if os.name == 'nt':
                    subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                                    str(ROOT / 'tools/protect_key_directory.ps1'), temporary.name], check=True)
                key = Path(temporary.name) / f'K{active}.pem'
                key.write_bytes(pem(load_key(args.directory, active)))
            else:
                key = ROOT / ('secrets/secure_signing_key.pem' if profile == 'secure' else 'secrets/ota_signing_key.pem')
            if not key.exists():
                raise ValueError(f'Missing {profile} signing key; restore the existing key')
            config_path = build / 'sdkconfig'
            overrides = {'SECURE_BOOT_SIGNING_KEY': json.dumps(key.as_posix()),
                         'APP_OTA_GITHUB_REPOSITORY': json.dumps(repository_name())}
            if profile == 'secure':
                overrides.update(APP_SECURE_KEY_INDEX=str(active), APP_SECURE_REVOKE_MASK=str(policy),
                                 PARTITION_TABLE_OFFSET='0x10000')
            text = config_path.read_text() if config_path.exists() else ''
            for name, value in overrides.items():
                text, count = re.subn(rf'^CONFIG_{name}=.*$', f'CONFIG_{name}={value}', text, flags=re.M)
                if not count:
                    text += f'\nCONFIG_{name}={value}\n'
            config_path.write_text(text)
            command = [sys.executable, str(idf), '-B', str(build), '-D', f'CI_LIGHTS_PROFILE={profile}',
                       '-D', f'CI_LIGHTS_SIGNING_KEY={key.as_posix()}']
            if profile == 'secure':
                command.extend(['-D', f'CI_LIGHTS_KEYRING={registry_path.as_posix()}'])
            command.append('build')
            subprocess.run(command, cwd=ROOT, env=env, check=True)
            if profile == 'secure':
                command = [sys.executable, str(ROOT / 'tools/secure_bootloader.py'), '--build-dir', str(build)]
                if args.approve_bootloader or args.development:
                    command.append('--approve')
                if args.development:
                    command.append('--development')
                command.extend(['--directory', str(args.directory)])
                subprocess.run(command, cwd=ROOT, check=True)
        finally:
            if temporary:
                temporary.cleanup()
    if args.profile == 'both' and not (args.ci or args.development or args.version or os.environ.get('CI_LIGHTS_RELEASE_VERSION')):
        (ROOT / 'version.txt').write_text(version + '\n')
    print(f'Profiles built successfully: {version}. No hardware was flashed.')


if __name__ == '__main__':
    main()
