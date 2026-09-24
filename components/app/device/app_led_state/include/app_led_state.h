#ifndef LED_DEVICE_STATE_H
#define LED_DEVICE_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "app_led.h"

/**
 * @brief Định nghĩa các trạng thái hoạt động của thiết bị liên quan đến hiển thị LED
 */
typedef enum {
    E_LED_STATE_UNCONNECTED = 0,    // Khi vừa khởi động lên chưa cấu hình: Nhấp nháy trắng nhạt
    E_LED_STATE_NORMAL_IDLE,        // Trạng thái bình thường / Chờ lệnh kết nối
    E_LED_STATE_BLUFI_AUTO,         // Chế độ kết nối tự động (BluFi - Giữ 3s): Nhấp nháy xanh dương
    E_LED_STATE_CONNECT_MANUAL,     // Chế độ kết nối thủ công (Giữ 7s): Nhấp nháy đỏ
    E_LED_STATE_CONNECT_BLE_MESH,   // chế độ kết nối ble
    E_LED_STATE_NORMAL,             // Chế độ đã kết nối, hoạt động bình thường
    E_LED_STATE_LOCKED,             // Trạng thái khóa tạm thời
    E_LED_STATE_LOCKED_CHILD,       // Khóa trẻ em
    E_LED_STATE_LOCKED_RF,          // Loại bỏ điều khiển ngoài ý muốn (Khóa phím theo giờ)
    E_LED_STATE_WARNING,            // Cảnh báo (An ninh / Xô lô / Còi hú)
    E_LED_STATE_OTA,                // Đang cập nhật firmware OTA
    E_LED_STATE_GATE_UP,            // Đang mở
    E_LED_STATE_GATE_DOWN,          // Đang đóng
    E_LED_STATE_GATE_STOP,          // Dừng
    E_LED_STATE_MAX
} e_led_device_state_t;

/**
 * @brief   Khởi tạo module quản lý trạng thái LED và tác vụ định thời hiệu ứng.
 * @return  esp_err_t ESP_OK nếu thành công.
 */
esp_err_t app_led_state_Init(void);

/**
 * @brief   Thiết lập trạng thái hiển thị LED hiện tại của thiết bị.
 * @param   eState Trạng thái cần chuyển đổi (thuộc e_led_device_state_t).
 * @return  esp_err_t ESP_OK nếu chuyển trạng thái thành công.
 */
esp_err_t app_led_state_SetState(e_led_device_state_t eState);

/**
 * @brief   Lấy trạng thái LED hiện tại của thiết bị.
 * @return  e_led_device_state_t Trạng thái hiện tại.
 */
e_led_device_state_t app_led_state_GetState(void);

/**
 * @brief   BỔ SUNG: Tự động đồng bộ từ Bitmask của app_device_state sang LED State.
 */
void app_led_state_UpdateFromDeviceMask(void);

#endif /* LED_DEVICE_STATE_H */