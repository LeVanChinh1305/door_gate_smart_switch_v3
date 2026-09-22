#ifndef APP_LOGIC_TOUCH_H
#define APP_LOGIC_TOUCH_H

/**
 * @file app_logic_touch.h
 * @brief API tầng Application điều phối các lệnh cảm ứng qua queue và task.
 */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Các lệnh mà task cảm ứng có thể xử lý. */
typedef enum {
    E_APP_LOGIC_TOUCH_CMD_READ_STATUS = 0,
    E_APP_LOGIC_TOUCH_CMD_CLEAR_LATCHED,
    E_APP_LOGIC_TOUCH_CMD_SOFT_RESET
} e_app_logic_touch_cmd_t;

/**
 * @brief Khởi tạo driver cảm ứng, queue và task xử lý lệnh.
 * @param None.
 * @return ESP_OK nếu thành công; mã lỗi nếu khởi tạo thất bại.
 */
esp_err_t app_logic_touch_Init(void);

/**
 * @brief Gửi một lệnh cảm ứng vào queue xử lý bất đồng bộ.
 * @param eCommand Lệnh cảm ứng cần thực thi.
 * @return ESP_OK nếu đưa lệnh vào queue thành công; mã lỗi nếu thất bại.
 */
esp_err_t app_logic_touch_SendCommand(e_app_logic_touch_cmd_t eCommand);


/**
 * @brief Khởi tạo task cảm ứng RÚT GỌN, chỉ dùng trong chế độ cấu hình
 *        (BluFi/UDP). Bất kỳ nút nào được giữ đủ lâu sẽ hủy cấu hình
 *        và khởi động lại thiết bị.
 * @return ESP_OK nếu khởi tạo thành công.
 */
esp_err_t app_logic_touch_InitCancelConfigMode(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_LOGIC_TOUCH_H */