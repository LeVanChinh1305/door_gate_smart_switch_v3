#ifndef APP_BLE_MESH_H_
#define APP_BLE_MESH_H_


#include "esp_err.h"
#include "stdbool.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief khởi tạo BLE Mesh (profile)
 * @note gọi sau khi vào chế độ normal 
 */
esp_err_t app_ble_mesh_Init(void);

/**
 * @brief hủy ble mesh và giải phóng tài nguyên
 */
esp_err_t app_ble_mesh_Deinit(void);

/**
 * @brief kiểm tra mesh đang chạy hay không
 */
bool app_ble_mesh_IsRunning(void);

/**
 * @brief gửi lệnh điều khiển qua Vendor model 
 * 
 */
esp_err_t app_ble_mesh_SendGateCommand(uint16_t dest_addr, uint8_t command);


#ifdef __cplusplus
}
#endif

#endif