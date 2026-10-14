"""Package a signed ESP32-S3 application for GitHub OTA and browser installation."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

try:
    from .security_images import verified_signatures
    from .secure_bootloader import verify_approval
    from .repository_config import repository_name
except ImportError:
    from security_images import verified_signatures
    from secure_bootloader import verify_approval
    from repository_config import repository_name


def image_description(image):
    if len(image) < 288 or image[0] != 0xE9 or struct.unpack_from('<H', image, 12)[0] != 9:
        raise ValueError('Expected an ESP32-S3 application image')
    if struct.unpack_from('<I', image, 32)[0] != 0xABCD5432:
        raise ValueError('Application descriptor is missing')
    version = image[48:80].split(b'\0', 1)[0].decode('ascii')
    project = image[80:112].split(b'\0', 1)[0].decode('ascii')
    if project != 'CI-Lights' or not re.fullmatch(r'1\.0\.[0-9]+', version):
        raise ValueError('Invalid project name or release version')
    if len(image) > 0x1C0000 or len(image) % 4096 or image[-4096:-4094] != b'\xe7\x02':
        raise ValueError('Image must fit a 1.75 MiB slot and contain an RSA signature block')
    return version


def build_package(build_dir, output_dir, repository, expected_version=None):
    root = Path(__file__).resolve().parent.parent
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository):
        raise ValueError('Repository must be owner/name')
    image_path = build_dir / 'CI-Lights.bin'
    image = image_path.read_bytes()
    version = image_description(image)
    if expected_version and expected_version != version:
        raise ValueError(f'Tag version {expected_version} differs from image version {version}')
    config = json.loads((build_dir / 'config' / 'sdkconfig.json').read_text(encoding='utf-8'))
    if config.get('APP_OTA_GITHUB_REPOSITORY') != repository:
        raise ValueError('Firmware OTA repository differs from the publishing repository')
    if config.get('ESPTOOLPY_FLASHSIZE') != '4MB' or not config.get('BOOTLOADER_APP_ROLLBACK_ENABLE'):
        raise ValueError('Expected 4 MiB flash and rollback support')
    if not config.get('SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT') or any(config.get(name) for name in (
            'APP_SECURITY_SECURE', 'SECURE_BOOT', 'SECURE_FLASH_ENC_ENABLED', 'NVS_ENCRYPTION')):
        raise ValueError('Expected software signature verification without eFuse security activation')
    signatures = verified_signatures(image)
    expected_key = (root / 'ota_signing_public_key.sha256').read_text(encoding='ascii').strip()
    if len(signatures) != 1 or signatures[0]['spki_sha256'] != expected_key:
        raise ValueError('Standard firmware does not use the registered signing key')
    flash = json.loads((build_dir / 'flasher_args.json').read_text(encoding='utf-8'))
    parts = {int(offset, 0): build_dir / name for offset, name in flash['flash_files'].items()}
    expected_offsets = {0, 0x8000, 0x11000, 0x20000}
    if set(parts) != expected_offsets:
        raise ValueError(f'Unexpected USB flash addresses: {sorted(parts)}')
    if parts[0x20000].resolve() != image_path.resolve():
        raise ValueError('USB and OTA application files must be identical')
    # Keep the declared OTA layout and the physical partition table in sync.
    expected_partitions = {
        'nvs': (1, 2, 0x9000, 0x6000), 'phy_init': (1, 1, 0x10000, 0x1000),
        'otadata': (1, 0, 0x11000, 0x2000), 'ota_0': (0, 0x10, 0x20000, 0x1C0000),
        'branding': (1, 0x40, 0x1E0000, 0x20000), 'ota_1': (0, 0x11, 0x200000, 0x1C0000),
    }
    partitions = {}
    table = parts[0x8000].read_bytes()
    for position in range(0, len(table) - 31, 32):
        magic, kind, subtype, offset, size, label, flags = struct.unpack_from('<HBBII16sI', table, position)
        if magic != 0x50AA:
            break
        if flags:
            raise ValueError('Unexpected encrypted or read-only partition flags')
        name = label.split(b'\0', 1)[0].decode('ascii')
        if name in partitions:
            raise ValueError('Duplicate partition name')
        partitions[name] = (kind, subtype, offset, size)
    if partitions != expected_partitions:
        raise ValueError('Partition table does not match layout 4mb-ota-v1')
    # The browser keeps flash parameters to preserve the signed images.
    # The build must contain the correct DIO / 80 MHz / 4 MiB bootloader header.
    bootloader = parts[0].read_bytes()
    if bootloader[:1] != b'\xe9' or bootloader[2] != 2 or bootloader[3] != 0x2F:
        raise ValueError('Bootloader must be built for DIO, 80 MHz, 4 MiB')
    if parts[0x11000].read_bytes() != b'\xff' * 8192:
        raise ValueError('Initial OTA selection data must be erased')
    release_dir = output_dir / 'release'
    site_dir = output_dir / 'site'
    version_dir = site_dir / 'firmware' / f'v{version}'
    release_dir.mkdir(parents=True, exist_ok=True)
    version_dir.mkdir(parents=True, exist_ok=True)
    names = {0: 'bootloader.bin', 0x8000: 'partition-table.bin',
             0x11000: 'ota_data_initial.bin', 0x20000: 'CI-Lights.bin'}
    checksums = []
    web_parts = []
    for offset in sorted(parts):
        name = names[offset]
        shutil.copyfile(parts[offset], release_dir / name)
        shutil.copyfile(parts[offset], version_dir / name)
        data = parts[offset].read_bytes()
        checksum = hashlib.sha256(data).hexdigest()
        checksums.append(f'{checksum}  {name}')
        web_parts.append({'path': f'firmware/v{version}/{name}', 'offset': offset,
                          'size': len(data), 'sha256': checksum})
    ota_manifest = {
        'schema': 1, 'version': version, 'project': 'CI-Lights', 'chip': 'esp32s3',
        'profile': 'standard',
        'layout': '4mb-ota-v1', 'size': len(image), 'sha256': hashlib.sha256(image).hexdigest(),
        'url': f'https://github.com/{repository}/releases/download/v{version}/CI-Lights.bin',
    }
    web_manifest = {
        'schema': 1, 'name': 'CI-Lights', 'version': version, 'layout': '4mb-ota-v1',
        'repository': repository,
        'builds': [{'chipFamily': 'ESP32-S3', 'parts': web_parts}],
        'release_url': f'https://github.com/{repository}/releases/tag/v{version}',
    }
    (release_dir / 'ota-manifest.json').write_text(json.dumps(ota_manifest, indent=2) + '\n', encoding='utf-8')
    checksums.append(f'{hashlib.sha256((release_dir / "ota-manifest.json").read_bytes()).hexdigest()}  ota-manifest.json')
    (release_dir / 'SHA256SUMS').write_text('\n'.join(checksums) + '\n', encoding='ascii')
    (site_dir / 'install-manifest.json').write_text(json.dumps(web_manifest, indent=2) + '\n', encoding='utf-8')
    for name in ('index.html', 'styles.css', 'installer.js', 'flash-core.mjs', 'security-state.mjs', 'md5.mjs', 'impressum.html', 'datenschutz.html',
                 'language.mjs', 'translations.mjs', 'language-init.js'):
        shutil.copyfile(root / 'web-flasher' / name, site_dir / name)
    # A fresh fork links to its own downloads and documentation.
    for name in ('index.html', 'translations.mjs'):
        path = site_dir / name
        path.write_text(path.read_text(encoding='utf-8').replace('WR-Design-Dev/CI-Lights', repository), encoding='utf-8')
    # Reuse the administration graphic directly so both interfaces stay in sync.
    shutil.copyfile(root / 'main' / 'assets' / 'TrafficLight.svg', site_dir / 'TrafficLight.svg')
    shutil.copyfile(release_dir / 'SHA256SUMS', version_dir / 'SHA256SUMS')
    (site_dir / '.nojekyll').touch()
    print(f'Release {version}: {len(image)} bytes; {0x1C0000 - len(image)} bytes free in each OTA slot')
    print(f'GitHub assets: {release_dir}')
    print(f'Browser installer: {site_dir}')
    return ota_manifest, web_manifest


def build_secure_package(build_dir, output_dir, repository, expected_version=None):
    root = Path(__file__).resolve().parent.parent
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository):
        raise ValueError('Invalid publishing repository')
    config = json.loads((build_dir / 'config/sdkconfig.json').read_text())
    required = ('APP_SECURITY_SECURE', 'SECURE_BOOT', 'SECURE_BOOT_V2_ENABLED',
                'SECURE_SIGNED_APPS_RSA_SCHEME', 'SECURE_FLASH_ENC_ENABLED',
                'SECURE_FLASH_ENCRYPTION_AES256', 'SECURE_FLASH_ENCRYPTION_MODE_RELEASE',
                'SECURE_DISABLE_ROM_DL_MODE', 'NVS_ENCRYPTION', 'NVS_SEC_KEY_PROTECT_USING_FLASH_ENC',
                'BOOTLOADER_APP_ROLLBACK_ENABLE')
    forbidden = ('SECURE_BOOT_INSECURE', 'SECURE_BOOT_ALLOW_JTAG', 'SECURE_BOOT_ALLOW_UNUSED_DIGEST_SLOTS',
                 'BOOTLOADER_APP_ANTI_ROLLBACK',
                 'SECURE_BOOT_V2_ALLOW_EFUSE_RD_DIS', 'SECURE_BOOT_ENABLE_AGGRESSIVE_KEY_REVOKE',
                 'EFUSE_VIRTUAL', 'SECURE_FLASH_UART_BOOTLOADER_ALLOW_ENC', 'NVS_SEC_KEY_PROTECT_USING_HMAC')
    if any(not config.get(name) for name in required) or any(config.get(name) for name in forbidden):
        raise ValueError('Secure profile security configuration is incomplete or insecure')
    if config.get('APP_OTA_GITHUB_REPOSITORY') != repository or config.get('ESPTOOLPY_FLASHSIZE') != '4MB' or config.get('PARTITION_TABLE_OFFSET') != 0x10000:
        raise ValueError('Secure repository, flash size or partition offset differs')
    image_path = build_dir / 'CI-Lights-Secure.bin'
    image = image_path.read_bytes()
    # Same ESP application descriptor, but the project identity is signed too.
    if image[:1] != b'\xe9' or len(image) < 288 or struct.unpack_from('<H', image, 12)[0] != 9 or struct.unpack_from('<I', image, 32)[0] != 0xABCD5432:
        raise ValueError('Expected a secure ESP32-S3 application descriptor')
    version = image[48:80].split(b'\0', 1)[0].decode('ascii')
    project = image[80:112].split(b'\0', 1)[0].decode('ascii')
    if project != 'CI-Lights-Secure' or not re.fullmatch(r'1\.0\.[0-9]+', version) or expected_version and version != expected_version:
        raise ValueError('Secure project or release version differs')
    if len(image) > 0x1c0000 or (len(image) - 4096) % 65536:
        raise ValueError('Secure application must fit its OTA slot with MMU padding and signature')
    registry = json.loads((root / 'security/secure-keyring.json').read_text())
    index = config.get('APP_SECURE_KEY_INDEX')
    mask = config.get('APP_SECURE_REVOKE_MASK', 0)
    if index != registry['active_key'] or mask != registry.get('revoke_mask', 0) or mask & (1 << index):
        raise ValueError('Secure signed key policy differs from approved public registry')
    signatures = verified_signatures(image)
    expected_key = registry['keys'][index]
    if signatures != [{name: expected_key[name] for name in ('spki_sha256', 'efuse_digest')}]:
        raise ValueError('Secure application is not signed only by the active approved key')
    bootloader = verify_approval(build_dir)
    parts = {int(offset, 0): build_dir / path for offset, path in json.loads((build_dir / 'flasher_args.json').read_text())['flash_files'].items()}
    if set(parts) != {0, 0x10000, 0x18000, 0x20000} or parts[0x20000].read_bytes() != image:
        raise ValueError('Secure complete installation files or addresses differ')
    expected_partitions = {
        'nvs': (1, 2, 0x11000, 0x6000, 0), 'phy_init': (1, 1, 0x17000, 0x1000, 0),
        'otadata': (1, 0, 0x18000, 0x2000, 0), 'nvs_keys': (1, 4, 0x1a000, 0x1000, 1),
        'ota_0': (0, 0x10, 0x20000, 0x1c0000, 0), 'branding': (1, 0x40, 0x1e0000, 0x20000, 1),
        'ota_1': (0, 0x11, 0x200000, 0x1c0000, 0)}
    partitions = {}
    table = parts[0x10000].read_bytes()
    for position in range(0, len(table) - 31, 32):
        magic, kind, subtype, offset, size, label, flags = struct.unpack_from('<HBBII16sI', table, position)
        if magic != 0x50aa:
            break
        name = label.split(b'\0', 1)[0].decode('ascii')
        if name in partitions:
            raise ValueError('Duplicate secure partition')
        partitions[name] = kind, subtype, offset, size, flags
    if partitions != expected_partitions:
        raise ValueError('Secure partition table differs from 4mb-secure-v1')
    boot_bytes = bootloader.read_bytes()
    if boot_bytes[:1] != b'\xe9' or boot_bytes[2:4] != b'\x02\x2f' or parts[0x18000].read_bytes() != b'\xff' * 8192:
        raise ValueError('Secure bootloader flash header or initial OTA selection differs')
    parts[0] = bootloader
    release = output_dir / 'release'
    site = output_dir / 'site'
    target = site / 'firmware' / f'v{version}' / 'secure'
    release.mkdir(parents=True, exist_ok=True)
    target.mkdir(parents=True, exist_ok=True)
    names = {0: 'bootloader-secure.bin', 0x10000: 'partition-table-secure.bin',
             0x18000: 'ota_data_initial-secure.bin', 0x20000: 'CI-Lights-Secure.bin'}
    web_parts = []
    for offset in sorted(parts):
        name = names[offset]
        data = parts[offset].read_bytes()
        (release / name).write_bytes(data)
        (target / name).write_bytes(data)
        web_parts.append({'path': f'firmware/v{version}/secure/{name}', 'offset': offset,
                          'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
    ota = {'schema': 1, 'profile': 'secure', 'version': version, 'project': project, 'chip': 'esp32s3',
           'layout': '4mb-secure-v1', 'size': len(image), 'sha256': hashlib.sha256(image).hexdigest(),
           'signing_key': index, 'url': f'https://github.com/{repository}/releases/download/v{version}/CI-Lights-Secure.bin'}
    web = {'schema': 2, 'profile': 'secure', 'name': project, 'version': version, 'layout': '4mb-secure-v1',
           'repository': repository,
           'erase_required': True, 'ota_only': True, 'bootloader_signatures': 3,
           'builds': [{'chipFamily': 'ESP32-S3', 'parts': web_parts}],
           'release_url': f'https://github.com/{repository}/releases/tag/v{version}'}
    (release / 'ota-manifest-secure.json').write_text(json.dumps(ota, indent=2) + '\n', encoding='utf-8')
    (site / 'install-manifest-secure.json').write_text(json.dumps(web, indent=2) + '\n', encoding='utf-8')
    # One checksum list covers both variants, including both OTA manifests.
    checksums = '\n'.join(f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}'
                           for path in sorted(release.iterdir()) if path.name != 'SHA256SUMS') + '\n'
    (release / 'SHA256SUMS').write_text(checksums, encoding='ascii')
    (target / 'SHA256SUMS').write_text(checksums, encoding='ascii')
    standard_target = site / 'firmware' / f'v{version}'
    (standard_target / 'SHA256SUMS').write_text(checksums, encoding='ascii')
    print(f'Secure release {version}: {len(image)} bytes; three bootloader signatures and encrypted layout verified')
    return ota, web


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('build-standard'))
    parser.add_argument('--output-dir', type=Path, default=Path('dist'))
    parser.add_argument('--repository', default=repository_name())
    parser.add_argument('--version', help='Expected version without the v prefix')
    parser.add_argument('--secure-build-dir', type=Path, help='Include the secure profile in this release')
    args = parser.parse_args()
    try:
        build_package(args.build_dir, args.output_dir, args.repository, args.version)
        if args.secure_build_dir:
            build_secure_package(args.secure_build_dir, args.output_dir, args.repository, args.version)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Packaging failed: {error}\n')


if __name__ == '__main__':
    main()
