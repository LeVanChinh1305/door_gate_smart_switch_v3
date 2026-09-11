#include "app_logic_schedule.h"
#include "app_nvs.h"
#include "app_logic_relay.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <time.h>
#include <string.h>
#include "app_logic_telemetry.h"
#include "app_mqtt.h"
#include "esp_task_wdt.h"

#define DF_APP_LOGIC_SCHEDULE_POLLING 2000U
static const char *TAG = "APP_SCHEDULE";

static void app_schedule_task(void *pArg)
{
    uint8_t u8LastTriggeredMinute = 60; /* Giá trị khởi tạo ngoài dải phút (0-59) */
    app_schedule_item_t asSchedules[DF_MAX_SCHEDULES];
    uint8_t u8Count = 0;

    /* Đăng ký task lịch trình vào TWDT */
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    while(1) {
        esp_task_wdt_reset();

        time_t xNow;
        struct tm sTimeInfo;
        time(&xNow);
        localtime_r(&xNow, &sTimeInfo);

        /* Chỉ quét khi SNTP đã đồng bộ thành công (Năm >= 2024) */
        if (sTimeInfo.tm_year >= (2024 - 1900)) {
            
            /* Nếu đồng hồ vừa chuyển sang phút mới, tiến hành quét mảng lịch */
            if (sTimeInfo.tm_min != u8LastTriggeredMinute) {
                if (app_nvs_GetAllSchedules(asSchedules, &u8Count) == ESP_OK) {
                    const app_nvs_device_config_t *psConfig = app_mqtt_GetDeviceConfig();
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

                                app_logic_control_history_item_t sLog;
                                (void)memset(&sLog, 0, sizeof(app_logic_control_history_item_t));
                                
                                /* Khớp lệnh và điều khiển Relay */
                                if (strcmp(asSchedules[i].acParam, "open_level") == 0 || strcmp(asSchedules[i].acParam, "gate_level") == 0) {
                                    uint8_t u8TargetVal = (uint8_t)asSchedules[i].i32Value;
                                    app_logic_relay_SetLevel(u8TargetVal);
                                    if(psConfig != NULL){
                                        app_logic_telemetry_BuildControlItem(&sLog, 
                                                                             "open_level", 
                                                                             E_TELEMETRY_MODE_PERCENT, 
                                                                             E_TELEMETRY_SRC_DEVICE_SCHEDULE, 
                                                                             psConfig->dev_ext_addr, 
                                                                             "");
                                        sLog.i32Value = (int32_t)u8TargetVal;
                                        (void)app_logic_telemetry_ReportControlHistory(&sLog, 1);
                                    }
                                } else if (strcmp(asSchedules[i].acParam, "up") == 0 || strcmp(asSchedules[i].acParam, "gate_3") == 0) {
                                    app_logic_relay_Open();
                                    if (psConfig != NULL) {
                                        app_logic_telemetry_BuildControlItem(&sLog, 
                                                                             "gate_3", 
                                                                             E_TELEMETRY_MODE_OPEN, 
                                                                             E_TELEMETRY_SRC_DEVICE_SCHEDULE, 
                                                                             psConfig->dev_ext_addr, 
                                                                             "");
                                        (void)app_logic_telemetry_ReportControlHistory(&sLog, 1);
                                    }
                                } else if (strcmp(asSchedules[i].acParam, "down") == 0 || strcmp(asSchedules[i].acParam, "gate_1") == 0) {
                                    app_logic_relay_Close();
                                    if (psConfig != NULL) {
                                        app_logic_telemetry_BuildControlItem(&sLog, 
                                                                             "gate_1", 
                                                                             E_TELEMETRY_MODE_CLOSE, 
                                                                             E_TELEMETRY_SRC_DEVICE_SCHEDULE, 
                                                                             psConfig->dev_ext_addr, 
                                                                             "");
                                        (void)app_logic_telemetry_ReportControlHistory(&sLog, 1);
                                    }
                                } else if (strcmp(asSchedules[i].acParam, "stop") == 0 || strcmp(asSchedules[i].acParam, "gate_2") == 0) {
                                    app_logic_relay_Stop();
                                    if (psConfig != NULL) {
                                        uint8_t u8StopLevel = app_logic_relay_GetCurrentLevel();
                                        app_logic_telemetry_BuildControlItem(&sLog, 
                                                                             "gate_2", 
                                                                             E_TELEMETRY_MODE_STOP, 
                                                                             E_TELEMETRY_SRC_DEVICE_SCHEDULE, 
                                                                             psConfig->dev_ext_addr, 
                                                                             "");
                                        sLog.i32Value = (int32_t)u8StopLevel;
                                        (void)app_logic_telemetry_ReportControlHistory(&sLog, 1);
                                    }
                                }

                                /* 2. XỬ LÝ LỊCH CHẠY 1 LẦN (loopDays = 0) */
                                if (asSchedules[i].u8LoopDays == 0) {
                                    asSchedules[i].u8Activate = 0; /* Tắt kích hoạt */
                                    
                                    esp_err_t err = app_nvs_SaveSchedule(&asSchedules[i]);
                                    if (err == ESP_OK) {
                                        ESP_LOGI(TAG, "Đã tự động vô hiệu hóa lịch 1 lần ID: %u trong NVS", (unsigned int)asSchedules[i].u32Id);
                                    } else {
                                        ESP_LOGE(TAG, "Lỗi khi cập nhật trạng thái lịch 1 lần vào NVS: %s", esp_err_to_name(err));
                                    }
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