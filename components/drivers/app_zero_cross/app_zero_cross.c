#include "app_zero_cross.h"
#include "esp_attr.h"
#include "esp_log.h"

static const char *TAG = "APP_ZERO_CROSS";

#define DF_ZERO_CROSS_INTR_FLAG (0)

static volatile bool g_bWaitForZeroCross = false;
static bool g_bIsReady = false;

// Lưu trữ Callback và tham số từ Application Layer
static app_zero_cross_cb_t g_IsrCallback = NULL;
static void *g_pCallbackArg = NULL;

/* ==================== ISR HANDLER ==================== */

// Từ khóa IRAM_ATTR bắt buộc phải có để nạp hàm này vào RAM, giúp thực thi ngắt siêu tốc
static void IRAM_ATTR app_zero_cross_IsrHandler(void *arg)
{
    if (g_bWaitForZeroCross) {
        g_bWaitForZeroCross = false; // Tự động reset cờ để tránh bắt ngắt liên tục

        // Kích hoạt callback nếu đã được đăng ký
        if (g_IsrCallback != NULL) {
            g_IsrCallback(g_pCallbackArg);
        }
    }
}

/* ==================== PUBLIC FUNCTIONS ==================== */

esp_err_t app_zero_cross_Init(app_zero_cross_cb_t cb, void *arg)
{
    if (g_bIsReady) return ESP_OK;
    if (cb == NULL) return ESP_ERR_INVALID_ARG;

    g_IsrCallback = cb;
    g_pCallbackArg = arg;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DF_ZERO_CROSS_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_POSEDGE,      
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Đăng ký dịch vụ ngắt toàn cục (Có thể trả về INVALID_STATE nếu đã đăng ký bởi module khác, điều này hợp lệ)
    ret = gpio_install_isr_service(DF_ZERO_CROSS_INTR_FLAG);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_isr_handler_add(DF_ZERO_CROSS_PIN, app_zero_cross_IsrHandler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_isr_handler_add failed: %s", esp_err_to_name(ret));
        return ret;
    }

    g_bIsReady = true;
    g_bWaitForZeroCross = false;

    ESP_LOGI(TAG, "Zero-Cross ISR registered on GPIO%d", DF_ZERO_CROSS_PIN);
    return ESP_OK;
}

esp_err_t app_zero_cross_EnableWait(void)
{
    if (!g_bIsReady) return ESP_ERR_INVALID_STATE;
    g_bWaitForZeroCross = true;
    return ESP_OK;
}

esp_err_t app_zero_cross_DisableWait(void)
{
    if (!g_bIsReady) return ESP_ERR_INVALID_STATE;
    g_bWaitForZeroCross = false;
    return ESP_OK;
}