#ifndef APP_LED_H
#define APP_LED_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} app_led_color_t;

#define DF_LED_COUNT            3U
#define DF_LED_GPIO             GPIO_NUM_8
#define DF_LED_COLOR_OFF        ((app_led_color_t){0, 0, 0})
#define DF_LED_COLOR_RED        ((app_led_color_t){255, 0, 0})
#define DF_LED_COLOR_GREEN      ((app_led_color_t){0, 255, 0})
#define DF_LED_COLOR_BLUE       ((app_led_color_t){0, 0, 255})
#define DF_LED_COLOR_WHITE      ((app_led_color_t){255, 255, 255})
#define DF_LED_COLOR_YELLOW     ((app_led_color_t){255, 255, 0})
#define DF_LED_COLOR_CYAN       ((app_led_color_t){0, 255, 255})
#define DF_LED_COLOR_MAGENTA    ((app_led_color_t){255, 0, 255})


/**
 * @brief   Khởi tạo driver điều khiển LED WS2812 bằng ngoại vi RMT.
 * @param   gpio Chân GPIO kết nối với dây Data của dải LED.
 * @return  esp_err_t: ESP_OK nếu thành công, hoặc mã lỗi cấu hình RMT.
 * @note    Tự động dọn dẹp bộ đệm và tắt toàn bộ đèn ngay sau khi khởi tạo thành công để bắt đầu ở trạng thái sạch.
 */
esp_err_t app_led_Init(gpio_num_t gpio);

/**
 * @brief   Giải phóng tài nguyên phần cứng của driver LED.
 * @param   None
 * @return  esp_err_t: ESP_OK nếu thành công.
 * @note    Ra lệnh tắt đèn, vô hiệu hóa và xóa các bộ mã hóa (encoder) cũng như kênh truyền RMT.
 */
esp_err_t app_led_Deinit(void);


/**
 * @brief   Gán mã màu RGB thô cho một bóng LED cụ thể trong bộ đệm RAM.
 * @param   led Chỉ số của bóng LED (từ 0 đến DF_LED_COUNT - 1).
 * @param   r Giá trị màu Đỏ (0-255).
 * @param   g Giá trị màu Xanh lá (0-255).
 * @param   b Giá trị màu Xanh dương (0-255).
 * @return  esp_err_t: ESP_OK nếu thành công, ESP_ERR_INVALID_ARG nếu chỉ số led vượt quá giới hạn.
 * @note    Hàm này CHỈ thay đổi biến trong RAM, đèn vật lý chưa đổi màu cho đến khi gọi app_led_Show().
 */
esp_err_t app_led_SetPixel(uint8_t led, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief   Gán mã màu (dạng struct) cho một bóng LED cụ thể trong bộ đệm RAM.
 * @param   led Chỉ số của bóng LED.
 * @param   color Mã màu theo cấu trúc app_led_color_t (thường dùng các macro DF_LED_COLOR_*).
 * @return  esp_err_t: ESP_OK nếu thành công, ESP_ERR_INVALID_ARG nếu sai chỉ số.
 */
esp_err_t app_led_SetPixelRgb(int led, app_led_color_t color);


/**
 * @brief   Nạp cùng một mã màu RGB thô cho toàn bộ dải LED trong bộ đệm RAM.
 * @param   r Giá trị màu Đỏ (0-255).
 * @param   g Giá trị màu Xanh lá (0-255).
 * @param   b Giá trị màu Xanh dương (0-255).
 * @return  esp_err_t: ESP_OK nếu nạp thành công.
 */
esp_err_t app_led_Fill(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief   Cập nhật độ sáng tổng thể cho dải LED.
 * @param   brightness Mức độ sáng toàn cục (từ 0 đến 255).
 * @return  esp_err_t: ESP_OK.
 * @note    Độ sáng này sẽ được nhân tỷ lệ (scale) với giá trị màu khi hàm app_led_Show() thực thi.
 */
esp_err_t app_led_SetBrightness(uint8_t brightness);


/**
 * @brief   Bắn tín hiệu xung điện qua RMT để cập nhật màu sắc thực tế ra dải LED.
 * @param   None
 * @return  esp_err_t: ESP_OK nếu truyền xong, ESP_ERR_INVALID_STATE nếu chưa init, hoặc mã lỗi RMT (timeout).
 * @note    Hàm sẽ lấy dữ liệu từ RAM, ép lại tỷ lệ theo độ sáng g_u8Brightness, chuyển thành thứ tự GRB, 
 *          xuất xung RMT và áp dụng Timeout (DF_LED_TIMEOUT_MS) để chống kẹt luồng.
 */
esp_err_t app_led_Show(void);


/**
 * @brief   Xóa toàn bộ dữ liệu trong bộ đệm màu RAM về 0 (trạng thái tắt đèn).
 * @param   None
 * @return  esp_err_t: ESP_OK.
 * @note    Cần gọi tiếp app_led_Show() sau hàm này nếu muốn đèn tắt ngay lập tức.
 */
esp_err_t app_led_Clear(void);


/**
 * @brief   Hàm tiện ích gộp thao tác nạp màu và xuất tín hiệu ra toàn bộ dải LED.
 * @param   color Mã màu theo cấu trúc app_led_color_t.
 * @return  esp_err_t: ESP_OK nếu thành công.
 * @note    Là tổ hợp của việc gọi app_led_Fill() sau đó gọi ngay app_led_Show(). Giúp mã nguồn lớp Application ngắn gọn hơn.
 */
esp_err_t app_led_SetAllColor(app_led_color_t color);

#ifdef __cplusplus
}
#endif

#endif 
