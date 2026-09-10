#ifndef APP_LOGIC_TELEMETRY_H
#define APP_LOGIC_TELEMETRY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define DF_LOGIC_TELEMETRY_MAX_STR_LEN    64U

/**
 * @brief Định nghĩa các Mode điều khiển (mode)
 */
typedef enum {
    E_TELEMETRY_MODE_HALF_GATE   = 1,
    E_TELEMETRY_MODE_FULL_GATE   = 2,
    E_TELEMETRY_MODE_OPEN        = 3,
    E_TELEMETRY_MODE_STOP        = 4,
    E_TELEMETRY_MODE_CLOSE       = 5,
    E_TELEMETRY_MODE_ON          = 6,
    E_TELEMETRY_MODE_OFF         = 7,
    E_TELEMETRY_MODE_PERCENT     = 8,
    E_TELEMETRY_MODE_DEFAULT_VAL = 9
} e_telemetry_control_mode_t;

/**
 * @brief Định nghĩa Nguồn điều khiển (src)
 */
typedef enum {
    E_TELEMETRY_SRC_APP               = 1,  /* Điều khiển từ App */
    E_TELEMETRY_SRC_PHYSICAL_DEVICE   = 2,  /* Điều khiển từ nút bấm phần cứng */
    E_TELEMETRY_SRC_CLOUD_AUTOMATION  = 3,  /* Cloud Automation */
    E_TELEMETRY_SRC_CLOUD_SCENE       = 4,  /* Cloud Scene */
    E_TELEMETRY_SRC_CLOUD_DEVICE_GROUP= 5,  /* Cloud Device Group */
    E_TELEMETRY_SRC_GW_AUTOMATION     = 6,  /* Gateway Automation */
    E_TELEMETRY_SRC_GW_SCENE          = 7,  /* Gateway Scene */
    E_TELEMETRY_SRC_GW_DEVICE_GROUP   = 8,  /* Gateway Device Group */
    E_TELEMETRY_SRC_BLE_BACKUP        = 9,  /* Điều khiển dự phòng qua BLE */
    E_TELEMETRY_SRC_FORCE_AUTOMATION  = 10, /* Chạy cưỡng bức Automation */
    E_TELEMETRY_SRC_DEVICE_SCHEDULE   = 11, /* Lập lịch nội bộ thiết bị */
    E_TELEMETRY_SRC_OTHER_AUTOMATION  = 12, /* Ngữ cảnh từ Automation khác */
    E_TELEMETRY_SRC_ANTI_COLLISION    = 13, /* Cảm biến chống xô kích hoạt */
    E_TELEMETRY_SRC_MAIKA             = 14, /* Loa Maika */
    E_TELEMETRY_SRC_GOOGLE_HOME       = 15, /* Google Home */
    E_TELEMETRY_SRC_ALEXA             = 16  /* Alexa */
} e_telemetry_control_src_t;

/**
 * @brief Cấu trúc lưu trữ 1 bản ghi Lịch sử Điều khiển (ReportControlHistory)
 */
typedef struct {
    char cControl[DF_LOGIC_TELEMETRY_MAX_STR_LEN]; /* "gate_1", "gate_2", "gate_3" */
    int32_t i32Mode;                                /* Dùng enum e_telemetry_control_mode_t */
    int32_t i32Value;
    int64_t i64Time;                                /* Timestamp (ms) */
    int32_t i32Src;                                 /* Dùng enum e_telemetry_control_src_t */
    char cSrcId[DF_LOGIC_TELEMETRY_MAX_STR_LEN];   /* ID nguồn (SessionID, RuleID, DeviceID...) */
    char cDesId[DF_LOGIC_TELEMETRY_MAX_STR_LEN];   /* ID đích (Rỗng nếu từ thiết bị) */
} app_logic_control_history_item_t;

typedef struct {
    uint8_t u8SensorState;
    int64_t i64Time;
} app_logic_sensor_history_item_t;

/**
 * @brief Hàm tiện ích giúp đóng gói nhanh 1 item ReportControlHistory chuẩn Spec
 */
void app_logic_telemetry_BuildControlItem(app_logic_control_history_item_t *pOutItem,
                                          const char *pcControl,
                                          e_telemetry_control_mode_t eMode,
                                          e_telemetry_control_src_t eSrc,
                                          const char *pcRawSrcId,
                                          const char *pcRawDesId);

esp_err_t app_logic_telemetry_ReportControlHistory(const app_logic_control_history_item_t *pHistoryList, uint16_t u16ItemCount);
esp_err_t app_logic_telemetry_ReportSensorHistory(const app_logic_sensor_history_item_t *pHistoryList, uint16_t u16ItemCount);

/**
 * @brief Bắn bản tin cảnh báo trạng thái cửa ReportWarningSgm lên Topic Alert (mqtt_alert)
 * @param u8Sensor Cảnh báo ban đêm (1: kích hoạt, 0: không)
 * @param u8CloseError Cảnh báo đóng cửa lỗi (1: kích hoạt, 0: không)
 * @param u8AntiStuck Cảnh báo cảm biến chống xô (1: kích hoạt, 0: không)
 * @param u8Src Nguồn kích hoạt (14: cảm biến, 1: phím bấm/app, ...)
 * @param pcSrcId ID định danh nguồn (truyền NULL để tự động dùng MAC thiết bị)
 * @return esp_err_t ESP_OK nếu gửi thành công
 */
esp_err_t app_logic_telemetry_ReportWarningSgm(uint8_t u8Sensor,uint8_t u8CloseError, uint8_t u8AntiStuck, uint8_t u8Src, const char *pcSrcId);

#endif