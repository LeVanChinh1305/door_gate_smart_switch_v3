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
#define DF_RELAY_PIN_CLOSE                  GPIO_NUM_4
#define DF_RELAY_PIN_OPEN                   GPIO_NUM_0
#define DF_RELAY_PIN_STOP                   GPIO_NUM_1

#define DF_RELAY_DEFAULT_PULSE_DURATION_MS  (500U)

// Định nghĩa mã lệnh điều khiển phần cứng
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
 * @brief   Thực thi lệnh điều khiển rơ-le (Đóng/Mở/Dừng) ở chế độ giữ (Latch).
 * @param   cmd: Lệnh cần thực thi theo enum e_app_relay_cmd_t.
 * @return  esp_err_t: ESP_OK nếu thành công, ESP_ERR_INVALID_STATE nếu chưa init.
 * @note    Hàm đã tích hợp sẵn delay khóa liên động 100ms để bảo vệ cơ khí và tiếp điểm.
 */
esp_err_t app_relay_ExecuteCmd(e_app_relay_cmd_t cmd);

/**
 * @brief   Kích hoạt rơ-le dạng xung (Pulse): Kích rơ-le trong thời gian quy định rồi tự ngắt.
 * @param   cmd: Lệnh relay cần kích (CLOSE, OPEN, STOP).
 * @param   u32PulseDurationMs: Thời gian giữ xung tính bằng ms (mặc định 500ms nếu truyền 0).
 * @return  esp_err_t: ESP_OK nếu thành công.
 */
esp_err_t app_relay_TriggerPulse(e_app_relay_cmd_t cmd, uint32_t u32PulseDurationMs);

/**
 * @brief   Ngắt khẩn cấp toàn bộ các rơ-le ngay lập tức không delay.
 * @return  esp_err_t: ESP_OK nếu thành công.
 */
esp_err_t app_relay_EmergencyStop(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_RELAY_H */