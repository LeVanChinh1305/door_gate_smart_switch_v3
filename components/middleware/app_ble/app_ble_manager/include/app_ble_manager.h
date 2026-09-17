#ifndef APP_BLE_MANAGER_H_
#define APP_BLE_MANAGER_H_

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Callback chuyển tiếp sự kiện BluFi về app_blufi
 */
typedef void (*app_ble_manager_blufi_event_cb_t)(int event, void *param);

/**
 * @brief Khởi tạo phần cứng BLE Controller, NimBLE Host stack và phát Quảng bá BLE
 */
esp_err_t app_ble_manager_Init(void);

/**
 * @brief Tắt NimBLE Host, BLE Controller và giải phóng RAM Heap
 */
esp_err_t app_ble_manager_Deinit(void);

/**
 * @brief Bắt đầu phát lại quảng bá BLE (Advertising)
 */
esp_err_t app_ble_manager_StartAdv(void);

/**
 * @brief Dừng phát quảng bá BLE (Advertising)
 */
esp_err_t app_ble_manager_StopAdv(void);

/**
 * @brief Đăng ký callback sự kiện BluFi từ tầng ứng dụng (app_blufi)
 */
void app_ble_manager_RegisterBlufiCallback(app_ble_manager_blufi_event_cb_t cb);

#endif