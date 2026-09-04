#ifndef APP_RELAY_H
#define APP_RELAY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// Mapping GPIO theo schematic cửa cuốn
#define DF_RELAY_PIN_CLOSE      GPIO_NUM_4
#define DF_RELAY_PIN_OPEN       GPIO_NUM_0
#define DF_RELAY_PIN_STOP       GPIO_NUM_1

// Định nghĩa mã lệnh điều khiển
typedef enum {
    E_RELAY_CMD_CLOSE = 0,
    E_RELAY_CMD_OPEN,
    E_RELAY_CMD_STOP,
    E_RELAY_CMD_ALL_OFF
} e_app_relay_cmd_t;

/**
 * @brief   Khởi tạo các chân GPIO điều khiển Rơ-le.
 * @return  esp_err_t: ESP_OK nếu thành công.
 */
esp_err_t app_relay_Init(void);

/**
 * @brief   Thực thi lệnh điều khiển rơ-le (Đóng/Mở/Dừng).
 * @param   cmd: Lệnh cần thực thi theo enum e_app_relay_cmd_t.
 * @return  esp_err_t: ESP_OK nếu thành công, ESP_ERR_INVALID_STATE nếu chưa init.
 * @note    Hàm đã tích hợp sẵn delay khóa liên động 100ms để bảo vệ phần cứng.
 */
esp_err_t app_relay_ExecuteCmd(e_app_relay_cmd_t cmd);

#ifdef __cplusplus
}
#endif

#endif /* APP_RELAY_H */