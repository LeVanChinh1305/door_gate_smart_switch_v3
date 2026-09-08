#include "app_sntp.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>

static const char *TAG = "APP_SNTP";

void app_sntp_Init(void)
{
    ESP_LOGI(TAG, "Đang khởi tạo cấu hình SNTP...");

    /* Thiết lập chế độ tự động lấy giờ định kỳ (Polling) */
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    
    /* Cấu hình các máy chủ thời gian quốc tế và khu vực */
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.windows.com");
    esp_sntp_setservername(2, "time.google.com");

    /* Bắt đầu tiến trình đồng bộ */
    esp_sntp_init();

    /* Thiết lập múi giờ Việt Nam (UTC+7)
     * Cú pháp: "UTC-7" (Ngược dấu do quy ước chuẩn POSIX) 
     */
    setenv("TZ", "UTC-7", 1);
    tzset();

    ESP_LOGI(TAG, "Đã khởi tạo SNTP thành công. Quá trình đồng bộ ngầm đang chạy.");
}