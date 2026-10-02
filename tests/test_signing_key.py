"""Check key reuse, production key identity, and separation of development builds."""

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa


ROOT = Path(__file__).resolve().parent.parent


class SigningKeyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
        cls.pem = cls.key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                        serialization.NoEncryption())
        cls.public = cls.key.public_key().public_bytes(serialization.Encoding.DER,
                                                       serialization.PublicFormat.SubjectPublicKeyInfo)
        cls.fingerprint = hashlib.sha256(cls.public).hexdigest()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'tools').mkdir()
        shutil.copyfile(ROOT / 'tools/prepare_ota_key.py', self.root / 'tools/prepare_ota_key.py')
        self.path = self.root / 'secrets/ota_signing_key.pem'
        (self.root / 'ota_signing_public_key.sha256').write_text(self.fingerprint + '\n', encoding='ascii')

    def prepare(self, argument, pem=None):
        env = dict(os.environ)
        env.pop('OTA_SIGNING_KEY_PEM', None)
        if pem is not None:
            env['OTA_SIGNING_KEY_PEM'] = pem.decode('ascii')
        result = subprocess.run([sys.executable, str(self.root / 'tools/prepare_ota_key.py'), argument],
                                env=env, capture_output=True)
        self.assertNotIn(b'PRIVATE KEY', result.stdout + result.stderr)
        self.assertFalse(self.pem in result.stdout + result.stderr, 'Private key appeared in command output')
        return result

    def test_matching_github_secret_is_accepted_and_reused(self):
        self.assertEqual(self.prepare('--from-env', self.pem).returncode, 0)
        self.assertTrue(self.path.read_bytes() == self.pem, 'Secret was not preserved')
        self.assertEqual(self.prepare('--generate').returncode, 0)
        self.assertTrue(self.path.read_bytes() == self.pem, 'Local key was changed')

    def test_missing_or_different_production_secret_is_rejected(self):
        self.assertNotEqual(self.prepare('--from-env').returncode, 0)
        self.assertFalse(self.path.exists())
        (self.root / 'ota_signing_public_key.sha256').write_text('0' * 64 + '\n', encoding='ascii')
        result = self.prepare('--from-env', self.pem)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'refusing key rotation', result.stderr)
        self.assertFalse(self.path.exists())

    def test_development_key_leaves_production_fingerprint_unchanged(self):
        self.assertEqual(self.prepare('--development').returncode, 0)
        self.assertTrue(self.path.read_bytes() != self.pem, 'Development build reused the production key')
        self.assertEqual((self.root / 'ota_signing_public_key.sha256').read_text().strip(), self.fingerprint)
        self.assertNotEqual(self.prepare('--development').returncode, 0)

    def test_different_key_cannot_overwrite_an_existing_local_key(self):
        self.assertEqual(self.prepare('--development').returncode, 0)
        original = self.path.read_bytes()
        self.assertNotEqual(self.prepare('--from-env', self.pem).returncode, 0)
        self.assertTrue(self.path.read_bytes() == original, 'Existing key was overwritten')

    def test_firmware_signature_is_rejected_with_a_different_key(self):
        image = ROOT / 'build/CI-Lights.bin'
        if not image.exists():
            self.skipTest('Run idf.py build first')
        self.assertEqual(self.prepare('--development').returncode, 0)
        result = subprocess.run([sys.executable, '-m', 'espsecure', 'verify-signature', '--version', '2',
                                 '--keyfile', str(self.path), str(image)], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn(b'PRIVATE KEY', result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
