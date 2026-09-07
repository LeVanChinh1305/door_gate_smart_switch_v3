#ifndef APP_RELAY_STATE_H
#define APP_RELAY_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Định nghĩa các trạng thái vận hành của rơ-le / cửa
 */
typedef enum {
    E_RELAY_STATE_STOPPED = 0,    // Đang dừng (hoặc vị trí lửng)
    E_RELAY_STATE_OPENING,        // Đang trong tiến trình mở (Up)
    E_RELAY_STATE_CLOSING,        // Đang trong tiến trình đóng (Down)
    E_RELAY_STATE_OPENED,         // Đã mở hoàn toàn
    E_RELAY_STATE_CLOSED,         // Đã đóng hoàn toàn
    E_RELAY_STATE_OBSTACLE,       // Gặp vật cản / Kẹt cửa (Anti-stuck)
    E_RELAY_STATE_SAFETY_LOCKED,  // Bị khóa an toàn (Khóa phím / Khóa trẻ em)
    E_RELAY_STATE_MAX
} e_relay_state_t;

/**
 * @brief Con trỏ hàm callback khi trạng thái rơ-le thay đổi.
 * @param eNewState Trạng thái mới.
 * @param eOldState Trạng thái trước đó.
 */
typedef void (*relay_state_changed_cb_t)(e_relay_state_t eNewState, e_relay_state_t eOldState);

/**
 * @brief   Khởi tạo module quản lý trạng thái rơ-le.
 * @return  esp_err_t ESP_OK nếu thành công.
 */
esp_err_t app_relay_state_Init(void);

/**
 * @brief   Thiết lập trạng thái rơ-le mới và kích hoạt các callback đã đăng ký nếu có thay đổi.
 * @param   eNewState Trạng thái mới cần áp dụng.
 * @return  esp_err_t ESP_OK nếu thành công, ESP_ERR_INVALID_ARG nếu trạng thái không hợp lệ.
 */
esp_err_t app_relay_state_SetState(e_relay_state_t eNewState);

/**
 * @brief   Lấy trạng thái rơ-le hiện tại (Thread-safe).
 * @return  e_relay_state_t Trạng thái hiện tại.
 */
e_relay_state_t app_relay_state_GetState(void);

/**
 * @brief   Đăng ký hàm callback nhận sự kiện thay đổi trạng thái rơ-le.
 * @param   fnCallback Con trỏ hàm callback.
 * @return  esp_err_t ESP_OK nếu đăng ký thành công, ESP_ERR_NO_MEM nếu vượt quá số lượng cho phép.
 */
esp_err_t app_relay_state_RegisterCallback(relay_state_changed_cb_t fnCallback);

/**
 * @brief   Chuyển mã enum trạng thái thành chuỗi văn bản để hiển thị/ghi log.
 * @param   eState Trạng thái cần chuyển.
 * @return  const char* Chuỗi tên trạng thái.
 */
const char *app_relay_state_ToString(e_relay_state_t eState);

#ifdef __cplusplus
}
#endif

#endif /* APP_RELAY_STATE_H */