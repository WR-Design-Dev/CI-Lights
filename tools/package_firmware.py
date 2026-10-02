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
    if not config.get('SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT') or config.get('SECURE_BOOT') or config.get('SECURE_FLASH_ENC_ENABLED'):
        raise ValueError('Expected software signature verification without eFuse security activation')
    subprocess.run([sys.executable, '-m', 'espsecure', 'verify-signature', '--version', '2',
                    '--keyfile', str(root / 'secrets' / 'ota_signing_key.pem'), str(image_path)], check=True)
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
        'layout': '4mb-ota-v1', 'size': len(image), 'sha256': hashlib.sha256(image).hexdigest(),
        'url': f'https://github.com/{repository}/releases/download/v{version}/CI-Lights.bin',
    }
    web_manifest = {
        'schema': 1, 'name': 'CI-Lights', 'version': version, 'layout': '4mb-ota-v1',
        'builds': [{'chipFamily': 'ESP32-S3', 'parts': web_parts}],
        'release_url': f'https://github.com/{repository}/releases/tag/v{version}',
    }
    (release_dir / 'ota-manifest.json').write_text(json.dumps(ota_manifest, indent=2) + '\n', encoding='utf-8')
    checksums.append(f'{hashlib.sha256((release_dir / "ota-manifest.json").read_bytes()).hexdigest()}  ota-manifest.json')
    (release_dir / 'SHA256SUMS').write_text('\n'.join(checksums) + '\n', encoding='ascii')
    (site_dir / 'install-manifest.json').write_text(json.dumps(web_manifest, indent=2) + '\n', encoding='utf-8')
    for name in ('index.html', 'styles.css', 'installer.js', 'flash-core.mjs', 'impressum.html', 'datenschutz.html',
                 'language.mjs', 'translations.mjs', 'language-init.js'):
        shutil.copyfile(root / 'web-flasher' / name, site_dir / name)
    # Reuse the administration graphic directly so both interfaces stay in sync.
    shutil.copyfile(root / 'main' / 'assets' / 'TrafficLight.svg', site_dir / 'TrafficLight.svg')
    shutil.copyfile(release_dir / 'SHA256SUMS', version_dir / 'SHA256SUMS')
    (site_dir / '.nojekyll').touch()
    print(f'Release {version}: {len(image)} bytes; {0x1C0000 - len(image)} bytes free in each OTA slot')
    print(f'GitHub assets: {release_dir}')
    print(f'Browser installer: {site_dir}')
    return ota_manifest, web_manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('build'))
    parser.add_argument('--output-dir', type=Path, default=Path('dist'))
    parser.add_argument('--repository', default='WR-Design-Dev/CI-Lights')
    parser.add_argument('--version', help='Expected version without the v prefix')
    args = parser.parse_args()
    try:
        build_package(args.build_dir, args.output_dir, args.repository, args.version)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Packaging failed: {error}\n')


if __name__ == '__main__':
    main()
