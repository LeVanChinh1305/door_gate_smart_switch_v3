#include "app_logic_schedule.h"
#include "app_nvs.h"
#include "app_logic_relay.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <time.h>
#include <string.h>

#define DF_APP_LOGIC_SCHEDULE_POLLING 5000U
static const char *TAG = "APP_SCHEDULE";

static void app_schedule_task(void *pArg)
{
    uint8_t u8LastTriggeredMinute = 60; /* Giá trị khởi tạo ngoài dải phút (0-59) */
    app_schedule_item_t asSchedules[DF_MAX_SCHEDULES];
    uint8_t u8Count = 0;

    while(1) {
        time_t xNow;
        struct tm sTimeInfo;
        time(&xNow);
        localtime_r(&xNow, &sTimeInfo);

        /* Chỉ quét khi SNTP đã đồng bộ thành công (Năm >= 2024) */
        if (sTimeInfo.tm_year >= (2024 - 1900)) {
            
            /* Nếu đồng hồ vừa chuyển sang phút mới, tiến hành quét mảng lịch */
            if (sTimeInfo.tm_min != u8LastTriggeredMinute) {
                if (app_nvs_GetAllSchedules(asSchedules, &u8Count) == ESP_OK) {
                    for (uint8_t i = 0; i < u8Count; i++) {
                        
                        /* Kiểm tra lịch đang bật và khớp giờ/phút */
                        if (asSchedules[i].u8Activate == 1 &&
                            asSchedules[i].u8Hour == sTimeInfo.tm_hour &&
                            asSchedules[i].u8Minute == sTimeInfo.tm_min) {
                            
                            /* Chuyển đổi wday của C (0=CN, 1=T2) sang Index của Vconnex (0=T2, 6=CN) */
                            uint8_t u8DayIndex = (sTimeInfo.tm_wday == 0) ? 6 : (sTimeInfo.tm_wday - 1);
                            
                            /* Kiểm tra Bitmask ngày */
                            if (asSchedules[i].u8LoopDays == 0 || (asSchedules[i].u8LoopDays & (1 << u8DayIndex))) {
                                ESP_LOGI(TAG, "Đến giờ thực thi lịch ID: %u", (unsigned int)asSchedules[i].u32Id);
                                
                                /* Khớp lệnh và điều khiển Relay */
                                if (strcmp(asSchedules[i].acParam, "open_level") == 0) {
                                    app_logic_relay_SetLevel((uint8_t)asSchedules[i].i32Value);
                                } else if (strcmp(asSchedules[i].acParam, "up") == 0 || strcmp(asSchedules[i].acParam, "gate_3") == 0) {
                                    app_logic_relay_Open();
                                } else if (strcmp(asSchedules[i].acParam, "down") == 0 || strcmp(asSchedules[i].acParam, "gate_1") == 0) {
                                    app_logic_relay_Close();
                                } else if (strcmp(asSchedules[i].acParam, "stop") == 0 || strcmp(asSchedules[i].acParam, "gate_2") == 0) {
                                    app_logic_relay_Stop();
                                }
                            }
                        }
                    }
                }
                /* Cập nhật phút đã chạy để tránh gọi Relay 2 lần trong cùng 1 phút */
                u8LastTriggeredMinute = sTimeInfo.tm_min;
            }
        }
        
        /* Quét mỗi 5 giây một lần để tiết kiệm CPU */
        vTaskDelay(pdMS_TO_TICKS( DF_APP_LOGIC_SCHEDULE_POLLING));
    }
}

esp_err_t app_logic_schedule_Init(void)
{
    BaseType_t xRet = xTaskCreate(app_schedule_task, "schedule_task", 4096, NULL, 3, NULL);
    if (xRet != pdPASS) {
        ESP_LOGE(TAG, "Tạo task Hẹn giờ thất bại");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Khởi tạo Task Hẹn giờ thành công");
    return ESP_OK;
}