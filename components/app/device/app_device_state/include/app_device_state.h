#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Định nghĩa Bitmask đa trạng thái cho thiết bị
 */
typedef enum {
    DEVICE_MODE_NONE            = 0U,
    DEVICE_MODE_UNCONNECTED     = (1U << 0), /* 0x01: Chưa kết nối mạng */
    DEVICE_MODE_CONNECT_AUTO    = (1U << 1), /* 0x02: Đang kết nối tự động (BluFi) */
    DEVICE_MODE_CONNECT_MANUAL  = (1U << 2), /* 0x04: Đang kết nối thủ công */
    DEVICE_MODE_NORMAL          = (1U << 3), /* 0x08: Chế độ điều khiển thường (Wi-Fi) */
    DEVICE_MODE_LOCKED_TEMP     = (1U << 4), /* 0x10: Khóa tạm thời */
    DEVICE_MODE_LOCKED_CHILD    = (1U << 5), /* 0x20: Khóa trẻ em */
    DEVICE_MODE_NIGHT           = (1U << 6), /* 0x40: Cảnh báo ban đêm */
    DEVICE_MODE_OFFLINE_CONTROL = (1U << 7), /* 0x80: Điều khiển dự phòng Bluetooth */
    DEVICE_MODE_LOCKED_RF       = (1U << 8)  /* 0x100: Loại bỏ điều khiển ngoài ý muốn */
} device_mode_t;

/* APIs quản lý Bitmask mới */
uint32_t get_current_door_mode_mask(void);
bool app_device_state_HasMode(device_mode_t eMode);
void app_device_state_SetModeBit(device_mode_t eMode, bool bEnable);

/* Các enum và API khác giữ nguyên */
typedef enum {
    CONTROL_MODE_ROLLING_VENT,
    CONTROL_MODE_ROLLING_NORMAL,
    CONTROL_MODE_GATE_1LEAF,
    CONTROL_MODE_GATE_2LEAF,
    CONTROL_MODE_GATE_SLIDING,
    CONTROL_MODE_GATE_CURTAIN
} control_mode_t;

control_mode_t get_current_control_mode(void);
void set_current_control_mode(control_mode_t mode);

typedef enum {
    SENSOR_TYPE_NONE,
    SENSOR_TYPE_WIRE,
    SENSOR_TYPE_BLE
} sensor_type_t;

sensor_type_t get_current_sensor_type(void);
void set_current_sensor_type(sensor_type_t type);

typedef enum {
    SENSOR_WIRE_TYPE_NONE,
    SENSOR_WIRE_TYPE_DOOR_STATE,
    SENSOR_WIRE_TYPE_ANTI_STUCK
} sensor_wire_type_t;

sensor_wire_type_t get_current_sensor_wire_type(void);
void set_current_sensor_wire_type(sensor_wire_type_t wire_type);