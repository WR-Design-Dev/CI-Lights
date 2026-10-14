"""Compile the actual hostname policy and check conflicts, restart/reset, and input boundaries."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
HARNESS = r'''
#include "app_hostname_policy.h"
#include <assert.h>
#include <string.h>
int main(void) {
    char normalized[APP_HOSTNAME_MAX_LENGTH + 1];
    assert(app_hostname_normalize("Build-Ampel", normalized));
    assert(strcmp(normalized, "build-ampel") == 0);
    assert(app_hostname_normalize("", normalized));
    assert(app_hostname_normalize("7", normalized));
    const char *bad[] = {"-ampel", "ampel-", "ampel.local", "http://ampel", "ampel_1", "ampel 1", "ampel\n", "\xc3\xa4mpel"};
    for (unsigned i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) assert(!app_hostname_normalize(bad[i], normalized));
    assert(!app_hostname_normalize(NULL, normalized));
    char boundary[34]; memset(boundary, 'a', 32); boundary[32] = 0;
    assert(app_hostname_normalize(boundary, normalized));
    boundary[32] = 'a'; boundary[33] = 0;
    assert(!app_hostname_normalize(boundary, normalized));
    const uint8_t mac[6] = {0xd4, 5, 0x92, 0x40, 0xd2, 0x5c};
    app_hostname_policy_t policy;
    app_hostname_policy_init(&policy, "build-ampel", mac);
    assert(strcmp(policy.active, "build-ampel") == 0);
    assert(!app_hostname_policy_observe(&policy, "build-ampel"));
    assert(app_hostname_policy_observe(&policy, "build-ampel-2"));
    assert(policy.fallback && strcmp(policy.configured, "build-ampel") == 0);
    assert(!app_hostname_policy_using_default(&policy));
    assert(!app_hostname_policy_observe(&policy, "ci-lights-d25c"));
    assert(app_hostname_policy_using_default(&policy));
    assert(!app_hostname_policy_observe(&policy, "ci-lights-d25c-2"));
    assert(app_hostname_policy_using_default(&policy));
    assert(!app_hostname_policy_observe(&policy, "ci-lights-d25c-other"));
    assert(!app_hostname_policy_using_default(&policy));
    /* Restart keeps the saved request and tries it again. Factory reset clears it. */
    app_hostname_policy_init(&policy, "build-ampel", mac);
    assert(!policy.fallback && strcmp(policy.active, "build-ampel") == 0);
    app_hostname_policy_init(&policy, "", mac);
    assert(strcmp(policy.active, "ci-lights-d25c") == 0);
    assert(!app_hostname_policy_observe(&policy, "ci-lights-d25c-2"));
    assert(!policy.fallback && app_hostname_policy_using_default(&policy));
    app_hostname_policy_init(&policy, "invalid.local", mac);
    assert(strcmp(policy.active, "ci-lights-d25c") == 0);
    return 0;
}
'''


class HostnamePolicyTests(unittest.TestCase):
    def test_compiled_policy(self):
        compiler = shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if compiler is None:
            if os.name == 'nt':
                self.skipTest('Native C policy test runs with cc on GitHub Linux; ESP-IDF builds check Windows compilation')
            self.fail('A native C compiler is required for hostname policy verification')
        with tempfile.TemporaryDirectory() as directory:
            harness = Path(directory) / 'check.c'
            harness.write_text(HARNESS, encoding='ascii')
            executable = Path(directory) / ('check.exe' if os.name == 'nt' else 'check')
            subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'main'),
                            str(ROOT / 'main/app_hostname_policy.c'), str(harness), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    unittest.main()
