#include "app_relay.h"
#include "app_common.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "APP_RELAY";

// Triệt tiêu Magic Number cho khoảng thời gian khóa chéo bảo vệ động cơ
#define DF_RELAY_INTERLOCK_DELAY_MS  (100U)

static bool g_bIsReady = false;

/* ==================== PRIVATE FUNCTIONS ==================== */
/**
 * @brief   Thiết lập mức logic cho một chân GPIO rơ-le.
 * @param   gpio Chân GPIO cần thiết lập.
 * @param   level Mức logic (0 hoặc 1).
 * @return  esp_err_t: ESP_OK nếu thành công, hoặc mã lỗi từ gpio_set_level().
 */
static esp_err_t app_relay_SetLevel(gpio_num_t gpio, uint32_t level)
{
    esp_err_t ret = gpio_set_level(gpio, level);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO%d to %lu: %s", gpio, (unsigned long)level, esp_err_to_name(ret));
    }
    return ret;
}


/**
 * @brief   Tắt toàn bộ các rơ-le.
 * @param   None
 * @return  esp_err_t: ESP_OK nếu thành công, hoặc ESP_FAIL nếu có lỗi xảy ra khi tắt một trong các rơ-le.
 */
static esp_err_t app_relay_AllOffUnlocked(void)
{
    esp_err_t ret = ESP_OK;
    ret |= app_relay_SetLevel(DF_RELAY_PIN_CLOSE, 0);
    ret |= app_relay_SetLevel(DF_RELAY_PIN_OPEN, 0);
    ret |= app_relay_SetLevel(DF_RELAY_PIN_STOP, 0);
    return (ret == ESP_OK) ? ESP_OK : ESP_FAIL;
}

/* ==================== PUBLIC FUNCTIONS ==================== */

esp_err_t app_relay_Init(void)
{
    if (g_bIsReady) {
        return ESP_OK;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DF_RELAY_PIN_CLOSE) | 
                        (1ULL << DF_RELAY_PIN_OPEN) | 
                        (1ULL << DF_RELAY_PIN_STOP),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    g_bIsReady = true;
    
    // Đảm bảo rơ-le tắt an toàn ngay khi khởi động
    app_relay_AllOffUnlocked();

    ESP_LOGI(TAG, "Relay GPIO init OK (CLOSE=GPIO%d, OPEN=GPIO%d, STOP=GPIO%d)", 
             DF_RELAY_PIN_CLOSE, DF_RELAY_PIN_OPEN, DF_RELAY_PIN_STOP);
    return ESP_OK;
}

esp_err_t app_relay_ExecuteCmd(e_app_relay_cmd_t cmd)
{
    if (!g_bIsReady) {
        ESP_LOGW(TAG, "Relay driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // BƯỚC 1: Tắt toàn bộ để tránh chập pha
    app_relay_AllOffUnlocked();

    // BƯỚC 2: Nếu là lệnh ALL_OFF thì thoát luôn
    if (cmd == E_RELAY_CMD_ALL_OFF) {
        return ESP_OK;
    }

    // BƯỚC 3: Tạo trễ an toàn cơ học (Khóa liên động)
    vTaskDelay(pdMS_TO_TICKS(DF_RELAY_INTERLOCK_DELAY_MS));

    // BƯỚC 4: Kích hoạt rơ-le tương ứng
    switch (cmd) {
        case E_RELAY_CMD_CLOSE:
            return app_relay_SetLevel(DF_RELAY_PIN_CLOSE, 1);
        
        case E_RELAY_CMD_OPEN:
            return app_relay_SetLevel(DF_RELAY_PIN_OPEN, 1);
        
        case E_RELAY_CMD_STOP:
            return app_relay_SetLevel(DF_RELAY_PIN_STOP, 1);
        
        default:
            ESP_LOGW(TAG, "Unknown relay command: %d", cmd);
            return ESP_ERR_INVALID_ARG;
    }
}