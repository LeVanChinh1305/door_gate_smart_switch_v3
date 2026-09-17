#ifndef APP_BLE_CONTROL_H_
#define APP_BLE_CONTROL_H_

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo module điều khiển cổng qua BLE (Gate Control)
 *        Đăng ký Gate Control Profile vào app_ble_manager.
 * @return esp_err_t ESP_OK nếu thành công
 */
esp_err_t app_ble_control_Init(void);

/**
 * @brief Hủy module điều khiển cổng qua BLE, dừng quảng bá và giải phóng tài nguyên
 * @return esp_err_t ESP_OK nếu thành công
 */
esp_err_t app_ble_control_Deinit(void);

/**
 * @brief Kiểm tra xem module BLE Control có đang hoạt động hay không
 * @return true nếu đang hoạt động
 */
bool app_ble_control_IsRunning(void);

/**
 * @brief Kiểm tra xem hiện có điện thoại / ứng dụng nào đang kết nối qua BLE hay không
 * @return true nếu đang có kết nối BLE
 */
bool app_ble_control_IsConnected(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_BLE_CONTROL_H_ */
