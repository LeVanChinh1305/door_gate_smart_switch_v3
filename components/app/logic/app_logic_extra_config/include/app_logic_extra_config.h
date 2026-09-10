/**
 * @file    app_extra_config.h
 * @brief   Module xử lý logic phân tích và đóng gói JSON cho cấu hình mở rộng (Extra Config).
 */

#ifndef APP_LOGIC_EXTRA_CONFIG_H
#define APP_LOGIC_EXTRA_CONFIG_H

#include "cJSON.h"
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Đóng gói cấu hình Extra Config trực tiếp vào mảng tĩnh bằng snprintf (Không dùng malloc).
 * @param pcOutBuffer Con trỏ trỏ tới mảng tĩnh sẽ chứa chuỗi JSON.
 * @param zMaxLen Kích thước tối đa của mảng (để chống tràn bộ đệm).
 */
void app_logic_extra_config_ProcessGet(char *pcOutBuffer, size_t zMaxLen);

/**
 * @brief Phân tích JSON từ server, cập nhật cấu hình RAM, áp dụng phần cứng và lưu xuống NVS.
 * @param pValue Đối tượng JSON chứa các trường cấu hình (có thể là jsRoot hoặc object "value").
 */
void app_logic_extra_config_ProcessSet(const cJSON *pValue);


/**
 * @brief Kiểm tra và tự động cập nhật bitmask DEVICE_MODE_LOCKED_RF dựa theo khung giờ ExtraConfig.
 * @return true nếu đang trong khung giờ khóa và lockRFEnb =1, false nếu bình thường.
 */
bool app_logic_extra_config_IsRFLocked(void);


/**
 * @brief Lập lịch Dynamic Timer tự động thức dậy đúng mốc lockRFBegin/lockRFEnd tiếp theo.
 */
void app_logic_extra_config_ScheduleNextRFLock(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_LOGIC_EXTRA_CONFIG_H */