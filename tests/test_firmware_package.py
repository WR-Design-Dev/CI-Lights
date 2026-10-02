"""Integration checks for release safety using the actual signed IDF build."""

import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

from tools.package_firmware import build_package, image_description


ROOT = Path(__file__).resolve().parent.parent


class FirmwarePackageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = ROOT / 'build'
        if not (cls.source / 'CI-Lights.bin').exists():
            raise unittest.SkipTest('Run idf.py build first')
        cls.image = (cls.source / 'CI-Lights.bin').read_bytes()
        cls.version = image_description(cls.image)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.build = Path(self.temp.name) / 'build'
        self.output = Path(self.temp.name) / 'dist'
        flash = json.loads((self.source / 'flasher_args.json').read_text(encoding='utf-8'))
        for name in [*flash['flash_files'].values(), 'flasher_args.json', 'config/sdkconfig.json']:
            destination = self.build / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(self.source / name, destination)

    def package(self, version=None):
        return build_package(self.build, self.output, 'WR-Design-Dev/CI-Lights', version or self.version)

    def change_config(self, **values):
        path = self.build / 'config/sdkconfig.json'
        config = json.loads(path.read_text(encoding='utf-8'))
        config.update(values)
        path.write_text(json.dumps(config), encoding='utf-8')

    def test_signed_release_and_usb_files_match_build_without_private_key(self):
        ota, web = self.package()
        self.assertEqual(ota['sha256'], hashlib.sha256(self.image).hexdigest())
        self.assertEqual(ota['size'], len(self.image))
        self.assertEqual([part['offset'] for part in web['builds'][0]['parts']], [0, 0x8000, 0x11000, 0x20000])
        self.assertEqual(web['layout'], '4mb-ota-v1')
        self.assertEqual((self.output / 'site' / 'TrafficLight.svg').read_bytes(),
                         (ROOT / 'main' / 'assets' / 'TrafficLight.svg').read_bytes())
        flash = json.loads((self.source / 'flasher_args.json').read_text(encoding='utf-8'))['flash_files']
        for part in web['builds'][0]['parts']:
            expected = next(self.source / name for offset, name in flash.items() if int(offset, 0) == part['offset'])
            self.assertTrue((self.output / 'site' / part['path']).read_bytes() == expected.read_bytes(),
                            f'Browser firmware differs at offset {part["offset"]:#x}')
            self.assertEqual(part['size'], expected.stat().st_size)
            self.assertEqual(part['sha256'], hashlib.sha256(expected.read_bytes()).hexdigest())
        for path in self.output.rglob('*'):
            if path.is_file():
                # TLS libraries contain generic PEM parser strings. Check the
                # actual private key, and never let an assertion print its bytes.
                private_key = (ROOT / 'secrets/ota_signing_key.pem').read_bytes()
                self.assertFalse(private_key in path.read_bytes(), f'Private key leaked into {path.name}')
                self.assertNotEqual(path.suffix, '.pem')

    def test_rejects_wrong_chip_project_unsigned_and_oversized_images(self):
        wrong_chip = bytearray(self.image)
        struct.pack_into('<H', wrong_chip, 12, 5)
        wrong_project = bytearray(self.image)
        wrong_project[80] = ord('X')
        oversized = self.image[:-4096] + b'\xff' * 0x1C0000 + self.image[-4096:]
        for image in (wrong_chip, wrong_project, self.image[:-4096], oversized):
            with self.subTest(size=len(image)), self.assertRaises(ValueError):
                image_description(image)

    def test_rejects_tag_and_firmware_version_mismatch(self):
        with self.assertRaisesRegex(ValueError, 'Tag version'):
            self.package('1.0.0')
        self.assertFalse(self.output.exists())

    def test_rejects_wrong_repository_or_hardware_security_configuration(self):
        for config in ({'APP_OTA_GITHUB_REPOSITORY': 'other/repo'},
                       {'SECURE_BOOT': True}, {'SECURE_FLASH_ENC_ENABLED': True},
                       {'BOOTLOADER_APP_ROLLBACK_ENABLE': False},
                       {'SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT': False}):
            with self.subTest(config=config):
                shutil.copyfile(self.source / 'config/sdkconfig.json', self.build / 'config/sdkconfig.json')
                self.change_config(**config)
                with self.assertRaises(ValueError):
                    self.package()
                self.assertFalse(self.output.exists())

    def test_rejects_corrupted_signed_application(self):
        corrupted = bytearray(self.image)
        corrupted[400] ^= 1
        (self.build / 'CI-Lights.bin').write_bytes(corrupted)
        with self.assertRaises(subprocess.CalledProcessError):
            self.package()
        self.assertFalse(self.output.exists())

    def test_rejects_unexpected_usb_addresses(self):
        path = self.build / 'flasher_args.json'
        flash = json.loads(path.read_text(encoding='utf-8'))
        flash['flash_files']['0x9000'] = flash['flash_files'].pop('0x11000')
        path.write_text(json.dumps(flash), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'USB flash addresses'):
            self.package()

    def test_rejects_bootloader_header_that_web_tools_cannot_patch(self):
        path = self.build / 'bootloader/bootloader.bin'
        image = bytearray(path.read_bytes())
        image[3] = 0x1F  # 2 MiB instead of 4 MiB
        path.write_bytes(image)
        with self.assertRaisesRegex(ValueError, 'Bootloader'):
            self.package()

    def test_rejects_a_partition_table_with_a_different_ota_layout(self):
        path = self.build / 'partition_table/partition-table.bin'
        table = bytearray(path.read_bytes())
        # ota_1 is the sixth entry; moving it would invalidate OTA compatibility.
        struct.pack_into('<I', table, 5 * 32 + 4, 0x210000)
        path.write_bytes(table)
        with self.assertRaisesRegex(ValueError, 'Partition table'):
            self.package()


if __name__ == '__main__':
    unittest.main()
