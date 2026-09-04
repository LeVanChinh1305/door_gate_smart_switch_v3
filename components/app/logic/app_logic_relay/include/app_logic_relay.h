#ifndef APP_LOGIC_RELAY_H
#define APP_LOGIC_RELAY_H

/**
 * @file app_logic_relay.h
 * @brief API tầng Application điều phối các lệnh relay qua queue và task.
 */

#include "app_relay.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo driver relay, queue và task xử lý lệnh.
 * @param None.
 * @return ESP_OK nếu thành công; mã lỗi nếu khởi tạo thất bại.
 */
esp_err_t app_logic_relay_Init(void);

/**
 * @brief Gửi một lệnh relay vào queue xử lý bất đồng bộ.
 * @param eCommand Lệnh relay cần thực thi.
 * @return ESP_OK nếu đưa lệnh vào queue thành công; mã lỗi nếu thất bại.
 */
esp_err_t app_logic_relay_SendCommand(e_app_relay_cmd_t eCommand);

#ifdef __cplusplus
}
#endif

#endif /* APP_LOGIC_RELAY_H */