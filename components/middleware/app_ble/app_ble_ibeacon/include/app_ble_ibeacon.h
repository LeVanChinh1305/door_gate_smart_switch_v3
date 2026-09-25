/**
 * @file    app_ble_ibeacon.h
 * @brief   Middleware quét và xử lý gói tin BLE iBeacon (Vconnex Smart Switch)
 *
 * @note    Header chỉ chứa khai báo macro, kiểu dữ liệu và prototype hàm công khai.
 */

#ifndef APP_BLE_IBEACON_H
#define APP_BLE_IBEACON_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Hằng số cấu hình iBeacon */
#define DF_IBEACON_MAC_LEN              (6U)
#define DF_IBEACON_UUID_LEN             (16U)

/**
 * @brief Cấu trúc dữ liệu gói iBeacon gửi từ Middleware sang tầng Logic
 */
typedef struct {
    uint8_t  au8Mac[DF_IBEACON_MAC_LEN];    /*!< Địa chỉ MAC của thiết bị phát */
    uint8_t  au8Uuid[DF_IBEACON_UUID_LEN];  /*!< 16 bytes Payload UUID đã mã hóa */
    uint16_t u16Major;                      /*!< Major ID */
    uint16_t u16Minor;                      /*!< Minor ID */
    int8_t   i8Rssi;                        /*!< Cường độ tín hiệu nhận được (dBm) */
} app_ble_ibeacon_msg_t;

/**
 * @brief  Khởi tạo Middleware BLE iBeacon Scan
 * @param  xQueue Queue handle để đẩy dữ liệu sang tầng Logic
 * @return esp_err_t ESP_OK nếu thành công, mã lỗi nếu thất bại
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