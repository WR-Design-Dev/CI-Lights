"""Exercise public signature verification and secure release rejection paths."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile
import unittest
from unittest.mock import patch
import zlib

from tools import secure_bootloader
from tools.initialize_fork import validate_target
from tools.package_firmware import build_secure_package
from tools.security_images import verified_signatures

ROOT = Path(__file__).resolve().parent.parent


class PublicSecurityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.boot = (ROOT / 'security/secure-bootloader.bin').read_bytes()

    def test_frozen_bootloader_has_three_independent_trusted_signatures(self):
        keys = json.loads((ROOT / 'security/secure-keyring.json').read_text())['keys']
        self.assertEqual(verified_signatures(self.boot),
                         [{name: key[name] for name in ('spki_sha256', 'efuse_digest')} for key in keys])

    def test_tampering_fails_even_after_recomputing_signature_block_crc(self):
        for offset, repair_crc, message in [(128, False, 'image hash'),
                                           (len(self.boot) - 4096 + 812, False, 'CRC'),
                                           (len(self.boot) - 4096 + 812, True, 'RSA-PSS'),
                                           (len(self.boot) - 4096 + 424, True, 'hardware'),
                                           (len(self.boot) - 16, False, 'signature-sector')]:
            changed = bytearray(self.boot)
            changed[offset] ^= 1
            if repair_crc:
                start = len(changed) - 4096
                struct.pack_into('<I', changed, start + 1196, zlib.crc32(changed[start:start + 1196]) & 0xffffffff)
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, message):
                verified_signatures(changed)

    def test_approval_rejects_changed_bootloader_and_trust(self):
        approval = json.loads((ROOT / 'security/secure-bootloader.json').read_text())
        registry = json.loads((ROOT / 'security/secure-keyring.json').read_text())
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'security').mkdir()
            (root / 'config').mkdir()
            (root / 'security/secure-keyring.json').write_text(json.dumps(registry))
            (root / 'security/secure-bootloader.json').write_text(json.dumps(approval))
            (root / 'security/secure-bootloader.bin').write_bytes(self.boot)
            (root / 'config/sdkconfig.json').write_text(json.dumps(approval['config']))
            with patch.object(secure_bootloader, 'ROOT', root):
                secure_bootloader.verify_approval(root)
                (root / 'security/secure-bootloader.bin').write_bytes(self.boot[:-1] + b'\0')
                with self.assertRaisesRegex(ValueError, 'hash'):
                    secure_bootloader.verify_approval(root)
                (root / 'security/secure-bootloader.bin').write_bytes(self.boot)
                registry['keys'].reverse()
                (root / 'security/secure-keyring.json').write_text(json.dumps(registry))
                with self.assertRaisesRegex(ValueError, 'trust'):
                    secure_bootloader.verify_approval(root)

    def test_fork_initialization_cannot_rotate_upstream_or_existing_keys(self):
        upstream = 'WR-Design-Dev/CI-Lights'
        validate_target('alice/CI-Lights', 'https://github.com/alice/CI-Lights.git', upstream, False)
        validate_target('alice/CI-Lights', 'git@github.com:alice/CI-Lights.git', upstream, False)
        for arguments in [(upstream, 'https://github.com/' + upstream, upstream, False),
                          ('alice/CI-Lights', 'https://github.com/' + upstream, upstream, False),
                          ('alice/CI-Lights', 'https://github.com/alice/CI-Lights', upstream, True),
                          ('alice/CI-Lights', 'https://github.com/alice/CI-Lights', 'alice/CI-Lights', False)]:
            with self.assertRaises(ValueError):
                validate_target(*arguments)


class SecurePackageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = ROOT / 'build-secure'
        if not (cls.source / 'CI-Lights-Secure.bin').exists():
            raise unittest.SkipTest('Build the secure profile first')
        cls.image = (cls.source / 'CI-Lights-Secure.bin').read_bytes()
        cls.registry = json.loads((ROOT / 'security/secure-keyring.json').read_text())
        if verified_signatures(cls.image)[0]['spki_sha256'] != cls.registry['keys'][cls.registry['active_key']]['spki_sha256']:
            raise unittest.SkipTest('Disposable development image is not a production release')
        cls.version = cls.image[48:80].split(b'\0')[0].decode('ascii')
        cls.repository = json.loads((cls.source / 'config/sdkconfig.json').read_text())['APP_OTA_GITHUB_REPOSITORY']

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.build = Path(self.temp.name) / 'build'
        self.output = Path(self.temp.name) / 'dist'
        flash = json.loads((self.source / 'flasher_args.json').read_text())
        for name in [*flash['flash_files'].values(), 'flasher_args.json', 'config/sdkconfig.json']:
            destination = self.build / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(self.source / name, destination)
        # build_secure_package adds to a standard release destination.
        (self.output / 'site/firmware' / f'v{self.version}').mkdir(parents=True)

    def package(self):
        return build_secure_package(self.build, self.output, self.repository, self.version)

    def test_secure_release_uses_only_frozen_triple_signed_bootloader(self):
        ota, web = self.package()
        self.assertEqual(ota['profile'], 'secure')
        self.assertEqual(web['bootloader_signatures'], 3)
        self.assertEqual([part['offset'] for part in web['builds'][0]['parts']], [0, 0x10000, 0x18000, 0x20000])
        self.assertEqual((self.output / 'release/bootloader-secure.bin').read_bytes(),
                         (ROOT / 'security/secure-bootloader.bin').read_bytes())
        self.assertEqual(ota['sha256'], hashlib.sha256(self.image).hexdigest())
        self.assertFalse(any(path.suffix in ('.pem', '.dpapi') for path in self.output.rglob('*')))

    def test_rejects_security_and_signed_key_policy_changes(self):
        config_path = self.build / 'config/sdkconfig.json'
        original = json.loads(config_path.read_text())
        for key, value in [('NVS_ENCRYPTION', False), ('SECURE_DISABLE_ROM_DL_MODE', False),
                           ('SECURE_FLASH_ENCRYPTION_MODE_RELEASE', False), ('SECURE_BOOT_ALLOW_JTAG', True),
                           ('SECURE_BOOT_ENABLE_AGGRESSIVE_KEY_REVOKE', True), ('BOOTLOADER_APP_ANTI_ROLLBACK', True),
                           ('APP_SECURE_KEY_INDEX', (self.registry['active_key'] + 1) % 3),
                           ('APP_SECURE_REVOKE_MASK', 1 << self.registry['active_key'])]:
            config_path.write_text(json.dumps({**original, key: value}))
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.package()

    def test_rejects_corrupt_image_or_standard_profile(self):
        changed = bytearray(self.image)
        changed[512] ^= 1
        (self.build / 'CI-Lights-Secure.bin').write_bytes(changed)
        with self.assertRaises(ValueError):
            self.package()
        changed = bytearray(self.image)
        changed[80:112] = b'CI-Lights' + b'\0' * 23
        (self.build / 'CI-Lights-Secure.bin').write_bytes(changed)
        with self.assertRaisesRegex(ValueError, 'project'):
            self.package()

    def test_rejects_unencrypted_nvs_keys_or_wrong_partition_offset(self):
        table_path = self.build / 'partition_table/partition-table.bin'
        original = table_path.read_bytes()
        for position, value in [(3 * 32 + 28, 0), (4 * 32 + 4, 0x30000)]:
            changed = bytearray(original)
            struct.pack_into('<I', changed, position, value)
            table_path.write_bytes(changed)
            with self.assertRaisesRegex(ValueError, 'partition table'):
                self.package()
