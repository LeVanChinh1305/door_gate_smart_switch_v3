/**
 * @file    app_logic_ble_ibeacon.h
 * @brief   Tầng logic xử lý và phân phối lệnh BLE iBeacon (Gate/Door Switch)
 *
 * @note    Header chỉ chứa khai báo nguyên mẫu hàm công khai.
 */

#ifndef APP_LOGIC_BLE_IBEACON_H
#define APP_LOGIC_BLE_IBEACON_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Khởi tạo module Logic BLE iBeacon (Tạo Task và Queue)
 * @return esp_err_t ESP_OK nếu thành công, lỗi khác nếu thất bại
 */
esp_err_t app_logic_ble_ibeacon_Init(void);

/**
 * @brief  Hủy khởi tạo module Logic BLE iBeacon
 * @return esp_err_t ESP_OK nếu thành công
 */
esp_err_t app_logic_ble_ibeacon_Deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_LOGIC_BLE_IBEACON_H */