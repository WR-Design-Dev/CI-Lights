#pragma once

#include "app_backup.h"

typedef enum {
    APP_BACKUP_TRANSFER_EXPORT,
    APP_BACKUP_TRANSFER_VALIDATE,
    APP_BACKUP_TRANSFER_IMPORT,
    APP_BACKUP_TRANSFER_PREPARED,
} app_backup_transfer_kind_t;

/* Initialize once at boot, before starting either web server. */
esp_err_t app_backup_transfer_init(void);
/* Takes ownership of request only on success. The HTTP caller authenticates it. */
esp_err_t app_backup_transfer_start(app_backup_transfer_kind_t kind, cJSON *request, char operation_id[65]);
esp_err_t app_backup_transfer_poll(const char *operation_id, cJSON **response);
/* Acknowledge only after sending a completed response successfully. */
void app_backup_transfer_acknowledge(const char *operation_id);
bool app_backup_transfer_running(void);
bool app_backup_transfer_restart_pending(void);
