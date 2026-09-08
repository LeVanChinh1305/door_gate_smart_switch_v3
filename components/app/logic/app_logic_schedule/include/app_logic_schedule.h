/**
 * @file    app_logic_schedule.h
 * @brief   Khai báo API quản lý và thực thi lịch hẹn giờ.
 */

#ifndef APP_LOGIC_SCHEDULE_H
#define APP_LOGIC_SCHEDULE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Khởi tạo và chạy Task kiểm tra lịch hẹn giờ ngầm.
 * @return  esp_err_t ESP_OK nếu tạo Task thành công.
 */
esp_err_t app_logic_schedule_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_LOGIC_SCHEDULE_H */