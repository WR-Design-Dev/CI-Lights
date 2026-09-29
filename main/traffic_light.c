#include "traffic_light.h"

#include "driver/rmt_tx.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TRAFFIC_LIGHT_WS2812_GPIO GPIO_NUM_1
#define ONBOARD_RGB_GPIO GPIO_NUM_21
#define TRAFFIC_LIGHT_WS2812_LED_COUNT 3
#define TRAFFIC_LIGHT_WS2812_RED_INDEX 2
#define TRAFFIC_LIGHT_WS2812_YELLOW_INDEX 1
#define TRAFFIC_LIGHT_WS2812_GREEN_INDEX 0
#define WS2812_COLOR_BYTES_PER_LED 3
#define WS2812_RMT_RESOLUTION_HZ 10000000
#define STARTUP_PULSE_INTERVAL_MS 80
#define STARTUP_PULSE_STEP 2
#define BUILD_PULSE_INTERVAL_MS 80
#define BUILD_PULSE_HALF_CYCLE_STEPS 12
#define BUILD_PULSE_MINIMUM_PERCENT 10
#define QUERY_BLINK_INTERVAL_MS 75
#define QUERY_BLINK_PAUSE_MS 38
#define QUERY_SEQUENCE_PAUSE_MS 1000
#define SOS_DOT_INTERVAL_MS 150
#define DISCO_FRAME_INTERVAL_MS 85

static const char *TAG = "traffic_light";
static bool s_red_light_enabled;
static bool s_yellow_light_enabled;
static bool s_green_light_enabled;
static bool s_grey_light_enabled;
static app_control_mode_t s_control_mode;
static app_disco_effect_t s_disco_effect;
static uint8_t s_brightness_percent;
static rmt_channel_handle_t s_ws2812_channel;
static rmt_encoder_handle_t s_ws2812_encoder;
static SemaphoreHandle_t s_ws2812_mutex;
static bool s_ws2812_available;
static rmt_channel_handle_t s_onboard_rgb_channel;
static rmt_encoder_handle_t s_onboard_rgb_encoder;
static SemaphoreHandle_t s_onboard_rgb_mutex;
static bool s_onboard_rgb_available;
typedef enum {
    TRAFFIC_LIGHT_ANIMATION_NONE,
    TRAFFIC_LIGHT_ANIMATION_STARTUP_PULSE,
    TRAFFIC_LIGHT_ANIMATION_QUERY,
    TRAFFIC_LIGHT_ANIMATION_ERROR_SOS,
    TRAFFIC_LIGHT_ANIMATION_BUILD_PULSE,
    TRAFFIC_LIGHT_ANIMATION_DISCO,
} traffic_light_animation_t;

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} rgb_color_t;

/* The WS2812 data order runs from the bottom LED to the top LED. */
static rgb_color_t s_manual_red_color;
static rgb_color_t s_manual_yellow_color;
static rgb_color_t s_manual_green_color;

typedef struct {
    bool red_on;
    uint16_t duration_ms;
} sos_step_t;

static volatile traffic_light_animation_t s_animation;
static TaskHandle_t s_query_animation_task;
static uint32_t s_disco_random_state = 0x8f4c2a19;

static void start_animation(traffic_light_animation_t animation);

static uint8_t ws2812_brightness(void)
{
    return (uint8_t) (((uint16_t) s_brightness_percent * 255U + 50U) / 100U);
}

static const sos_step_t SOS_PATTERN[] = {
    {true, SOS_DOT_INTERVAL_MS}, {false, SOS_DOT_INTERVAL_MS},
    {true, SOS_DOT_INTERVAL_MS}, {false, SOS_DOT_INTERVAL_MS},
    {true, SOS_DOT_INTERVAL_MS}, {false, SOS_DOT_INTERVAL_MS * 3},
    {true, SOS_DOT_INTERVAL_MS * 3}, {false, SOS_DOT_INTERVAL_MS},
    {true, SOS_DOT_INTERVAL_MS * 3}, {false, SOS_DOT_INTERVAL_MS},
    {true, SOS_DOT_INTERVAL_MS * 3}, {false, SOS_DOT_INTERVAL_MS * 3},
    {true, SOS_DOT_INTERVAL_MS}, {false, SOS_DOT_INTERVAL_MS},
    {true, SOS_DOT_INTERVAL_MS}, {false, SOS_DOT_INTERVAL_MS},
    {true, SOS_DOT_INTERVAL_MS}, {false, SOS_DOT_INTERVAL_MS * 7},
};

static const uint8_t QUERY_SEQUENCE[] = {0, 1, 2, 1, 0};

static const rmt_symbol_word_t WS2812_ZERO = {
    .level0 = 1,
    .duration0 = 3,
    .level1 = 0,
    .duration1 = 9,
};

static const rmt_symbol_word_t WS2812_ONE = {
    .level0 = 1,
    .duration0 = 9,
    .level1 = 0,
    .duration1 = 3,
};

static const rmt_symbol_word_t WS2812_RESET = {
    .level0 = 0,
    .duration0 = WS2812_RMT_RESOLUTION_HZ / 1000000 * 30,
    .level1 = 0,
    .duration1 = WS2812_RMT_RESOLUTION_HZ / 1000000 * 30,
};

static size_t ws2812_encoder_callback(const void *data, size_t data_size,
                                      size_t symbols_written, size_t symbols_free,
                                      rmt_symbol_word_t *symbols, bool *done, void *argument)
{
    (void) argument;
    if (symbols_free < 8) {
        return 0;
    }

    size_t data_position = symbols_written / 8;
    const uint8_t *bytes = data;
    if (data_position >= data_size) {
        symbols[0] = WS2812_RESET;
        *done = true;
        return 1;
    }

    uint8_t value = bytes[data_position];
    for (size_t bit = 0; bit < 8; bit++) {
        symbols[bit] = (value & (0x80 >> bit)) ? WS2812_ONE : WS2812_ZERO;
    }
    return 8;
}

static bool transmit_ws2812_pixels(rmt_channel_handle_t channel, rmt_encoder_handle_t encoder,
                                   SemaphoreHandle_t mutex, const uint8_t *pixels,
                                   size_t pixel_data_size, const char *light_name)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    const rmt_transmit_config_t transmit_config = {0};
    esp_err_t err = rmt_transmit(channel, encoder, pixels, pixel_data_size,
                                 &transmit_config);
    if (err == ESP_OK) {
        err = rmt_tx_wait_all_done(channel, portMAX_DELAY);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s konnte nicht gesetzt werden: %s", light_name, esp_err_to_name(err));
    }
    xSemaphoreGive(mutex);
    return err == ESP_OK;
}

static void set_ws2812_pixels(const uint8_t *pixels, size_t pixel_data_size)
{
    if (!s_ws2812_available) {
        return;
    }

    if (!transmit_ws2812_pixels(s_ws2812_channel, s_ws2812_encoder, s_ws2812_mutex,
                                pixels, pixel_data_size, "WS2812-Ampel")) {
        s_ws2812_available = false;
    }
}

static void set_ws2812_pixel(uint8_t *pixels, size_t led_index, uint8_t red, uint8_t green,
                             uint8_t blue)
{
    size_t offset = led_index * WS2812_COLOR_BYTES_PER_LED;
    /* WS2812 LEDs expect their colour components in GRB order. */
    pixels[offset] = green;
    pixels[offset + 1] = red;
    pixels[offset + 2] = blue;
}

static void set_onboard_rgb_color(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!s_onboard_rgb_available) {
        return;
    }

    uint8_t pixels[WS2812_COLOR_BYTES_PER_LED] = {0};
    set_ws2812_pixel(pixels, 0, red, green, blue);
    if (!transmit_ws2812_pixels(s_onboard_rgb_channel, s_onboard_rgb_encoder,
                                s_onboard_rgb_mutex, pixels, sizeof(pixels),
                                "Eingebaute RGB-LED")) {
        s_onboard_rgb_available = false;
    }
}

static void set_onboard_rgb_levels_with_brightness(bool red, bool yellow, bool green,
                                                   uint8_t brightness)
{
    set_onboard_rgb_color((red || yellow) ? brightness : 0,
                          (green || yellow) ? brightness : 0, 0);
}

static void set_ws2812_color(uint8_t red, uint8_t green, uint8_t blue)
{
    uint8_t pixels[TRAFFIC_LIGHT_WS2812_LED_COUNT * WS2812_COLOR_BYTES_PER_LED] = {0};
    for (size_t index = 0; index < TRAFFIC_LIGHT_WS2812_LED_COUNT; index++) {
        set_ws2812_pixel(pixels, index, red, green, blue);
    }
    set_ws2812_pixels(pixels, sizeof(pixels));
}

static void set_ws2812_levels_with_brightness(bool red, bool yellow, bool green,
                                              uint8_t brightness)
{
    uint8_t pixels[TRAFFIC_LIGHT_WS2812_LED_COUNT * WS2812_COLOR_BYTES_PER_LED] = {0};
    set_ws2812_pixel(pixels, TRAFFIC_LIGHT_WS2812_RED_INDEX,
                     red ? brightness : 0, 0, 0);
    set_ws2812_pixel(pixels, TRAFFIC_LIGHT_WS2812_YELLOW_INDEX,
                     yellow ? brightness : 0, yellow ? brightness : 0, 0);
    set_ws2812_pixel(pixels, TRAFFIC_LIGHT_WS2812_GREEN_INDEX,
                     0, green ? brightness : 0, 0);
    set_ws2812_pixels(pixels, sizeof(pixels));
    set_onboard_rgb_levels_with_brightness(red, yellow, green, brightness);
}

static void set_ws2812_levels(bool red, bool yellow, bool green)
{
    set_ws2812_levels_with_brightness(red, yellow, green, ws2812_brightness());
}

static rgb_color_t color_wheel(uint8_t position, uint8_t brightness)
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;

    if (position < 85) {
        red = 255 - position * 3;
        green = position * 3;
        blue = 0;
    } else if (position < 170) {
        position -= 85;
        red = 0;
        green = 255 - position * 3;
        blue = position * 3;
    } else {
        position -= 170;
        red = position * 3;
        green = 0;
        blue = 255 - position * 3;
    }

    return (rgb_color_t) {
        .red = (uint8_t) (((uint16_t) red * brightness) / 255),
        .green = (uint8_t) (((uint16_t) green * brightness) / 255),
        .blue = (uint8_t) (((uint16_t) blue * brightness) / 255),
    };
}

static uint8_t disco_random_byte(void)
{
    s_disco_random_state = s_disco_random_state * 1664525U + 1013904223U;
    return (uint8_t) (s_disco_random_state >> 24);
}

static void set_disco_pixels(const rgb_color_t colors[TRAFFIC_LIGHT_WS2812_LED_COUNT])
{
    uint8_t pixels[TRAFFIC_LIGHT_WS2812_LED_COUNT * WS2812_COLOR_BYTES_PER_LED] = {0};
    uint16_t red = 0;
    uint16_t green = 0;
    uint16_t blue = 0;

    for (size_t index = 0; index < TRAFFIC_LIGHT_WS2812_LED_COUNT; index++) {
        set_ws2812_pixel(pixels, index, colors[index].red, colors[index].green,
                         colors[index].blue);
        red += colors[index].red;
        green += colors[index].green;
        blue += colors[index].blue;
    }
    set_ws2812_pixels(pixels, sizeof(pixels));
    set_onboard_rgb_color((uint8_t) (red / TRAFFIC_LIGHT_WS2812_LED_COUNT),
                          (uint8_t) (green / TRAFFIC_LIGHT_WS2812_LED_COUNT),
                          (uint8_t) (blue / TRAFFIC_LIGHT_WS2812_LED_COUNT));
}

static rgb_color_t color_with_brightness(rgb_color_t color, uint8_t brightness)
{
    return (rgb_color_t) {
        .red = (uint8_t) (((uint16_t) color.red * brightness) / 255),
        .green = (uint8_t) (((uint16_t) color.green * brightness) / 255),
        .blue = (uint8_t) (((uint16_t) color.blue * brightness) / 255),
    };
}

static void set_manual_light_colors(void)
{
    uint8_t brightness = ws2812_brightness();
    rgb_color_t colors[TRAFFIC_LIGHT_WS2812_LED_COUNT] = {0};

    if (s_red_light_enabled) {
        colors[TRAFFIC_LIGHT_WS2812_RED_INDEX] =
            color_with_brightness(s_manual_red_color, brightness);
    }
    if (s_yellow_light_enabled) {
        colors[TRAFFIC_LIGHT_WS2812_YELLOW_INDEX] =
            color_with_brightness(s_manual_yellow_color, brightness);
    }
    if (s_green_light_enabled) {
        colors[TRAFFIC_LIGHT_WS2812_GREEN_INDEX] =
            color_with_brightness(s_manual_green_color, brightness);
    }
    set_disco_pixels(colors);
}

static void render_disco_frame(uint32_t frame)
{
    rgb_color_t colors[TRAFFIC_LIGHT_WS2812_LED_COUNT] = {0};
    uint8_t hue = (uint8_t) (frame * 4U);
    uint8_t brightness = ws2812_brightness();

    switch (s_disco_effect) {
    case APP_DISCO_EFFECT_RAINBOW:
        for (size_t index = 0; index < TRAFFIC_LIGHT_WS2812_LED_COUNT; index++) {
            colors[index] = color_wheel((uint8_t) (hue + index * 85U), brightness);
        }
        break;
    case APP_DISCO_EFFECT_COLORLOOP:
        for (size_t index = 0; index < TRAFFIC_LIGHT_WS2812_LED_COUNT; index++) {
            colors[index] = color_wheel(hue, brightness);
        }
        break;
    case APP_DISCO_EFFECT_CHASE:
        colors[frame % TRAFFIC_LIGHT_WS2812_LED_COUNT] = color_wheel(hue, brightness);
        break;
    case APP_DISCO_EFFECT_RAINBOW_CHASE:
        for (size_t index = 0; index < TRAFFIC_LIGHT_WS2812_LED_COUNT; index++) {
            colors[index] = color_wheel((uint8_t) (hue + index * 85U), brightness / 5);
        }
        colors[frame % TRAFFIC_LIGHT_WS2812_LED_COUNT] = (rgb_color_t) {
            .red = brightness,
            .green = brightness,
            .blue = brightness,
        };
        break;
    case APP_DISCO_EFFECT_BLINK:
        if ((frame / 3U) % 2U == 0) {
            rgb_color_t blink_color = color_wheel((uint8_t) ((frame / 6U) * 85U),
                                                  brightness);
            for (size_t index = 0; index < TRAFFIC_LIGHT_WS2812_LED_COUNT; index++) {
                colors[index] = blink_color;
            }
        }
        break;
    case APP_DISCO_EFFECT_BREATHE: {
        uint8_t phase = (uint8_t) (frame % 64U);
        uint8_t amplitude = phase < 32 ? phase : 63 - phase;
        rgb_color_t breathe_color = color_wheel(hue,
                                                (uint8_t) ((amplitude * brightness) / 31));
        for (size_t index = 0; index < TRAFFIC_LIGHT_WS2812_LED_COUNT; index++) {
            colors[index] = breathe_color;
        }
        break;
    }
    case APP_DISCO_EFFECT_TWINKLE:
        for (size_t index = 0; index < TRAFFIC_LIGHT_WS2812_LED_COUNT; index++) {
            colors[index] = (rgb_color_t) {.blue = 2};
        }
        colors[disco_random_byte() % TRAFFIC_LIGHT_WS2812_LED_COUNT] =
            color_wheel(disco_random_byte(), brightness);
        break;
    case APP_DISCO_EFFECT_SCAN: {
        static const uint8_t scan_positions[] = {0, 1, 2, 1};
        colors[scan_positions[frame % (sizeof(scan_positions) / sizeof(scan_positions[0]))]] =
            color_wheel(hue, brightness);
        break;
    }
    case APP_DISCO_EFFECT_THEATER_CHASE:
        for (size_t index = 0; index < TRAFFIC_LIGHT_WS2812_LED_COUNT; index++) {
            colors[index] = (index + frame) % TRAFFIC_LIGHT_WS2812_LED_COUNT == 0 ?
                color_wheel(hue, brightness) :
                color_wheel((uint8_t) (hue + 128U), brightness / 8);
        }
        break;
    case APP_DISCO_EFFECT_FIREWORKS: {
        size_t center = (frame / 7U) % TRAFFIC_LIGHT_WS2812_LED_COUNT;
        uint8_t phase = (uint8_t) (frame % 7U);
        rgb_color_t spark_color = color_wheel((uint8_t) ((frame / 7U) * 57U),
                                              brightness);
        if (phase < 2) {
            colors[center] = spark_color;
        } else if (phase < 5) {
            for (size_t index = 0; index < TRAFFIC_LIGHT_WS2812_LED_COUNT; index++) {
                uint8_t spark_brightness = index == center ? brightness / 2 : brightness / 3;
                colors[index] = color_wheel((uint8_t) ((frame / 7U) * 57U + index * 32U),
                                            spark_brightness);
            }
        }
        break;
    }
    case APP_DISCO_EFFECT_COUNT:
        break;
    }

    set_disco_pixels(colors);
}

static bool init_ws2812(rmt_channel_handle_t *channel, rmt_encoder_handle_t *encoder,
                        SemaphoreHandle_t *mutex, gpio_num_t gpio_num, const char *light_name)
{
    *mutex = xSemaphoreCreateMutex();
    if (*mutex == NULL) {
        ESP_LOGW(TAG, "Kein Speicher fuer %s", light_name);
        return false;
    }

    const rmt_tx_channel_config_t channel_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = gpio_num,
        .mem_block_symbols = 64,
        .resolution_hz = WS2812_RMT_RESOLUTION_HZ,
        .trans_queue_depth = 1,
    };
    esp_err_t err = rmt_new_tx_channel(&channel_config, channel);
    if (err == ESP_OK) {
        const rmt_simple_encoder_config_t encoder_config = {
            .callback = ws2812_encoder_callback,
        };
        err = rmt_new_simple_encoder(&encoder_config, encoder);
    }
    if (err == ESP_OK) {
        err = rmt_enable(*channel);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s ist nicht verfuegbar: %s", light_name, esp_err_to_name(err));
        return false;
    }

    return true;
}

static void set_output_levels(bool red, bool yellow, bool green)
{
    set_ws2812_levels(red, yellow, green);
}

static void set_output_levels_with_brightness(bool red, bool yellow, bool green,
                                              uint8_t brightness)
{
    set_ws2812_levels_with_brightness(red, yellow, green, brightness);
}

static void render_stored_status(void)
{
    if (s_control_mode == APP_CONTROL_MODE_AUTO && s_grey_light_enabled) {
        uint8_t grey = (uint8_t) (((uint16_t) ws2812_brightness() * 25U + 50U) / 100U);
        if (grey == 0) {
            grey = 1;
        }
        uint8_t pixels[TRAFFIC_LIGHT_WS2812_LED_COUNT * WS2812_COLOR_BYTES_PER_LED] = {0};
        set_ws2812_pixel(pixels, TRAFFIC_LIGHT_WS2812_YELLOW_INDEX, grey, grey, grey);
        set_ws2812_pixels(pixels, sizeof(pixels));
        set_onboard_rgb_color(grey, grey, grey);
    } else {
        set_output_levels(s_red_light_enabled, s_yellow_light_enabled, s_green_light_enabled);
    }
}

void traffic_light_set(traffic_light_color_t color)
{
    if (s_control_mode != APP_CONTROL_MODE_AUTO) {
        ESP_LOGI(TAG, "Jenkins-Status wird durch die gewaehlte Betriebsart ignoriert");
        return;
    }

    s_red_light_enabled = color == TRAFFIC_LIGHT_RED;
    s_yellow_light_enabled = color == TRAFFIC_LIGHT_YELLOW;
    s_green_light_enabled = color == TRAFFIC_LIGHT_GREEN;
    s_grey_light_enabled = color == TRAFFIC_LIGHT_GREY;
    traffic_light_stop_query_animation();
    render_stored_status();
}

void traffic_light_set_pulsing(traffic_light_color_t color)
{
    if (s_control_mode != APP_CONTROL_MODE_AUTO) {
        ESP_LOGI(TAG, "Jenkins-Status wird durch die gewaehlte Betriebsart ignoriert");
        return;
    }

    s_red_light_enabled = color == TRAFFIC_LIGHT_RED;
    s_yellow_light_enabled = color == TRAFFIC_LIGHT_YELLOW;
    s_green_light_enabled = color == TRAFFIC_LIGHT_GREEN;
    s_grey_light_enabled = false;
    traffic_light_stop_query_animation();
    start_animation(TRAFFIC_LIGHT_ANIMATION_BUILD_PULSE);
}

static void query_animation_task(void *argument)
{
    (void) argument;
    bool query_light_on = false;
    size_t query_light_index = 0;
    traffic_light_animation_t previous_animation = TRAFFIC_LIGHT_ANIMATION_NONE;
    size_t sos_step_index = 0;
    uint8_t startup_brightness = ws2812_brightness();
    bool startup_dimming = true;
    uint8_t build_pulse_phase = BUILD_PULSE_HALF_CYCLE_STEPS;
    bool build_pulse_dimming = true;
    uint32_t disco_frame = 0;

    while (s_animation != TRAFFIC_LIGHT_ANIMATION_NONE) {
        traffic_light_animation_t animation = s_animation;
        if (animation != previous_animation) {
            query_light_on = false;
            query_light_index = 0;
            sos_step_index = 0;
            startup_brightness = ws2812_brightness();
            startup_dimming = true;
            build_pulse_phase = BUILD_PULSE_HALF_CYCLE_STEPS;
            build_pulse_dimming = true;
            disco_frame = 0;
            previous_animation = animation;
        }

        if (animation == TRAFFIC_LIGHT_ANIMATION_STARTUP_PULSE) {
            set_ws2812_color(startup_brightness, startup_brightness, startup_brightness);
            set_onboard_rgb_color(startup_brightness, startup_brightness, startup_brightness);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(STARTUP_PULSE_INTERVAL_MS));
            if (s_animation == TRAFFIC_LIGHT_ANIMATION_STARTUP_PULSE) {
                if (startup_dimming) {
                    if (startup_brightness <= STARTUP_PULSE_STEP) {
                        startup_brightness = 0;
                        startup_dimming = false;
                    } else {
                        startup_brightness -= STARTUP_PULSE_STEP;
                    }
                } else if (startup_brightness + STARTUP_PULSE_STEP >= ws2812_brightness()) {
                    startup_brightness = ws2812_brightness();
                    startup_dimming = true;
                } else {
                    startup_brightness += STARTUP_PULSE_STEP;
                }
            }
        } else if (animation == TRAFFIC_LIGHT_ANIMATION_QUERY) {
            if (query_light_on) {
                set_output_levels(false, false, false);
                query_light_on = false;
                bool sequence_finished = query_light_index ==
                                         (sizeof(QUERY_SEQUENCE) / sizeof(QUERY_SEQUENCE[0])) - 1;
                query_light_index = (query_light_index + 1) %
                                    (sizeof(QUERY_SEQUENCE) / sizeof(QUERY_SEQUENCE[0]));
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(sequence_finished ?
                                                        QUERY_SEQUENCE_PAUSE_MS :
                                                        QUERY_BLINK_PAUSE_MS));
            } else {
                uint8_t active_light = QUERY_SEQUENCE[query_light_index];
                set_output_levels(active_light == 0, active_light == 1, active_light == 2);
                query_light_on = true;
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(QUERY_BLINK_INTERVAL_MS));
            }
        } else if (animation == TRAFFIC_LIGHT_ANIMATION_ERROR_SOS) {
            const sos_step_t step = SOS_PATTERN[sos_step_index];
            set_output_levels(step.red_on, false, false);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(step.duration_ms));
            if (s_animation == TRAFFIC_LIGHT_ANIMATION_ERROR_SOS) {
                sos_step_index = (sos_step_index + 1) % (sizeof(SOS_PATTERN) /
                                                          sizeof(SOS_PATTERN[0]));
            }
        } else if (animation == TRAFFIC_LIGHT_ANIMATION_BUILD_PULSE) {
            uint8_t maximum_brightness = ws2812_brightness();
            uint8_t minimum_brightness = (uint8_t) (((uint16_t) maximum_brightness *
                                                      BUILD_PULSE_MINIMUM_PERCENT + 99U) /
                                                     100U);
            uint8_t build_pulse_brightness = (uint8_t) (minimum_brightness +
                (((uint16_t) (maximum_brightness - minimum_brightness) * build_pulse_phase +
                  BUILD_PULSE_HALF_CYCLE_STEPS / 2U) / BUILD_PULSE_HALF_CYCLE_STEPS));
            set_output_levels_with_brightness(s_red_light_enabled, s_yellow_light_enabled,
                                              s_green_light_enabled, build_pulse_brightness);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(BUILD_PULSE_INTERVAL_MS));
            if (s_animation == TRAFFIC_LIGHT_ANIMATION_BUILD_PULSE) {
                if (build_pulse_dimming) {
                    if (build_pulse_phase == 0) {
                        build_pulse_dimming = false;
                    } else {
                        --build_pulse_phase;
                    }
                } else if (build_pulse_phase >= BUILD_PULSE_HALF_CYCLE_STEPS) {
                    build_pulse_dimming = true;
                } else {
                    ++build_pulse_phase;
                }
            }
        } else if (animation == TRAFFIC_LIGHT_ANIMATION_DISCO) {
            render_disco_frame(disco_frame++);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(DISCO_FRAME_INTERVAL_MS));
        }
    }

    render_stored_status();
    s_query_animation_task = NULL;
    vTaskDelete(NULL);
}

static void start_animation(traffic_light_animation_t animation)
{
    bool disco_animation = animation == TRAFFIC_LIGHT_ANIMATION_DISCO;
    if ((disco_animation && s_control_mode != APP_CONTROL_MODE_DISCO) ||
        (!disco_animation && s_control_mode != APP_CONTROL_MODE_AUTO)) {
        return;
    }

    s_animation = animation;
    if (s_query_animation_task != NULL) {
        xTaskNotifyGive(s_query_animation_task);
        return;
    }

    if (xTaskCreate(query_animation_task, "jenkins_blink", 2048, NULL, 5,
                    &s_query_animation_task) != pdPASS) {
        s_animation = TRAFFIC_LIGHT_ANIMATION_NONE;
        s_query_animation_task = NULL;
        ESP_LOGE(TAG, "Blinkanimation konnte nicht gestartet werden");
    }
}

void traffic_light_start_query_animation(void)
{
    traffic_light_start_all_blink_animation();
}

void traffic_light_start_all_blink_animation(void)
{
    start_animation(TRAFFIC_LIGHT_ANIMATION_QUERY);
}

void traffic_light_start_error_sos_animation(void)
{
    start_animation(TRAFFIC_LIGHT_ANIMATION_ERROR_SOS);
}

void traffic_light_set_control_mode(app_control_mode_t mode)
{
    s_control_mode = mode;
    traffic_light_stop_query_animation();
    if (mode == APP_CONTROL_MODE_MANUAL) {
        set_manual_light_colors();
    } else if (mode == APP_CONTROL_MODE_DISCO) {
        start_animation(TRAFFIC_LIGHT_ANIMATION_DISCO);
    } else {
        render_stored_status();
    }
}

app_control_mode_t traffic_light_control_mode(void)
{
    return s_control_mode;
}

bool traffic_light_is_pulsing(void)
{
    return s_control_mode == APP_CONTROL_MODE_AUTO &&
           s_animation == TRAFFIC_LIGHT_ANIMATION_BUILD_PULSE;
}

bool traffic_light_is_grey(void)
{
    return s_control_mode == APP_CONTROL_MODE_AUTO && s_grey_light_enabled;
}

void traffic_light_stop_query_animation(void)
{
    if (s_query_animation_task == NULL) {
        return;
    }

    s_animation = TRAFFIC_LIGHT_ANIMATION_NONE;
    xTaskNotifyGive(s_query_animation_task);
    while (s_query_animation_task != NULL) {
        vTaskDelay(1);
    }
}

void traffic_light_set_manual(app_manual_light_t light, bool enabled)
{
    if (s_control_mode != APP_CONTROL_MODE_MANUAL) {
        return;
    }

    switch (light) {
    case APP_MANUAL_LIGHT_RED:
        s_red_light_enabled = enabled;
        set_manual_light_colors();
        break;
    case APP_MANUAL_LIGHT_YELLOW:
        s_yellow_light_enabled = enabled;
        set_manual_light_colors();
        break;
    case APP_MANUAL_LIGHT_GREEN:
        s_green_light_enabled = enabled;
        set_manual_light_colors();
        break;
    }
}

bool traffic_light_manual_state(app_manual_light_t light)
{
    switch (light) {
    case APP_MANUAL_LIGHT_RED:
        return s_red_light_enabled;
    case APP_MANUAL_LIGHT_YELLOW:
        return s_yellow_light_enabled;
    case APP_MANUAL_LIGHT_GREEN:
        return s_green_light_enabled;
    }
    return false;
}

void traffic_light_set_manual_color(app_manual_light_t light, uint8_t red, uint8_t green,
                                    uint8_t blue)
{
    if (s_control_mode != APP_CONTROL_MODE_MANUAL) {
        return;
    }

    rgb_color_t color = {.red = red, .green = green, .blue = blue};
    switch (light) {
    case APP_MANUAL_LIGHT_RED:
        s_manual_red_color = color;
        break;
    case APP_MANUAL_LIGHT_YELLOW:
        s_manual_yellow_color = color;
        break;
    case APP_MANUAL_LIGHT_GREEN:
        s_manual_green_color = color;
        break;
    }
    set_manual_light_colors();
}

void traffic_light_manual_color(app_manual_light_t light, uint8_t *red, uint8_t *green,
                                uint8_t *blue)
{
    rgb_color_t color = {0};
    switch (light) {
    case APP_MANUAL_LIGHT_RED:
        color = s_manual_red_color;
        break;
    case APP_MANUAL_LIGHT_YELLOW:
        color = s_manual_yellow_color;
        break;
    case APP_MANUAL_LIGHT_GREEN:
        color = s_manual_green_color;
        break;
    }
    if (red != NULL) {
        *red = color.red;
    }
    if (green != NULL) {
        *green = color.green;
    }
    if (blue != NULL) {
        *blue = color.blue;
    }
}

void traffic_light_set_api_status(app_light_status_t status)
{
    if (s_control_mode != APP_CONTROL_MODE_API) {
        return;
    }

    s_red_light_enabled = status == APP_LIGHT_STATUS_RED;
    s_yellow_light_enabled = status == APP_LIGHT_STATUS_YELLOW;
    s_green_light_enabled = status == APP_LIGHT_STATUS_GREEN;
    s_grey_light_enabled = false;
    set_output_levels(s_red_light_enabled, s_yellow_light_enabled, s_green_light_enabled);
}

void traffic_light_set_disco_effect(app_disco_effect_t effect)
{
    if (effect >= APP_DISCO_EFFECT_COUNT) {
        return;
    }

    s_disco_effect = effect;
    if (s_control_mode == APP_CONTROL_MODE_DISCO) {
        start_animation(TRAFFIC_LIGHT_ANIMATION_DISCO);
    }
}

app_disco_effect_t traffic_light_disco_effect(void)
{
    return s_disco_effect;
}

void traffic_light_set_brightness(uint8_t percent)
{
    if (percent < 1 || percent > 100) {
        return;
    }

    s_brightness_percent = percent;
    if (s_query_animation_task != NULL) {
        xTaskNotifyGive(s_query_animation_task);
    } else if (s_control_mode == APP_CONTROL_MODE_MANUAL) {
        set_manual_light_colors();
    } else {
        render_stored_status();
    }
}

uint8_t traffic_light_brightness(void)
{
    return s_brightness_percent;
}

void traffic_light_init(void)
{
    s_ws2812_available = init_ws2812(&s_ws2812_channel, &s_ws2812_encoder, &s_ws2812_mutex,
                                     TRAFFIC_LIGHT_WS2812_GPIO, "WS2812-Ampel");
    s_onboard_rgb_available = init_ws2812(&s_onboard_rgb_channel, &s_onboard_rgb_encoder,
                                           &s_onboard_rgb_mutex, ONBOARD_RGB_GPIO,
                                           "Eingebaute RGB-LED");
    s_control_mode = APP_CONTROL_MODE_AUTO;
    s_disco_effect = APP_DISCO_EFFECT_RAINBOW;
    s_brightness_percent = APP_LIGHT_BRIGHTNESS_DEFAULT_PERCENT;
    s_manual_red_color = (rgb_color_t) {.red = 255, .green = 0, .blue = 0};
    s_manual_yellow_color = (rgb_color_t) {.red = 255, .green = 255, .blue = 0};
    s_manual_green_color = (rgb_color_t) {.red = 0, .green = 255, .blue = 0};
    s_red_light_enabled = false;
    s_yellow_light_enabled = false;
    s_green_light_enabled = false;
    s_grey_light_enabled = false;
    set_ws2812_color(ws2812_brightness(), ws2812_brightness(), ws2812_brightness());
    set_onboard_rgb_color(ws2812_brightness(), ws2812_brightness(), ws2812_brightness());
    start_animation(TRAFFIC_LIGHT_ANIMATION_STARTUP_PULSE);
}
