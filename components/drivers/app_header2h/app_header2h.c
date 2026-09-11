#include "app_header2h.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "APP_HEADER2H";

static bool g_bIsReady = false;
static app_header2h_cb_t g_pfnCallback = NULL;
static void *g_pCallbackArg = NULL;

/* ==================== ISR HANDLER ==================== */

/**
 * @brief Trình xử lý ngắt phần cứng khi mức logic GPIO thay đổi (chạy trong RAM)
 * @param pArg Tham số ngắt (không sử dụng)
 */
static void IRAM_ATTR app_header2h_PrvIsrHandler(void *pArg)
{
    (void)pArg;
    int i32Level = gpio_get_level(DF_HEADER2H_GPIO_PIN);
    if (g_pfnCallback != NULL) {
        g_pfnCallback(i32Level, g_pCallbackArg);
    }
}

/* ==================== PUBLIC FUNCTIONS ==================== */

esp_err_t app_header2h_Init(app_header2h_cb_t pfnCb, void *pArg)
{
    if (g_bIsReady) {
        return ESP_OK;
    }
    if (pfnCb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    g_pfnCallback = pfnCb;
    g_pCallbackArg = pArg;

    gpio_config_t sIoConfig = {
        .pin_bit_mask = (1ULL << DF_HEADER2H_GPIO_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE
    };
    esp_err_t ret = gpio_config(&sIoConfig);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Cấu hình GPIO header2h thất bại: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Đăng ký dịch vụ ngắt toàn cục nếu chưa đăng ký */
    ret = gpio_install_isr_service(DF_HEADER2H_ISR_FLAGS);
    if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE)) {
        ESP_LOGE(TAG, "Cài đặt dịch vụ ISR header2h thất bại: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_isr_handler_add(DF_HEADER2H_GPIO_PIN, app_header2h_PrvIsrHandler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Đăng ký trình xử lý ISR header2h thất bại: %s", esp_err_to_name(ret));
        return ret;
    }

    g_bIsReady = true;
    ESP_LOGI(TAG, "Khởi tạo đầu vào header2h thành công trên GPIO%d", DF_HEADER2H_GPIO_PIN);
    return ESP_OK;
}

esp_err_t app_header2h_Read(int *pLevel)
{
    if (!g_bIsReady) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pLevel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *pLevel = gpio_get_level(DF_HEADER2H_GPIO_PIN);
    return ESP_OK;
}

bool app_header2h_IsDoorClosed(void)
{
    if (!g_bIsReady) {
        return false;
    }
    return (gpio_get_level(DF_HEADER2H_GPIO_PIN) == DF_HEADER2H_STATE_CLOSED);
}