#ifndef APP_BLE_MANAGER_H_
#define APP_BLE_MANAGER_H_

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ble_gatt_register_ctxt;

/**
 * @brief Cấu trúc đại diện cho một BLE Profile độc lập (BluFi, Gate Control, OTA...)
 */
typedef struct {
    const char *name;                                                         // Tên profile để log/debug 
    esp_err_t (*profile_init)(void);                                          // Hàm khởi tạo GATT service & callbacks 
    esp_err_t (*profile_deinit)(void);                                        // Hàm hủy GATT service & tài nguyên của profile 
    void (*on_sync)(void);                                                    // Hook gọi khi NimBLE host sync 
    void (*on_reset)(int reason);                                             // Hook gọi khi NimBLE host reset (optional, có thể NULL) 
    void (*gatts_register_cb)(struct ble_gatt_register_ctxt *ctxt, void *arg);// Callback đăng ký GATT (optional, có thể NULL) 
    esp_err_t (*start_adv)(void);                                             // Bắt đầu phát quảng bá BLE 
    esp_err_t (*stop_adv)(void);                                              // Dừng phát quảng bá BLE 
} app_ble_profile_t;

/**
 * @brief Khởi tạo phần cứng BLE Controller, NimBLE Host stack và liên kết Profile
 * @param p_profile Con trỏ tới cấu trúc profile cần kích hoạt (bắt buộc)
 * @return esp_err_t ESP_OK nếu thành công
 */
esp_err_t app_ble_manager_Init(const app_ble_profile_t *p_profile);

/**
 * @brief Tắt NimBLE Host, giải phóng profile và giải phóng RAM Heap
 * @return esp_err_t ESP_OK nếu thành công
 */
esp_err_t app_ble_manager_Deinit(void);

/**
 * @brief Bắt đầu phát quảng bá BLE thông qua Profile hiện hành
 */
esp_err_t app_ble_manager_StartAdv(void);

/**
 * @brief Dừng phát quảng bá BLE thông qua Profile hiện hành
 */
esp_err_t app_ble_manager_StopAdv(void);

/**
 * @brief Kiểm tra xem BLE Manager có đang hoạt động hay không
 */
bool app_ble_manager_IsRunning(void);

/**
 * @brief Lấy con trỏ Profile đang hoạt động
 */
const app_ble_profile_t *app_ble_manager_GetActiveProfile(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_BLE_MANAGER_H_ */