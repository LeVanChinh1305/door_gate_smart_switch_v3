#ifndef APP_BLE_IBEACON_H
#define APP_BLE_IBEACON_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Cấu trúc dữ liệu gói iBeacon gửi từ Middleware sang tầng Logic
 */
typedef struct {
    uint8_t au8Mac[6];     /*!< Địa chỉ MAC của thiết bị phát (Phone) */
    uint8_t au8Uuid[16];   /*!< 16 bytes Payload UUID đã mã hóa */
    uint16_t u16Major;     /*!< Major ID */
    uint16_t u16Minor;     /*!< Minor ID */
    int8_t i8Rssi;         /*!< Cường độ tín hiệu nhận được (dBm) */
} app_ble_ibeacon_msg_t;

/**
 * @brief  Khởi tạo Middleware BLE iBeacon Scan
 * @param  xQueue Queue handle để đẩy dữ liệu sang tầng Logic
 * @return esp_err_t ESP_OK nếu thành công
 */
esp_err_t app_ble_ibeacon_Init(QueueHandle_t xQueue);

/**
 * @brief  Hủy khởi tạo và dừng Scan BLE iBeacon
 * @return esp_err_t ESP_OK nếu thành công
 */
esp_err_t app_ble_ibeacon_Deinit(void);

/**
 * @brief  Kiểm tra trạng thái hoạt động của Middleware
 * @return true nếu đang chạy, false nếu đã dừng
 */
bool app_ble_ibeacon_IsRunning(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_BLE_IBEACON_H */