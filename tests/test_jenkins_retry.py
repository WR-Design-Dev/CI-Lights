"""Exercise the production retry loops with simulated Jenkins requests and delays."""
import ctypes
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


def function_source(path, signature):
    source = (ROOT / path).read_text(encoding='utf-8')
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


HARNESS = r'''
#include "app_config.h"
typedef unsigned long long size_t;
typedef int cJSON;
#define NULL ((void *)0)
#define TRAFFIC_LIGHT_RED 1
#define ESP_LOGW(...) ((void)0)
#define pdMS_TO_TICKS(ms) (ms)

/* calls, delays, SOS, loading starts, successful status updates, invariant errors */
static int counters[6];
static int failures_left, may_retry, mode, cancel_on_delay, expect_loading;
static volatile bool s_request_in_progress;
static cJSON jobs;

static int traffic_light_control_mode(void) { return mode; }
static void traffic_light_start_query_animation(void) { ++counters[3]; }
static void traffic_light_set(int color) { if (color != TRAFFIC_LIGHT_RED) ++counters[5]; }
static void traffic_light_start_error_sos_animation(void) { ++counters[2]; }
static bool jenkins_client_request_once(const app_config_t *config, bool *retryable) {
    (void)config;
    ++counters[0];
    if (counters[2] || s_request_in_progress != expect_loading) ++counters[5];
    *retryable = may_retry;
    if (failures_left > 0) { --failures_left; return false; }
    ++counters[4];
    return true;
}
static cJSON *fetch_jenkins_job_level_once(const char *url, size_t first, bool *retryable) {
    if (url == NULL || first != 40) ++counters[5];
    return jenkins_client_request_once(NULL, retryable) ? &jobs : NULL;
}
static void vTaskDelay(unsigned ticks) {
    ++counters[1];
    if (ticks != 5000 || counters[2] || s_request_in_progress != expect_loading) ++counters[5];
    if (cancel_on_delay) mode = 1;
}
'''

ENTRY_POINTS = r'''
void run_case(int list, int failures, int retryable, int loading, int manual, int cancel) {
    for (unsigned i = 0; i < 6; ++i) counters[i] = 0;
    failures_left = failures;
    may_retry = retryable;
    mode = manual;
    cancel_on_delay = cancel;
    expect_loading = loading;
    s_request_in_progress = false;
    if (list) {
        cJSON *result = fetch_jenkins_job_level("jenkins", 40);
        if ((result != NULL) != (counters[4] != 0)) ++counters[5];
    } else {
        jenkins_client_request_internal(NULL, loading);
    }
    if (s_request_in_progress) ++counters[5];
}
int metric(unsigned index) { return counters[index]; }
uint32_t timeout_ms(uint32_t seconds) { return app_jenkins_timeout_ms(seconds); }
bool valid_timeout(uint32_t seconds) { return app_jenkins_timeout_is_valid(seconds); }
'''


class JenkinsRetryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if os.name == 'nt':
            raise unittest.SkipTest('Native retry tests run on GitHub Linux; ESP-IDF builds check Windows compilation')
        compiler = shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if compiler is None:
            raise RuntimeError('A native C compiler is required for retry verification')
        cls.directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        directory = Path(cls.directory.name)
        constants = '\n'.join(re.findall(r'^#define JENKINS_REQUEST_\w+ \d+$',
                                         (ROOT / 'main/jenkins_client.h').read_text(), re.M))
        source = directory / 'retry.c'
        source.write_text(constants + '\n' + HARNESS + '\n' +
                          function_source('main/jenkins_client.c',
                                          'static void jenkins_client_request_internal(') + '\n' +
                          function_source('main/app_config.c',
                                          'static cJSON *fetch_jenkins_job_level(') + '\n' +
                          ENTRY_POINTS, encoding='utf-8')
        library = directory / 'retry.so'
        subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-shared', '-fPIC', '-I', str(ROOT / 'main'),
                        str(source), '-o', str(library)], check=True)
        cls.library = ctypes.CDLL(str(library))
        cls.library.run_case.argtypes = [ctypes.c_int] * 6
        cls.library.run_case.restype = None
        cls.library.metric.argtypes = [ctypes.c_uint]
        cls.library.metric.restype = ctypes.c_int
        cls.library.timeout_ms.argtypes = [ctypes.c_uint32]
        cls.library.timeout_ms.restype = ctypes.c_uint32
        cls.library.valid_timeout.argtypes = [ctypes.c_uint32]
        cls.library.valid_timeout.restype = ctypes.c_bool

    def run_case(self, *, job_list=False, failures=0, retryable=True,
                 loading=True, manual=False, cancel=False):
        self.library.run_case(job_list, failures, retryable, loading, manual, cancel)
        result = [self.library.metric(i) for i in range(6)]
        self.assertEqual(result[5], 0, 'No SOS while retrying; loading stays active; page stays the same')
        return result[:5]

    def test_status_recovers_after_timeouts_without_sos(self):
        self.assertEqual(self.run_case(failures=2), [3, 2, 0, 1, 1])

    def test_periodic_poll_preserves_previous_status_while_retrying(self):
        self.assertEqual(self.run_case(failures=2, loading=False), [3, 2, 0, 0, 1])

    def test_status_shows_sos_only_after_last_attempt(self):
        self.assertEqual(self.run_case(failures=10), [3, 2, 1, 1, 0])

    def test_status_does_not_retry_permanent_errors(self):
        self.assertEqual(self.run_case(failures=10, retryable=False), [1, 0, 1, 1, 0])

    def test_manual_mode_skips_requests(self):
        self.assertEqual(self.run_case(manual=True), [0, 0, 0, 0, 0])

    def test_mode_change_cancels_retry_without_sos(self):
        self.assertEqual(self.run_case(failures=10, cancel=True), [1, 1, 0, 1, 0])

    def test_job_page_recovers_after_timeouts(self):
        self.assertEqual(self.run_case(job_list=True, failures=2, loading=False), [3, 2, 0, 0, 1])

    def test_job_page_stops_after_three_failures(self):
        self.assertEqual(self.run_case(job_list=True, failures=10, loading=False), [3, 2, 0, 0, 0])

    def test_job_page_does_not_retry_permanent_errors(self):
        self.assertEqual(self.run_case(job_list=True, failures=10, retryable=False, loading=False),
                         [1, 0, 0, 0, 0])

    def test_timeout_conversion_and_boundaries(self):
        for seconds, milliseconds in [(1, 1000), (60, 60000), (180, 180000), (600, 600000)]:
            with self.subTest(seconds=seconds):
                self.assertTrue(self.library.valid_timeout(seconds))
                self.assertEqual(self.library.timeout_ms(seconds), milliseconds)

    def test_invalid_or_missing_timeout_uses_default(self):
        for seconds in [0, 601, 2**32 - 1]:
            with self.subTest(seconds=seconds):
                self.assertFalse(self.library.valid_timeout(seconds))
                self.assertEqual(self.library.timeout_ms(seconds), 60000)


if __name__ == '__main__':
    unittest.main()
