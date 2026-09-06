#ifndef APP_LOGIC_LED_H
#define APP_LOGIC_LED_H

/**
 * @file app_logic_led.h
 * @brief API tầng Application điều phối màu sắc và độ sáng LED.
 */

#include "app_led.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif


/* Định nghĩa sẵn các macro hoặc biến hằng số màu chuẩn */
#define APP_LED_COLOR_RED      ((app_led_color_t){255, 0,   0})
#define APP_LED_COLOR_GREEN    ((app_led_color_t){0,   255, 0})
#define APP_LED_COLOR_BLUE     ((app_led_color_t){0,   0,   255})
#define APP_LED_COLOR_YELLOW   ((app_led_color_t){255, 255, 0})
#define APP_LED_COLOR_CYAN     ((app_led_color_t){0,   255, 255})
#define APP_LED_COLOR_MAGENTA  ((app_led_color_t){255, 0,   255})
#define APP_LED_COLOR_WHITE    ((app_led_color_t){255, 255, 255})
#define APP_LED_COLOR_OFF      ((app_led_color_t){0,   0,   0})

/** Dữ liệu lệnh LED gồm màu và độ sáng cần áp dụng. */
typedef struct {
    app_led_color_t sColor;
    uint8_t u8Brightness;
} app_logic_led_command_t;

/**
 * @brief Khởi tạo driver LED, queue và task xử lý lệnh.
 * @param None.
 * @return ESP_OK nếu thành công; mã lỗi nếu khởi tạo thất bại.
 */
esp_err_t app_logic_led_Init(void);

/**
 * @brief Gửi lệnh đổi màu cho toàn bộ dải LED.
 * @param sColor Màu RGB cần hiển thị.
 * @return ESP_OK nếu gửi thành công; mã lỗi nếu queue chưa sẵn sàng hoặc đầy.
 */
esp_err_t app_logic_led_SetColor(app_led_color_t sColor);

/**
 * @brief Gửi lệnh thay đổi độ sáng LED.
 * @param u8Brightness Độ sáng từ 0 đến 255.
 * @return ESP_OK nếu gửi thành công; mã lỗi nếu queue chưa sẵn sàng hoặc đầy.
 */
esp_err_t app_logic_led_SetBrightness(uint8_t u8Brightness);

/**
 * @brief Gửi lệnh xuất dữ liệu màu hiện tại ra LED.
 * @param None.
 * @return ESP_OK nếu gửi thành công; mã lỗi nếu queue chưa sẵn sàng hoặc đầy.
 */
esp_err_t app_logic_led_Show(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_LOGIC_LED_H */