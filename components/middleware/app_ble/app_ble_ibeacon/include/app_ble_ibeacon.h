#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo module quét iBeacon
 */
esp_err_t app_ble_ibeacon_Init(void);

/**
 * @brief Tắt module quét iBeacon
 */
esp_err_t app_ble_ibeacon_Deinit(void);

/**
 * @brief Kiểm tra module đang chạy không
 */
bool app_ble_ibeacon_IsRunning(void);

#ifdef __cplusplus
}
#endif