#include "app_buzzer.h"
#include "app_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "APP_BUZZER";

static bool g_bIsReady = false;

esp_err_t app_buzzer_Init(void)
{
    if (g_bIsReady) {
        return ESP_OK;
    }

    gpio_config_t io_config = {
        .pin_bit_mask = (1ULL << DF_BUZZER_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t ret = gpio_config(&io_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init failed on GPIO%d: %s", DF_BUZZER_PIN, esp_err_to_name(ret));
        return ret;
    }

    // Ép tắt còi ngay khi khởi tạo để đảm bảo trạng thái an toàn
    ret = gpio_set_level(DF_BUZZER_PIN, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to turn buzzer off: %s", esp_err_to_name(ret));
        return ret;
    }

    g_bIsReady = true;
    ESP_LOGI(TAG, "Buzzer init OK on GPIO%d", DF_BUZZER_PIN);
    return ESP_OK;
}

esp_err_t app_buzzer_On(void)
{
    if (!g_bIsReady) return ESP_ERR_INVALID_STATE;
    return gpio_set_level(DF_BUZZER_PIN, 1);
}

esp_err_t app_buzzer_Off(void)
{
    if (!g_bIsReady) return ESP_ERR_INVALID_STATE;
    return gpio_set_level(DF_BUZZER_PIN, 0);
}

esp_err_t app_buzzer_Beep(uint32_t delay_ms)
{
    if (!g_bIsReady) return ESP_ERR_INVALID_STATE;
    if (delay_ms == 0) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = app_buzzer_On();
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    return app_buzzer_Off();
}