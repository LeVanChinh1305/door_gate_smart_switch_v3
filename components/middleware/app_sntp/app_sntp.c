/**
 * @file    app_sntp.c
 * @brief   Khởi tạo và quản lý đồng bộ thời gian SNTP cho hệ thống.
 */

#include "app_sntp.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_logic_extra_config.h"

static const char *TAG = "APP_SNTP";

/**
 * @brief Callback tự động kích hoạt khi nhận được gói tin NTP thành công
 */
static void time_sync_notification_cb(struct timeval *tv)
{
    time_t tNow = 0;
    (void)time(&tNow);
    ESP_LOGI(TAG, "=> Đồng bộ SNTP thành công! Timestamp hiện tại: %lld", (long long)tNow);
    /* 1. Cập nhật ngay Bitmask theo giờ thực vừa lấy từ Internet */
    (void)app_logic_extra_config_IsRFLocked();

    /* 2. Tính toán và kích hoạt Dynamic Timer đếm ngược mốc bật/tắt tiếp theo */
    app_logic_extra_config_ScheduleNextRFLock();
}

/**
 * @brief Khởi tạo dịch vụ SNTP
 */
void app_sntp_Init(void)
{
    ESP_LOGI(TAG, "Đang khởi tạo cấu hình SNTP...");

    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    
    /* Dùng tên miền chuẩn của Google và Quốc tế */
    esp_sntp_setservername(0, "time.google.com");
    esp_sntp_setservername(1, "pool.ntp.org");
    esp_sntp_setservername(2, "time.windows.com");

    sntp_set_time_sync_notification_cb(time_sync_notification_cb);

    esp_sntp_init();

    setenv("TZ", "UTC-7", 1);
    tzset();

    ESP_LOGI(TAG, "Đã khởi tạo SNTP thành công.");
}

/**
 * @brief Chờ quá trình đồng bộ thời gian hoàn tất
 */
bool app_sntp_WaitForSync(uint32_t u32TimeoutMs)
{
    uint32_t u32Elapsed = 0U;
    ESP_LOGI(TAG, "Đang chờ đồng bộ thời gian từ Internet...");
    
    /* SỬA ĐỔI QUAN TRỌNG: Tiếp tục lặp nếu trạng thái CHƯA BẰNG COMPLETED */
    while ((sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) && (u32Elapsed < u32TimeoutMs)) {
        vTaskDelay(pdMS_TO_TICKS(500));
        u32Elapsed += 500U;
    }

    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        time_t tNow = 0;
        (void)time(&tNow);
        ESP_LOGI(TAG, "Đồng bộ thời gian thành công sau %u ms! Timestamp: %lld", (unsigned int)u32Elapsed, (long long)tNow);
        return true;
    }
    
    ESP_LOGE(TAG, "Đồng bộ thời gian thất bại (Timeout %u ms)", (unsigned int)u32TimeoutMs);
    return false;
}

/**
 * @brief Gán giờ hệ thống thủ công (Fallback khi mất NTP)
 */
void app_sntp_SetSystemTime(int64_t i64TimestampSec)
{
    if (i64TimestampSec <= 1700000000LL) {
        return;
    }
    struct timeval tv;
    tv.tv_sec = (time_t)i64TimestampSec;
    tv.tv_usec = 0;
    (void)settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "Đã gán giờ hệ thống thủ công: %lld", (long long)i64TimestampSec);
}