#ifndef APP_LOGIC_RELAY_H
#define APP_LOGIC_RELAY_H

/**
 * @file app_logic_relay.h
 * @brief API tầng Application điều phối các lệnh relay qua queue và task bất đồng bộ.
 */

#include "app_relay.h"
#include "app_relay_state.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Định nghĩa các mã lệnh nghiệp vụ relay
 */
typedef enum {
    E_APP_LOGIC_RELAY_CMD_OPEN = 0,
    E_APP_LOGIC_RELAY_CMD_CLOSE,
    E_APP_LOGIC_RELAY_CMD_STOP,
    E_APP_LOGIC_RELAY_CMD_EMERGENCY_STOP,
    E_APP_LOGIC_RELAY_CMD_ALL_OFF
} e_app_logic_relay_cmd_t;

/**
 * @brief Bản tin gửi vào queue điều khiển relay
 */
typedef struct {
    e_app_logic_relay_cmd_t eCmd;
    uint32_t u32PulseDurationMs;    // 0 = sử dụng mặc định 500ms
    bool bForceOverride;            // Bỏ qua kiểm tra khóa (dành cho dừng khẩn cấp/vật cản)
} app_logic_relay_msg_t;

/**
 * @brief Khởi tạo driver relay, module relay state, queue và task xử lý lệnh.
 * @return ESP_OK nếu thành công; mã lỗi nếu khởi tạo thất bại.
 */
esp_err_t app_logic_relay_Init(void);

/**
 * @brief Gửi một bản tin lệnh đầy đủ vào queue xử lý bất đồng bộ.
 * @param pMsg Con trỏ tới bản tin app_logic_relay_msg_t.
 * @return ESP_OK nếu đưa lệnh vào queue thành công; mã lỗi nếu thất bại.
 */
esp_err_t app_logic_relay_SendMsg(const app_logic_relay_msg_t *pMsg);

/**
 * @brief Gửi lệnh mở cửa (UP) vào queue.
 * @return ESP_OK nếu thành công.
 */
esp_err_t app_logic_relay_Open(void);

/**
 * @brief Gửi lệnh đóng cửa (DOWN) vào queue.
 * @return ESP_OK nếu thành công.
 */
esp_err_t app_logic_relay_Close(void);

/**
 * @brief Gửi lệnh dừng cửa (STOP) vào queue.
 * @return ESP_OK nếu thành công.
 */
esp_err_t app_logic_relay_Stop(void);

/**
 * @brief Dừng khẩn cấp và ngắt tức thì toàn bộ các rơ-le.
 * @return ESP_OK nếu thành công.
 */
esp_err_t app_logic_relay_EmergencyStop(void);

/**
 * @brief API tương thích ngược: Gửi lệnh phần cứng trực tiếp theo enum e_app_relay_cmd_t.
 * @param eCommand Lệnh relay cần thực thi.
 * @return ESP_OK nếu đưa lệnh vào queue thành công; mã lỗi nếu thất bại.
 */
esp_err_t app_logic_relay_SendCommand(e_app_relay_cmd_t eCommand);

#ifdef __cplusplus
}
#endif

#endif /* APP_LOGIC_RELAY_H */