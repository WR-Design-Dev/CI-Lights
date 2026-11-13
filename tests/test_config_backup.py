"""Exercise production configuration backups with simulated NVS/flash and OpenSSL crypto."""
import base64
import copy
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from backup_host_headers import HEADERS

ROOT = Path(__file__).resolve().parent.parent
PASSWORD = b'backup-password-123'


class ConfigBackupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if os.name == 'nt':
            raise unittest.SkipTest('Native backup tests run under Linux/WSL; IDF checks Windows compilation')
        compiler = shutil.which('cc')
        if not compiler:
            raise RuntimeError('A native C compiler is required')
        if not (ROOT / 'managed_components/espressif__cjson/cJSON/cJSON.c').exists():
            raise unittest.SkipTest('Run an ESP-IDF build to resolve the cJSON component first')
        cls.directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        root = Path(cls.directory.name)
        for name, contents in HEADERS.items():
            path = root / name
            path.parent.mkdir(exist_ok=True, parents=True)
            path.write_text(contents)
        options = [compiler, '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-fPIC', '-I', str(root),
                   '-I', str(ROOT / 'main'), '-I', str(ROOT / 'managed_components/espressif__cjson/cJSON')]
        stub = root / 'platform.o'
        subprocess.run([*options, '-w', '-c', str(ROOT / 'tests/backup_host_platform.c'), '-o', str(stub)], check=True)
        sources = ['app_settings', 'app_config_store', 'app_branding', 'app_ota_storage',
                   'app_ota_policy', 'app_timezones', 'app_hostname_policy', 'app_backup_model', 'app_backup_crypto', 'app_backup']
        library = root / 'backup.so'
        subprocess.run([*options, '-Wall', '-Wextra', '-Werror', '-shared',
                        *[str(ROOT / 'main' / (name + '.c')) for name in sources],
                        str(ROOT / 'managed_components/espressif__cjson/cJSON/cJSON.c'), str(stub),
                        '-L/lib/x86_64-linux-gnu', '-L/usr/lib/x86_64-linux-gnu', '-l:libcrypto.so.3', '-lm', '-o', str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        signatures = {'test_seed': ([ctypes.c_int], None), 'test_boot': ([], None),
                      'test_export': ([ctypes.c_char_p], ctypes.c_void_p),
                      'test_validate': ([ctypes.c_char_p, ctypes.c_char_p], ctypes.c_void_p),
                      'test_import': ([ctypes.c_char_p, ctypes.c_char_p], ctypes.c_int),
                      'test_state': ([], ctypes.c_void_p), 'test_free': ([ctypes.c_void_p], None),
                      'test_writes': ([], ctypes.c_int), 'test_fail_after': ([ctypes.c_int], None),
                      'test_reset': ([], ctypes.c_int), 'test_config_present': ([], ctypes.c_int),
                      'test_valid_json': ([ctypes.c_char_p], ctypes.c_int), 'test_expand': ([], None)}
        signatures.update({'test_open_file': ([ctypes.c_char_p, ctypes.c_char_p], ctypes.c_void_p),
                           'test_seal_file': ([ctypes.c_char_p, ctypes.c_char_p], ctypes.c_void_p),
                           'test_clear': ([], None), 'test_select_bank': ([ctypes.c_uint8], None),
                           'test_banner': ([], ctypes.c_void_p)})
        for name, (args, result) in signatures.items():
            function = getattr(cls.lib, name)
            function.argtypes, function.restype = args, result

    def read(self, pointer):
        self.assertTrue(pointer)
        try:
            return json.loads(ctypes.string_at(pointer))
        finally:
            self.lib.test_free(pointer)

    def backup(self):
        return self.read(self.lib.test_export(PASSWORD))

    def state(self):
        return self.read(self.lib.test_state())

    def setUp(self):
        self.lib.test_seed(1)

    def test_encrypted_round_trip_includes_complete_configuration(self):
        source = self.state()
        backup = self.backup()
        encoded = json.dumps(backup).encode()
        for secret in ['wifi-password', 'private-test-token', 'source-admin', 'Source title', 'source-password']:
            self.assertNotIn(secret.encode(), encoded)
        self.assertEqual((backup['cipher'], backup['kdf'], backup['iterations']), ('AES-256-GCM', 'PBKDF2-SHA256', 600000))
        self.assertEqual(len(base64.b64decode(backup['salt'])), 16)
        self.assertEqual(len(base64.b64decode(backup['nonce'])), 12)
        self.lib.test_seed(0)
        before = self.lib.test_writes()
        summary = self.read(self.lib.test_validate(PASSWORD, encoded))
        self.assertEqual(summary['admin_username'], 'source-admin')
        self.assertEqual(summary['networks'], ['source-wifi'])
        self.assertNotIn('token', json.dumps(summary))
        self.assertEqual(self.lib.test_writes(), before, 'Validation must never mutate NVS or branding')
        self.assertEqual(self.lib.test_import(PASSWORD, encoded), 0)
        self.assertEqual(self.state()['device']['title'], 'Target title', 'Activation waits for restart')
        self.lib.test_boot()
        target = self.state()
        self.assertEqual(target.pop('blocked'), '1.0.1006', 'Keep this device’s rollback quarantine')
        source.pop('blocked')
        self.assertEqual(target, source)

    def test_wrong_password_tampering_and_format_changes_never_write(self):
        backup = self.backup()
        for field, changed in [('data', None), ('nonce', None), ('salt', None), ('schema', 2),
                               ('iterations', 1), ('cipher', 'none')]:
            candidate = dict(backup)
            if changed is None:
                data = bytearray(base64.b64decode(candidate[field])); data[0] ^= 1
                changed = base64.b64encode(data).decode()
            candidate[field] = changed
            before = self.lib.test_writes()
            self.assertNotEqual(self.lib.test_import(PASSWORD, json.dumps(candidate).encode()), 0, field)
            self.assertEqual(self.lib.test_writes(), before, field)
        self.assertNotEqual(self.lib.test_import(b'incorrect-password', json.dumps(backup).encode()), 0)
        fresh = self.backup()
        self.assertNotEqual(backup['salt'], fresh['salt'])
        self.assertNotEqual(backup['nonce'], fresh['nonce'])
        self.assertNotEqual(backup['data'], fresh['data'])

    def test_interrupted_import_keeps_a_complete_old_or_new_configuration(self):
        file = json.dumps(self.backup()).encode()
        completed = False
        for fault in range(1, 100):
            self.lib.test_seed(0)
            self.lib.test_fail_after(fault)
            error = self.lib.test_import(PASSWORD, file)
            self.lib.test_fail_after(0)
            self.lib.test_boot()
            state = self.state()
            if state['device']['title'] == 'Target title':
                self.assertEqual(state['admin']['username'], 'target-admin')
                self.assertEqual(state['wifi']['networks'][0]['ssid'], 'target-wifi')
                self.assertEqual(state['banner'], '<svg>target</svg>')
            else:
                self.assertEqual(state['device']['title'], 'Source title')
                self.assertEqual(state['admin']['username'], 'source-admin')
                self.assertEqual(state['wifi']['networks'][0]['ssid'], 'source-wifi')
                self.assertEqual(state['banner'], '<svg>source</svg>')
            if error == 0:
                self.assertEqual(state['device']['title'], 'Source title')
                completed = True
                break
        self.assertTrue(completed, 'Fault sweep must eventually reach a complete import')

    def test_maximum_branding_and_eight_enterprise_networks_fit_file_limit(self):
        self.lib.test_expand()
        backup = self.backup()
        file = json.dumps(backup).encode()
        self.assertLess(len(file), 160 * 1024)
        self.lib.test_seed(0)
        self.assertEqual(self.lib.test_import(PASSWORD, file), 0)
        self.lib.test_boot()
        self.assertEqual(len(self.state()['wifi']['networks']), 8)

    def test_parser_rejects_deep_json_and_embedded_zeroes(self):
        for text in [b'[' * 13 + b'0' + b']' * 13, b'{"x":"a\\u0000b"}', b'{broken', b'{} trailing']:
            self.assertFalse(self.lib.test_valid_json(text))
        self.assertTrue(self.lib.test_valid_json(b'{"x":"literal \\\\u0000"}'))

    def test_authenticated_but_invalid_settings_are_rejected_before_writes(self):
        plain = self.read(self.lib.test_open_file(PASSWORD, json.dumps(self.backup()).encode()))
        cases = [('device', 'cpu_mode', 100), ('device', 'hostname', 'not valid.local'),
                 ('jenkins', 'url', 'http://jenkins.example'), ('jenkins', 'job_path', '/other/path'),
                 ('jenkins', 'status_timeout_seconds', 601), ('jenkins', 'poll_interval_minutes', 0),
                 ('lights', 'brightness', 0), ('lights', 'colors', ['#notrgb', '#000000', '#000000']),
                 ('ota', 'time_zone', 'Unknown/Zone'), ('ota', 'interval_hours', 5),
                 ('admin', 'username', 'bad user'), ('admin', 'verifier', 'short'),
                 ('wifi', 'last_index', 7), ('wifi', 'networks', [])]
        for group, key, value in cases:
            altered = copy.deepcopy(plain)
            altered[group][key] = value
            sealed = self.read(self.lib.test_seal_file(PASSWORD, json.dumps(altered).encode()))
            before = self.lib.test_writes()
            self.assertNotEqual(self.lib.test_import(PASSWORD, json.dumps(sealed).encode()), 0, (group, key))
            self.assertEqual(self.lib.test_writes(), before, (group, key))

    def test_import_into_fresh_setup_and_repeated_import_in_both_directions(self):
        source = json.dumps(self.backup()).encode()
        self.lib.test_seed(0)
        target = json.dumps(self.backup()).encode()
        self.lib.test_clear()
        self.assertFalse(self.lib.test_config_present())
        for file, title in [(source, 'Source title'), (target, 'Target title'), (source, 'Source title')]:
            self.assertEqual(self.lib.test_import(PASSWORD, file), 0)
            self.lib.test_boot()
            self.assertEqual(self.state()['device']['title'], title)
            self.assertEqual(self.state()['blocked'], '')

    def test_absent_branding_replaces_existing_images_and_survives_reset(self):
        plain = self.read(self.lib.test_open_file(PASSWORD, json.dumps(self.backup()).encode()))
        plain['branding'] = {'banner': None, 'favicon': None}
        file = json.dumps(self.read(self.lib.test_seal_file(PASSWORD, json.dumps(plain).encode()))).encode()
        self.lib.test_seed(0)
        self.assertEqual(self.lib.test_import(PASSWORD, file), 0)
        self.lib.test_boot()
        self.assertEqual(self.state()['banner'], '')
        self.assertTrue(self.lib.test_reset())
        self.lib.test_boot()
        pointer = self.lib.test_banner()
        try:
            self.assertEqual(ctypes.string_at(pointer), b'')
        finally:
            self.lib.test_free(pointer)

    def test_factory_reset_clears_both_banks_and_keeps_selected_branding(self):
        backup = json.dumps(self.backup()).encode()
        self.lib.test_seed(0)
        self.assertEqual(self.lib.test_import(PASSWORD, backup), 0)
        self.lib.test_boot()
        self.assertTrue(self.lib.test_reset())
        for bank in (0, 1):
            self.lib.test_select_bank(bank)
            self.assertFalse(self.lib.test_config_present())
            pointer = self.lib.test_banner()
            try:
                self.assertEqual(ctypes.string_at(pointer), b'<svg>source</svg>')
            finally:
                self.lib.test_free(pointer)


if __name__ == '__main__':
    unittest.main()
