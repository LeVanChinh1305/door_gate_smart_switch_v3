#ifndef APP_HEADER2H_H
#define APP_HEADER2H_H

#include "driver/gpio.h"
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Chân GPIO nhận tín hiệu cảm biến tiếp điểm / công tắc hành trình header 2-pin */
#define DF_HEADER2H_GPIO_PIN       GPIO_NUM_3
#define DF_HEADER2H_ISR_FLAGS      (0U)

/** @brief Mức logic cảm biến khi tiếp điểm đóng (kéo xuống GND) hoặc hở */
#define DF_HEADER2H_STATE_CLOSED   (0)
#define DF_HEADER2H_STATE_OPENED   (1)

/**
 * @brief Định nghĩa kiểu con trỏ hàm Callback khi mức logic cảm biến thay đổi.
 * @note  Hàm này được thực thi trong ngữ cảnh Ngắt (ISR Context).
 * @param i32Level Mức logic mới đọc được từ GPIO (0 hoặc 1).
 * @param pArg     Tham số con trỏ tùy chọn truyền vào callback.
 */
typedef void (*app_header2h_cb_t)(int i32Level, void *pArg);

/**
 * @brief   Khởi tạo chân GPIO cảm biến header 2-pin với ngắt 2 cạnh (ANYEDGE) và pull-up nội.
 * @param   pfnCb  Con trỏ hàm callback ISR.
 * @param   pArg   Tham số con trỏ truyền vào callback.
 * @return  esp_err_t: ESP_OK nếu khởi tạo thành công.
 */
esp_err_t app_header2h_Init(app_header2h_cb_t pfnCb, void *pArg);

/**
 * @brief   Đọc trực tiếp mức logic hiện tại của chân GPIO cảm biến header 2-pin.
 * @param   pLevel Con trỏ lưu giá trị mức logic đọc được (0 hoặc 1).
 * @return  esp_err_t: ESP_OK nếu đọc thành công.
 */
esp_err_t app_header2h_Read(int *pLevel);

/**
 * @brief   Kiểm tra nhanh xem cửa có đang ở trạng thái đóng hoàn toàn hay không.
 * @return  bool: true nếu cảm biến báo đóng (mức 0), false nếu mở hoặc chưa khởi tạo.
 */
bool app_header2h_IsDoorClosed(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_HEADER2H_H */