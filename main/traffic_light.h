#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"

#define TRAFFIC_LIGHT_PROVISION_CODE_LENGTH 8
#define TRAFFIC_LIGHT_PROVISION_COLOR_COUNT 6
#define TRAFFIC_LIGHT_PROVISION_SYMBOL_COUNT 18

typedef enum {
    TRAFFIC_LIGHT_OFF,
    TRAFFIC_LIGHT_RED,
    TRAFFIC_LIGHT_YELLOW,
    TRAFFIC_LIGHT_GREEN,
    TRAFFIC_LIGHT_GREY,
} traffic_light_color_t;

void traffic_light_init(void);
void traffic_light_set(traffic_light_color_t color);
void traffic_light_set_build_running(traffic_light_color_t color);
void traffic_light_start_all_blink_animation(void);
void traffic_light_start_provision_code_animation(
    const uint8_t symbols[TRAFFIC_LIGHT_PROVISION_CODE_LENGTH]);
void traffic_light_start_query_animation(void);
void traffic_light_start_error_sos_animation(void);
void traffic_light_stop_query_animation(void);
void traffic_light_set_control_mode(app_control_mode_t mode);
app_control_mode_t traffic_light_control_mode(void);
bool traffic_light_is_pulsing(void);
bool traffic_light_is_blinking(void);
bool traffic_light_is_grey(void);
void traffic_light_set_build_effect(app_build_effect_t effect);
void traffic_light_set_manual(app_manual_light_t light, bool enabled);
bool traffic_light_manual_state(app_manual_light_t light);
void traffic_light_set_manual_color(app_manual_light_t light, uint8_t red, uint8_t green,
                                    uint8_t blue);
void traffic_light_manual_color(app_manual_light_t light, uint8_t *red, uint8_t *green,
                                uint8_t *blue);
void traffic_light_set_api_status(app_light_status_t status);
void traffic_light_set_disco_effect(app_disco_effect_t effect);
app_disco_effect_t traffic_light_disco_effect(void);
void traffic_light_set_brightness(uint8_t percent);
uint8_t traffic_light_brightness(void);
