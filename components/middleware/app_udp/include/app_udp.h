/**
 * @file app_udp.h
 * @brief Header khai báo các hàm và hằng số quản lý dịch vụ kết nối thủ công qua UDP.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ====================================================================
 * Hằng số cấu hình UDP (Macro DF_...)
 * ==================================================================== */
#define DF_UDP_SERVER_PORT              (5000U)
#define DF_UDP_BROADCAST_PORT           (5001U)
#define DF_UDP_RX_BUFFER_SIZE           (2048U)
#define DF_UDP_STA_IP_WAIT_MS           (120000U)
#define DF_UDP_ANNOUNCE_INTERVAL_MS     (2500U)

/* ====================================================================
 * Enum mã trạng thái phản hồi UDP (Enum E_...)
 * ==================================================================== */
typedef enum {
    E_UDP_STATUS_SUCCESS             = 50000,
    E_UDP_STATUS_ERROR               = 50004,
    E_UDP_STATUS_NOT_IN_CONFIG_MODE  = 50100  /* Thiết bị không ở trong chế độ cài đặt thủ công */
} app_udp_status_code_t;

/* ====================================================================
 * Khai báo API công khai (Public Function Prototypes)
 * ==================================================================== */

/**
 * @brief Khởi tạo dịch vụ UDP cấu hình thủ công (bật SoftAP, mở UDP socket, tạo task xử lý).
 * @param None.
 * @return ESP_OK nếu thành công; mã lỗi nếu khởi tạo thất bại.
 */
esp_err_t app_udp_Init(void);

/**
 * @brief Hủy và dừng dịch vụ UDP cấu hình thủ công (đóng socket, dừng task, chuyển về chế độ STA).
 * @param None.
 * @return ESP_OK nếu thành công; mã lỗi nếu dừng thất bại.
 */
esp_err_t app_udp_Deinit(void);

/**
 * @brief Kiểm tra xem dịch vụ UDP cấu hình có đang chạy hay không.
 * @param None.
 * @return true nếu đang chạy, false nếu đã dừng hoặc chưa khởi tạo.
 */
bool app_udp_IsRunning(void);

/**
 * @brief Khởi động luồng cấu hình UDP (tương đương app_udp_Init).
 * @param None.
 * @return ESP_OK nếu thành công; mã lỗi nếu thao tác thất bại.
 */
esp_err_t app_udp_StartConfiguration(void);

/**
 * @brief Dừng luồng cấu hình UDP (tương đương app_udp_Deinit).
 * @param None.
 * @return ESP_OK nếu thành công; mã lỗi nếu thao tác thất bại.
 */
esp_err_t app_udp_StopConfiguration(void);

#ifdef __cplusplus
}
#endif