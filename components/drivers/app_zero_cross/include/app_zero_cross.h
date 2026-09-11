#ifndef APP_ZERO_CROSS_H
#define APP_ZERO_CROSS_H

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Chân GPIO nhận tín hiệu Zero-Cross */
#define DF_ZCD_GPIO_PIN         GPIO_NUM_2
#define DF_ZERO_CROSS_PIN       DF_ZCD_GPIO_PIN

/** @brief Thời gian timeout tối đa chờ điểm 0 (ms) - Chu kỳ 50Hz = 20ms, 60Hz = 16.6ms */
#define DF_ZCD_TIMEOUT_MS       (30U)

/**
 * @brief Định nghĩa kiểu con trỏ hàm Callback cho ngắt Zero-Cross.
 * @note  Hàm này sẽ được thực thi trong ngữ cảnh Ngắt (ISR Context).
 */
typedef void (*app_zero_cross_cb_t)(void *pArg);

/**
 * @brief   Khởi tạo chân GPIO và đăng ký ngắt Zero-Cross.
 * @param   pfnCb  Con trỏ hàm callback sẽ được gọi khi phát hiện điểm 0.
 * @param   pArg   Tham số tùy chọn truyền vào callback.
 * @return  esp_err_t: ESP_OK nếu thành công.
 */
esp_err_t app_zero_cross_Init(app_zero_cross_cb_t pfnCb, void *pArg);

/**
 * @brief   Bật cờ chờ tín hiệu điểm 0.
 * @return  esp_err_t: ESP_OK nếu thành công, ESP_ERR_INVALID_STATE nếu chưa Init.
 */
esp_err_t app_zero_cross_EnableWait(void);

/**
 * @brief   Tắt cờ chờ tín hiệu điểm 0.
 * @return  esp_err_t: ESP_OK nếu thành công.
 */
esp_err_t app_zero_cross_DisableWait(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_ZERO_CROSS_H */