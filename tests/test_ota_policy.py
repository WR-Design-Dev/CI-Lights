"""Compile and exercise the firmware's timezone, scheduling and rollback policy."""
import ctypes
from datetime import datetime, timezone
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


def epoch(value):
    return int(datetime.fromisoformat(value).replace(tzinfo=timezone.utc).timestamp())


class Settings(ctypes.Structure):
    _fields_ = [('schema', ctypes.c_uint8), ('mode', ctypes.c_uint8),
                ('interval_hours', ctypes.c_uint8), ('schedule', ctypes.c_uint8),
                ('install_minute', ctypes.c_uint16), ('timezone_auto', ctypes.c_bool),
                ('timezone', ctypes.c_char * 64)]


class History(ctypes.Structure):
    _fields_ = [('schema', ctypes.c_uint8), ('result', ctypes.c_uint8),
                ('pending_version', ctypes.c_char * 32), ('blocked_version', ctypes.c_char * 32),
                ('installed_version', ctypes.c_char * 32), ('checked_at', ctypes.c_int64),
                ('installed_at', ctypes.c_int64), ('install_day', ctypes.c_uint32)]


class OtaPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if os.name == 'nt':
            raise unittest.SkipTest('Native time policy tests run under Linux/WSL; IDF builds check Windows compilation')
        compiler = shutil.which('cc')
        if not compiler:
            raise RuntimeError('A native C compiler is required')
        cls.directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        library = Path(cls.directory.name) / 'policy.so'
        subprocess.run([compiler, '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror',
                        '-shared', '-fPIC', '-I', str(ROOT / 'main'), str(ROOT / 'main/app_ota_policy.c'),
                        str(ROOT / 'main/app_timezones.c'), '-o', str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        definitions = {
            'app_ota_settings_defaults': ([ctypes.POINTER(Settings)], None),
            'app_ota_settings_valid': ([ctypes.POINTER(Settings)], ctypes.c_bool),
            'app_ota_version_newer': ([ctypes.c_char_p, ctypes.c_char_p], ctypes.c_bool),
            'app_ota_auto_version_allowed': ([ctypes.c_char_p, ctypes.c_char_p], ctypes.c_bool),
            'app_timezone_count': ([], ctypes.c_size_t),
            'app_timezone_name': ([ctypes.c_size_t], ctypes.c_char_p),
            'app_timezone_offset': ([ctypes.c_char_p, ctypes.c_int64, ctypes.POINTER(ctypes.c_int32)], ctypes.c_bool),
            'app_timezone_resolve_local': ([ctypes.c_char_p, ctypes.c_int64, ctypes.POINTER(ctypes.c_int64)], ctypes.c_bool),
            'app_ota_next_install': ([ctypes.POINTER(Settings), ctypes.c_int64, ctypes.c_uint32], ctypes.c_int64),
            'app_ota_install_due': ([ctypes.POINTER(Settings), ctypes.c_int64, ctypes.c_uint32], ctypes.c_bool),
            'app_ota_local_day': ([ctypes.c_char_p, ctypes.c_int64], ctypes.c_uint32),
            'app_ota_retry_seconds': ([ctypes.c_uint], ctypes.c_uint32),
            'app_ota_history_finish_boot': ([ctypes.POINTER(History), ctypes.c_char_p, ctypes.c_int64], ctypes.c_bool),
        }
        for name, (arguments, result) in definitions.items():
            function = getattr(cls.lib, name)
            function.argtypes, function.restype = arguments, result

    def settings(self, **changes):
        value = Settings()
        self.lib.app_ota_settings_defaults(ctypes.byref(value))
        for key, item in changes.items():
            setattr(value, key, item)
        return value

    def offset(self, zone, date):
        value = ctypes.c_int32()
        self.assertTrue(self.lib.app_timezone_offset(zone.encode(), epoch(date), ctypes.byref(value)))
        return value.value

    def resolve(self, zone, date):
        value = ctypes.c_int64()
        self.assertTrue(self.lib.app_timezone_resolve_local(zone.encode(), epoch(date), ctypes.byref(value)))
        return value.value

    def test_default_off_and_validated_settings(self):
        value = self.settings()
        self.assertEqual((value.mode, value.interval_hours, value.install_minute), (0, 24, 180))
        self.assertTrue(self.lib.app_ota_settings_valid(ctypes.byref(value)))
        for changes in [{'mode': 3}, {'interval_hours': 2}, {'install_minute': 1440},
                        {'timezone': b'not-a-zone'}, {'mode': 2}]:
            with self.subTest(changes=changes):
                invalid = self.settings(**changes)
                self.assertFalse(self.lib.app_ota_settings_valid(ctypes.byref(invalid)))
        for changes in [{'mode': 2, 'timezone': b'Europe/Berlin'}, {'mode': 2, 'schedule': 1}]:
            valid = self.settings(**changes)
            self.assertTrue(self.lib.app_ota_settings_valid(ctypes.byref(valid)))

    def test_browser_zone_identifiers_and_aliases(self):
        self.assertGreater(self.lib.app_timezone_count(), 500)
        names = [self.lib.app_timezone_name(i) for i in range(self.lib.app_timezone_count())]
        self.assertEqual(names, sorted(names))
        for alias in ['Europe/Kiev', 'Europe/Kyiv', 'Asia/Calcutta', 'Asia/Kolkata', 'UTC', 'Etc/UTC']:
            self.assertIn(alias.encode(), names)

    def test_offsets_include_dst_and_quarter_hour_zones(self):
        for zone, winter, summer in [('Europe/Berlin', 3600, 7200), ('America/New_York', -18000, -14400),
                                     ('Asia/Kathmandu', 20700, 20700), ('Australia/Lord_Howe', 39600, 37800),
                                     ('Pacific/Chatham', 49500, 45900)]:
            with self.subTest(zone=zone):
                self.assertEqual(self.offset(zone, '2026-01-15T12:00:00'), winter)
                self.assertEqual(self.offset(zone, '2026-07-15T12:00:00'), summer)

    def test_irregular_ramadan_rules_are_not_replaced_by_fixed_posix_offset(self):
        self.assertEqual(self.offset('Africa/Casablanca', '2026-02-20T12:00:00'), 0)
        self.assertEqual(self.offset('Africa/Casablanca', '2026-04-20T12:00:00'), 3600)

    def test_nonexistent_time_moves_forward_and_repeated_time_uses_first_occurrence(self):
        self.assertEqual(self.resolve('Europe/Berlin', '2026-03-29T02:30:00'), epoch('2026-03-29T01:30:00'))
        self.assertEqual(self.resolve('Europe/Berlin', '2026-10-25T02:30:00'), epoch('2026-10-25T00:30:00'))

    def test_scheduled_window_and_reboot_do_not_repeat_same_day(self):
        settings = self.settings(mode=2, timezone=b'Europe/Berlin')
        start = epoch('2026-01-15T02:00:00')
        for delta, expected in [(-1, False), (0, True), (299, True), (300, False)]:
            self.assertEqual(self.lib.app_ota_install_due(ctypes.byref(settings), start + delta, 0), expected)
        day = self.lib.app_ota_local_day(settings.timezone, start)
        self.assertEqual(day, 20260115)
        self.assertFalse(self.lib.app_ota_install_due(ctypes.byref(settings), start + 30, day))
        self.assertEqual(self.lib.app_ota_next_install(ctypes.byref(settings), start + 30, day), start + 86400)

    def test_dst_next_day_uses_local_calendar_instead_of_24_hours(self):
        settings = self.settings(mode=2, timezone=b'Europe/Berlin')
        self.assertEqual(self.lib.app_ota_next_install(ctypes.byref(settings), epoch('2026-03-28T02:01:00'), 20260328),
                         epoch('2026-03-29T01:00:00'))
        self.assertEqual(self.lib.app_ota_next_install(ctypes.byref(settings), epoch('2026-10-24T01:01:00'), 20261024),
                         epoch('2026-10-25T02:00:00'))

    def test_clock_unknown_or_zone_invalid_never_schedules_install(self):
        settings = self.settings(mode=2, timezone=b'Europe/Berlin')
        for now in [0, epoch('2019-01-01'), epoch('2101-01-01')]:
            self.assertFalse(self.lib.app_ota_install_due(ctypes.byref(settings), now, 0))
            self.assertEqual(self.lib.app_ota_next_install(ctypes.byref(settings), now, 0), 0)

    def test_numeric_versions_and_failed_version_quarantine(self):
        self.assertTrue(self.lib.app_ota_version_newer(b'1.0.1017', b'1.0.103'))
        for version in [b'1.0.1016', b'1.0.1017', b'invalid']:
            self.assertFalse(self.lib.app_ota_auto_version_allowed(version, b'1.0.1017'))
        self.assertTrue(self.lib.app_ota_auto_version_allowed(b'1.0.1018', b'1.0.1017'))

    def test_rollback_marker_survives_reboot_and_manual_retry_can_clear_it(self):
        history = History(schema=1, pending_version=b'1.0.12')
        self.assertTrue(self.lib.app_ota_history_finish_boot(ctypes.byref(history), b'1.0.11', 0))
        self.assertEqual((history.result, history.blocked_version, history.pending_version), (6, b'1.0.12', b''))
        self.assertFalse(self.lib.app_ota_history_finish_boot(ctypes.byref(history), b'1.0.11', 0))
        history.pending_version = b'1.0.12'
        self.assertTrue(self.lib.app_ota_history_finish_boot(ctypes.byref(history), b'1.0.12', 123))
        self.assertEqual((history.result, history.blocked_version, history.installed_version, history.installed_at),
                         (5, b'', b'1.0.12', 123))

    def test_failed_older_boot_does_not_clear_a_newer_blocked_version(self):
        history = History(schema=1, pending_version=b'1.0.12', blocked_version=b'1.0.14')
        self.lib.app_ota_history_finish_boot(ctypes.byref(history), b'1.0.11', 0)
        self.assertEqual(history.blocked_version, b'1.0.14')

    def test_network_failure_backoff_is_bounded(self):
        self.assertEqual([self.lib.app_ota_retry_seconds(i) for i in [1, 2, 3, 10]], [900, 3600, 21600, 21600])


if __name__ == '__main__':
    unittest.main()
