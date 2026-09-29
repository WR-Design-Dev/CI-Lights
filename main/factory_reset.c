#include "factory_reset.h"

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "traffic_light.h"

#define FACTORY_RESET_BUTTON_GPIO GPIO_NUM_0
#define FACTORY_RESET_HOLD_TIME_MS 5000
#define FACTORY_RESET_POLL_INTERVAL_MS 50

static const char *TAG = "factory_reset";

static void factory_reset_task(void *argument)
{
    (void) argument;
    TickType_t pressed_since = 0;

    for (;;) {
        if (gpio_get_level(FACTORY_RESET_BUTTON_GPIO) == 0) {
            if (pressed_since == 0) {
                pressed_since = xTaskGetTickCount();
            } else if (xTaskGetTickCount() - pressed_since >=
                       pdMS_TO_TICKS(FACTORY_RESET_HOLD_TIME_MS)) {
                ESP_LOGW(TAG, "BOOT wurde 5 Sekunden gehalten; setze Einstellungen zurueck");
                if (app_config_factory_reset()) {
                    traffic_light_set(TRAFFIC_LIGHT_OFF);
                    vTaskDelay(pdMS_TO_TICKS(200));
                    esp_restart();
                }
                pressed_since = 0;
            }
        } else {
            pressed_since = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(FACTORY_RESET_POLL_INTERVAL_MS));
    }
}

void factory_reset_button_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << FACTORY_RESET_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    if (xTaskCreate(factory_reset_task, "factory_reset", 3072, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Reset-Taste konnte nicht eingerichtet werden");
    }
}
