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
 * @note trong chế độ loại bỏ điều khiển ngoài ý muốn
 */
void app_logic_extra_config_ScheduleNextRFLock(void);


/**
 * @brief Tạm thời tháo cờ khóa cảm ứng (anti_animal) và đặt lịch đếm ngược N giây để khóa lại.
 * @note trong chế độ khóa an toàn 
 */
void app_logic_extra_config_StartAntiAnimalWindow(void);


/**
 * @brief Kiểm tra xem hiện tại có đang trong khung giờ Cảnh báo ban đêm hay không.
 * @return true nếu tính năng warningEnb BẬT và thời gian thực nằm trong khoảng warningBegin -> warningEnd.
 */
bool app_logic_extra_config_IsWarningNightActive(void);


/**
 * @brief  Kích hoạt bíp còi có kiểm tra cờ cấu hình buzzerEnb.
 * @param  u32DurationMs Thời gian phát tiếng bíp (ms).
 * @note   Nếu buzzerEnb == 0, hàm sẽ bỏ qua không phát bíp.
 */
void app_logic_extra_config_TriggerBuzzer(uint32_t u32DurationMs);

/**
 * @brief  Kích hoạt bíp còi lặp lại nhiều lần có kiểm tra cờ cấu hình buzzerEnb.
 * @param  u32DurationMs Thời gian phát mỗi tiếng bíp (ms).
 * @param  u32DelayMs Thời gian nghỉ giữa các tiếng bíp (ms).
 * @param  u8RepeatCount Số lần lặp lại.
 */
void app_logic_extra_config_TriggerBuzzerRepeat(uint32_t u32DurationMs, uint32_t u32DelayMs, uint8_t u8RepeatCount);


#ifdef __cplusplus
}
#endif

#endif /* APP_LOGIC_EXTRA_CONFIG_H */