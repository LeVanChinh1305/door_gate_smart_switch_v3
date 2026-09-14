#pragma once 

/**
 * @file app_wifi.h
 * @brief API quản lý kết nối Wi-Fi STA và trạng thái kết nối.
 */

#include <stdint.h>
#include "esp_err.h"
#include "stdbool.h"

#define DF_WIFI_CONNECTED_BIT       BIT0
#define DF_WIFI_FAIL_BIT            BIT1
#define DF_WIFI_MAX_RETRY_COUNT     (100U)
#define DF_WIFI_MAX_TX_POWER        (40U)


#ifdef __cplusplus
extern "C"{
#endif

/**
 * @brief Khởi tạo ngăn xếp mạng, Wi-Fi STA và các trình xử lý sự kiện.
 * @param None.
 * @return ESP_OK nếu khởi tạo thành công; mã lỗi nếu thao tác thất bại.
 */
esp_err_t app_wifi_InitSta(void);

/**
 * @brief Kiểm tra Wi-Fi đã nhận địa chỉ IP hay chưa.
 * @param None.
 * @return true nếu đã kết nối; false nếu chưa kết nối hoặc chưa khởi tạo.
 */
bool app_wifi_IsConnected(void);

/**
 * @brief Chủ động ngắt kết nối Wi-Fi STA và xóa cờ trạng thái kết nối.
 * @param None.
 * @return ESP_OK nếu thành công; mã lỗi nếu thao tác thất bại.
 */
esp_err_t app_wifi_Disconnect(void);

/**
 * @brief Cấu hình bật/tắt cơ chế tự động thử kết nối lại khi Wi-Fi bị ngắt.
 * @param bEnable true để bật tự động thử lại, false để tắt.
 * @return None.
 */
void app_wifi_SetAutoReconnect(bool bEnable);

/**
 * @brief Chờ Wi-Fi kết nối trong khoảng thời gian chỉ định.
 * @param u32TimeoutMs Thời gian chờ tối đa, tính bằng mili-giây.
 * @return true nếu kết nối thành công; false nếu hết thời gian hoặc thất bại.
 */
bool app_wifi_WaitForConnect(uint32_t u32TimeoutMs);


#ifdef __cplusplus
}
#endif 


