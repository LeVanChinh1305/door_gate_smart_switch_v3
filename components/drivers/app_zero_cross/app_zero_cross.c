#include "app_zero_cross.h"
#include "esp_attr.h"
#include "esp_log.h"

static const char *TAG = "APP_ZERO_CROSS";

#define DF_ZCD_INTR_FLAG (0)

static volatile bool g_bWaitForZeroCross = false;
static bool g_bIsReady = false;

/* Lưu trữ Callback và tham số từ Application Layer */
static app_zero_cross_cb_t g_pfnIsrCallback = NULL;
static void *g_pCbArg = NULL;

/* ==================== ISR HANDLER ==================== */

/**
 * @brief  Trình xử lý ngắt phần cứng Zero-Cross (chạy trong RAM để tối ưu thời gian phản hồi)
 * @param  pArg Tham số truyền vào ngắt (không dùng)
 */
static void IRAM_ATTR app_zero_cross_PrvIsrHandler(void *pArg)
{
    (void)pArg;
    if (g_bWaitForZeroCross) {
        g_bWaitForZeroCross = false; /* Tự động reset cờ để tránh bắt ngắt liên tục */

        /* Kích hoạt callback nếu đã được đăng ký */
        if (g_pfnIsrCallback != NULL) {
            g_pfnIsrCallback(g_pCbArg);
        }
    }
}

/* ==================== PUBLIC FUNCTIONS ==================== */

esp_err_t app_zero_cross_Init(app_zero_cross_cb_t pfnCb, void *pArg)
{
    if (g_bIsReady) {
        return ESP_OK;
    }
    if (pfnCb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    g_pfnIsrCallback = pfnCb;
    g_pCbArg = pArg;

    gpio_config_t sIoConf = {
        .pin_bit_mask = (1ULL << DF_ZCD_GPIO_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_POSEDGE,      
    };
    
    esp_err_t ret = gpio_config(&sIoConf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Cấu hình GPIO zero-cross thất bại: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Đăng ký dịch vụ ngắt toàn cục (Hợp lệ nếu đã được cài đặt bởi module khác) */
    ret = gpio_install_isr_service(DF_ZCD_INTR_FLAG);
    if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE)) {
        ESP_LOGE(TAG, "Cài đặt dịch vụ ISR zero-cross thất bại: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_isr_handler_add(DF_ZCD_GPIO_PIN, app_zero_cross_PrvIsrHandler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Đăng ký trình xử lý ISR zero-cross thất bại: %s", esp_err_to_name(ret));
        return ret;
    }

    g_bIsReady = true;
    g_bWaitForZeroCross = false;

    ESP_LOGI(TAG, "Đã đăng ký ISR zero-cross trên GPIO%d", DF_ZCD_GPIO_PIN);
    return ESP_OK;
}

esp_err_t app_zero_cross_EnableWait(void)
{
    if (!g_bIsReady) {
        return ESP_ERR_INVALID_STATE;
    }
    g_bWaitForZeroCross = true;
    return ESP_OK;
}

esp_err_t app_zero_cross_DisableWait(void)
{
    if (!g_bIsReady) {
        return ESP_ERR_INVALID_STATE;
    }
    g_bWaitForZeroCross = false;
    return ESP_OK;
}