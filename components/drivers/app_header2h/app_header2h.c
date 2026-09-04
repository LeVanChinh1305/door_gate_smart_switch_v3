#include "app_header2h.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "APP_HEADER2H";
static bool g_bIsReady = false;
static app_header2h_cb_t g_Callback = NULL;
static void *g_pCallbackArg = NULL;

static void IRAM_ATTR app_header2h_IsrHandler(void *arg)
{
    int level = gpio_get_level(DF_HEADER2H_GPIO_PIN);
    if (g_Callback != NULL) {
        g_Callback(level, g_pCallbackArg);
    }
}

esp_err_t app_header2h_Init(app_header2h_cb_t cb, void *arg){
    if (g_bIsReady) {
        return ESP_OK;
    }
    if (cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    g_Callback = cb;
    g_pCallbackArg = arg;

    gpio_config_t io_config = {
        .pin_bit_mask = (1ULL << DF_HEADER2H_GPIO_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    esp_err_t ret = gpio_config(&io_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Cấu hình GPIO thất bại: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_err_t isr_service_ret = gpio_install_isr_service(DF_HEADER2H_ISR_FLAGS);
    if (isr_service_ret != ESP_OK && isr_service_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Cài đặt dịch vụ ISR thất bại: %s", esp_err_to_name(isr_service_ret));
        return isr_service_ret;
    }

    ret = gpio_isr_handler_add(DF_HEADER2H_GPIO_PIN, app_header2h_IsrHandler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Đăng ký trình xử lý ISR thất bại: %s", esp_err_to_name(ret));
        return ret;
    }

    g_bIsReady = true;
    ESP_LOGI(TAG, "Khởi tạo đầu vào header thành công trên GPIO%d", DF_HEADER2H_GPIO_PIN);
    return ESP_OK;
}

esp_err_t app_header2h_Read(int *level){
    if (!g_bIsReady) {
        return ESP_ERR_INVALID_STATE;
    }
    if (level == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *level = gpio_get_level(DF_HEADER2H_GPIO_PIN);
    return ESP_OK;
}