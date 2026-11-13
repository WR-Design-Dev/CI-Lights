"""Exercise the production NVS adapter with missing, corrupt and failed storage."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
ERRORS = '''
#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_SIZE 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_INVALID_ARG 3
#define ESP_ERR_NVS_NOT_FOUND 4
'''
NVS = '''
#pragma once
#include <stddef.h>
#include "esp_err.h"
typedef int nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
esp_err_t nvs_open(const char *, int, nvs_handle_t *);
void nvs_close(nvs_handle_t);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_commit(nvs_handle_t);
'''
HARNESS = r'''
#include <assert.h>
#include <string.h>
#include "app_ota_storage.h"
#include "nvs.h"
const char *app_settings_namespace(void) { return "settings"; }
static unsigned char settings_blob[sizeof(app_ota_settings_t)];
static unsigned char history_blob[sizeof(app_ota_history_t)];
static size_t settings_size, history_size;
static int commit_error, writes, commits;
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) {
    assert(strcmp(name, "settings") == 0);
    (void)mode; *handle = 1; return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { assert(handle == 1); }
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *name, void *value, size_t *size) {
    assert(handle == 1);
    int settings = strcmp(name, "ota_auto") == 0;
    assert(settings || strcmp(name, "ota_history") == 0);
    size_t actual = settings ? settings_size : history_size;
    if (!actual) return ESP_ERR_NVS_NOT_FOUND;
    if (*size < actual) return ESP_ERR_INVALID_SIZE;
    memcpy(value, settings ? settings_blob : history_blob, actual);
    *size = actual; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *name, const void *value, size_t size) {
    assert(handle == 1); ++writes;
    if (strcmp(name, "ota_auto") == 0) {
        assert(size == sizeof(settings_blob)); settings_size = size; memcpy(settings_blob, value, size);
    } else {
        assert(strcmp(name, "ota_history") == 0 && size == sizeof(history_blob));
        history_size = size; memcpy(history_blob, value, size);
    }
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) { assert(handle == 1); ++commits; return commit_error; }
int main(void) {
    app_ota_settings_t settings, loaded;
    app_ota_history_t history, loaded_history;
    assert(app_ota_storage_load(&settings, &history) == ESP_OK);
    assert(settings.mode == APP_OTA_AUTO_OFF && settings.interval_hours == 24);
    assert(history.schema == 1 && !history.pending_version[0]);
    settings.mode = APP_OTA_AUTO_INSTALL;
    strcpy(settings.timezone, "Europe/Berlin");
    history.install_day = 20261113;
    strcpy(history.pending_version, "1.0.1018");
    assert(app_ota_storage_save_settings(&settings, &history) == ESP_OK);
    assert(writes == 2 && commits == 1);
    assert(app_ota_storage_load(&loaded, &loaded_history) == ESP_OK);
    assert(loaded.mode == APP_OTA_AUTO_INSTALL && strcmp(loaded.timezone, "Europe/Berlin") == 0);
    assert(loaded_history.install_day == 20261113 && strcmp(loaded_history.pending_version, "1.0.1018") == 0);
    commit_error = ESP_FAIL;
    assert(app_ota_storage_save_history(&history) == ESP_FAIL);
    assert(app_ota_storage_save_settings(&settings, &history) == ESP_FAIL);
    commit_error = ESP_OK;
    settings.mode = 255;
    int before = writes;
    assert(app_ota_storage_save_settings(&settings, &history) == ESP_ERR_INVALID_ARG && before == writes);
    history_blob[0] = 255;
    assert(app_ota_storage_load(&loaded, &loaded_history) == ESP_ERR_INVALID_STATE);
    assert(loaded.mode == APP_OTA_AUTO_OFF && !loaded_history.pending_version[0]);
    history_size = sizeof(history_blob) - 1;
    assert(app_ota_storage_load(&loaded, &loaded_history) == ESP_ERR_INVALID_SIZE);
    assert(loaded.mode == APP_OTA_AUTO_OFF);
    history_size = 0;
    assert(app_ota_storage_load(&loaded, &loaded_history) == ESP_OK);
    assert(loaded.mode == APP_OTA_AUTO_INSTALL && loaded_history.schema == 1);
    memset(history.pending_version, 'X', sizeof(history.pending_version));
    assert(app_ota_storage_save_history(&history) == ESP_ERR_INVALID_ARG);
    return 0;
}
'''


class OtaStorageTests(unittest.TestCase):
    def test_settings_history_and_storage_failures(self):
        if os.name == 'nt':
            self.skipTest('Native storage tests run under Linux/WSL')
        compiler = shutil.which('cc')
        self.assertIsNotNone(compiler, 'A native C compiler is required')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'esp_err.h').write_text(ERRORS)
            (root / 'nvs.h').write_text(NVS)
            (root / 'test.c').write_text(HARNESS)
            executable = root / 'storage-test'
            subprocess.run([compiler, '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror',
                            '-I', str(root), '-I', str(ROOT / 'main'), str(root / 'test.c'),
                            str(ROOT / 'main/app_ota_storage.c'), str(ROOT / 'main/app_ota_policy.c'),
                            str(ROOT / 'main/app_timezones.c'), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    unittest.main()
