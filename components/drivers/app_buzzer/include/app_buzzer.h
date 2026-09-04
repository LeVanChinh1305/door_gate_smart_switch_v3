#ifndef APP_BUZZER_H
#define APP_BUZZER_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Khai báo chân GPIO kết nối với Buzzer
#define DF_BUZZER_PIN GPIO_NUM_10

/**
 * @brief   Khởi tạo chân GPIO điều khiển còi chip.
 * @return  esp_err_t: ESP_OK nếu thành công.
 */
esp_err_t app_buzzer_Init(void);

/**
 * @brief   Bật còi kêu liên tục.
 * @return  esp_err_t: ESP_OK nếu thành công, ESP_ERR_INVALID_STATE nếu chưa init.
 */
esp_err_t app_buzzer_On(void);

/**
 * @brief   Tắt còi.
 * @return  esp_err_t: ESP_OK nếu thành công.
 */
esp_err_t app_buzzer_Off(void);

/**
 * @brief   Bật còi và tự động tắt sau một khoảng thời gian (dành cho tiếng bíp phản hồi nút bấm).
 * @param   delay_ms Thời gian kêu (mili-giây).
 * @return  esp_err_t: ESP_OK nếu thành công.
 * @note    Không nên dùng hàm này với delay_ms quá lớn để tránh block Task hiện tại.
 */
esp_err_t app_buzzer_Beep(uint32_t delay_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_BUZZER_H */