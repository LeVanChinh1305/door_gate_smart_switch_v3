#include "app_led.h"

#include <string.h>

#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

static const char *TAG = "app_led";

#define DF_LED_RMT_RESOLUTION_HZ 10000000U   // 10 MHz -> 1 tick = 0.1 us

#define DF_LED_T0H_TICKS 4U
#define DF_LED_T0L_TICKS 8U
#define DF_LED_T1H_TICKS 8U
#define DF_LED_T1L_TICKS 4U
#define DF_LED_RESET_MS 1U
#define DF_LED_TIMEOUT_MS 200U
#define DF_LED_MEM_BLOCK_SYMBOLS 48U
#define DF_LED_TRANS_QUEUE_DEPTH 4U

static rmt_channel_handle_t g_rmt_channel = NULL;
static rmt_encoder_handle_t g_led_encoder = NULL;
static bool g_bIsReady = false;

/* Buffer màu "gốc" người dùng set (thứ tự RGB, chưa áp brightness) */
static uint8_t g_led_rgb[DF_LED_COUNT * 3U];

/* Buffer thực sự bắn ra LED (thứ tự GRB, đã áp brightness) */
static uint8_t g_led_data[DF_LED_COUNT * 3U];

static uint8_t g_u8Brightness = UINT8_MAX;

esp_err_t app_led_Init(gpio_num_t gpio)
{
    if (g_bIsReady) {
        ESP_LOGW(TAG, "app_led_Init() được gọi lại, bỏ qua");
        return ESP_OK;
    }

    rmt_tx_channel_config_t tx_config = {
        .gpio_num = gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = DF_LED_RMT_RESOLUTION_HZ,
        .mem_block_symbols = DF_LED_MEM_BLOCK_SYMBOLS,
        .trans_queue_depth = DF_LED_TRANS_QUEUE_DEPTH,
    };

    esp_err_t err = rmt_new_tx_channel(&tx_config, &g_rmt_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Tạo kênh truyền RMT thất bại: %s", esp_err_to_name(err));
        return err;
    }

    rmt_symbol_word_t bit0 = {
        .level0 = 1,
        .duration0 = DF_LED_T0H_TICKS,
        .level1 = 0,
        .duration1 = DF_LED_T0L_TICKS,
    };

    rmt_symbol_word_t bit1 = {
        .level0 = 1,
        .duration0 = DF_LED_T1H_TICKS,
        .level1 = 0,
        .duration1 = DF_LED_T1L_TICKS,
    };

    rmt_bytes_encoder_config_t encoder_config = {
        .bit0 = bit0,
        .bit1 = bit1,
        .flags.msb_first = 1,
    };

    err = rmt_new_bytes_encoder(&encoder_config, &g_led_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Tạo bộ mã hóa byte RMT thất bại: %s", esp_err_to_name(err));
        rmt_del_channel(g_rmt_channel);
        g_rmt_channel = NULL;
        return err;
    }

    err = rmt_enable(g_rmt_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bật RMT thất bại: %s", esp_err_to_name(err));
        rmt_del_encoder(g_led_encoder);
        rmt_del_channel(g_rmt_channel);
        g_led_encoder = NULL;
        g_rmt_channel = NULL;
        return err;
    }

    g_bIsReady = true;
    g_u8Brightness = UINT8_MAX;

    err = app_led_Clear();
    if (err == ESP_OK) {
        err = app_led_Show();
    }
    if (err != ESP_OK) {
        g_bIsReady = false;
        rmt_disable(g_rmt_channel);
        rmt_del_encoder(g_led_encoder);
        rmt_del_channel(g_rmt_channel);
        g_led_encoder = NULL;
        g_rmt_channel = NULL;
        return err;
    }

    ESP_LOGI(TAG, "Driver LED sẵn sàng trên GPIO%d, số LED: %d", gpio, DF_LED_COUNT);
    return ESP_OK;
}

esp_err_t app_led_Deinit(void)
{
    if (!g_bIsReady) {
        return ESP_OK;
    }

    esp_err_t result = app_led_Clear();
    if (result == ESP_OK) {
        result = app_led_Show();
    }

    esp_err_t err = rmt_disable(g_rmt_channel);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Tắt RMT thất bại: %s", esp_err_to_name(err));
    }

    if (g_led_encoder) {
        rmt_del_encoder(g_led_encoder);
        g_led_encoder = NULL;
    }
    if (g_rmt_channel) {
        rmt_del_channel(g_rmt_channel);
        g_rmt_channel = NULL;
    }

    g_bIsReady = false;
    return result != ESP_OK ? result : err;
}

esp_err_t app_led_SetPixel(uint8_t led, uint8_t r, uint8_t g, uint8_t b)
{
    if (led >= DF_LED_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t offset = (size_t)led * 3U;
    g_led_rgb[offset] = r;
    g_led_rgb[offset + 1U] = g;
    g_led_rgb[offset + 2U] = b;
    return ESP_OK;
}

esp_err_t app_led_SetPixelRgb(int led, app_led_color_t color)
{
    if (led < 0 || led >= (int)DF_LED_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    return app_led_SetPixel((uint8_t)led, color.r, color.g, color.b);
}

esp_err_t app_led_Fill(uint8_t r, uint8_t g, uint8_t b)
{
    for (uint8_t index = 0U; index < DF_LED_COUNT; index++) {
        esp_err_t err = app_led_SetPixel(index, r, g, b);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t app_led_SetBrightness(uint8_t brightness)
{
    g_u8Brightness = brightness;
    return ESP_OK;
}

static inline uint8_t scale8(uint8_t value, uint8_t brightness)
{
    return (uint8_t)(((uint16_t)value * (uint16_t)brightness) / UINT8_MAX);
}

esp_err_t app_led_Show(void)
{
    if (!g_bIsReady) {
        ESP_LOGW(TAG, "app_led_Show() được gọi trước app_led_Init()");
        return ESP_ERR_INVALID_STATE;
    }


    for (uint8_t index = 0U; index < DF_LED_COUNT; index++) {
        const size_t offset = (size_t)index * 3U;
        const uint8_t r = g_led_rgb[offset];
        const uint8_t g = g_led_rgb[offset + 1U];
        const uint8_t b = g_led_rgb[offset + 2U];

        g_led_data[offset] = scale8(g, g_u8Brightness);
        g_led_data[offset + 1U] = scale8(r, g_u8Brightness);
        g_led_data[offset + 2U] = scale8(b, g_u8Brightness);
    }

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };

    esp_err_t err = rmt_transmit(g_rmt_channel, g_led_encoder, g_led_data, sizeof(g_led_data), &tx_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Truyền dữ liệu RMT thất bại: %s", esp_err_to_name(err));
        return err;
    }

    const esp_err_t wait_result = rmt_tx_wait_all_done(g_rmt_channel, pdMS_TO_TICKS(DF_LED_TIMEOUT_MS));
    if (wait_result != ESP_OK) {
        ESP_LOGE(TAG, "Chờ RMT truyền xong thất bại: %s", esp_err_to_name(wait_result));
        return wait_result;
    }

    /* Reset/latch của WS2812 (RES > 280us theo datasheet) */
    vTaskDelay(pdMS_TO_TICKS(DF_LED_RESET_MS));
    return ESP_OK;
}

esp_err_t app_led_Clear(void)
{
    memset(g_led_rgb, 0, sizeof(g_led_rgb));
    return ESP_OK;
}

esp_err_t app_led_SetAllColor(app_led_color_t color)
{
    esp_err_t err = app_led_Fill(color.r, color.g, color.b);
    return err == ESP_OK ? app_led_Show() : err;
}
