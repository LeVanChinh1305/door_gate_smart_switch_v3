#ifndef APP_LOGIC_BUZZER_H
#define APP_LOGIC_BUZZER_H

/**
 * @file app_logic_buzzer.h
 * @brief API tầng Application điều phối hoạt động của buzzer.
 */

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo driver buzzer, queue và task xử lý lệnh.
 * @param None.
 * @return ESP_OK nếu thành công; mã lỗi nếu khởi tạo thất bại.
 */
esp_err_t app_logic_buzzer_Init(void);

/**
 * @brief Gửi lệnh bật buzzer.
 * @param None.
 * @return ESP_OK nếu gửi thành công; mã lỗi nếu queue chưa sẵn sàng hoặc đầy.
 */
esp_err_t app_logic_buzzer_On(void);

/**
 * @brief Gửi lệnh tắt buzzer.
 * @param None.
 * @return ESP_OK nếu gửi thành công; mã lỗi nếu queue chưa sẵn sàng hoặc đầy.
 */
esp_err_t app_logic_buzzer_Off(void);

/**
 * @brief Gửi lệnh phát tiếng beep trong khoảng thời gian chỉ định.
 * @param u32DurationMs Thời gian phát, tính bằng mili-giây.
 * @return ESP_OK nếu gửi thành công; mã lỗi nếu tham số hoặc queue không hợp lệ.
 */
esp_err_t app_logic_buzzer_Beep(uint32_t u32DurationMs);

#ifdef __cplusplus
}
#endif

#endif /* APP_LOGIC_BUZZER_H */