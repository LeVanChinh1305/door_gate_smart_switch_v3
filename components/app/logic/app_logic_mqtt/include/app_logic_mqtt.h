/**
* @file    app_logic_mqtt.h
* @brief   Khai báo giao diện xử lý logic bản tin MQTT và hàng đợi sự kiện.
*/

#ifndef APP_LOGIC_MQTT_H
#define APP_LOGIC_MQTT_H


#include <stdint.h>
#include "esp_err.h"


#define DF_APP_LOGIC_MQTT_QUEUE_LEN    10U


/**
 * @brief Cấu trúc mục dữ liệu lưu trữ trong hàng đợi MQTT Queue
 */
typedef struct {
    char     *pcData;        // Con trỏ chuỗi payload JSON cấp phát động
    uint32_t  u32DataLen;    // Độ dài dữ liệu payload
} app_logic_mqtt_queue_item_t;

/**
 * @brief   Khởi tạo hàng đợi và tác vụ nền (Task) xử lý logic MQTT.
 * @return  esp_err_t ESP_OK nếu thành công, ngược lại trả về mã lỗi cụ thể.
 */
esp_err_t app_logic_mqtt_Init(void);

/**
 * @brief   Đưa bản tin MQTT thô nhận được từ tầng Middleware vào hàng đợi an toàn.
 * @param   pcData Con trỏ chứa dữ liệu payload thô.
 * @param   u32DataLen Độ dài dữ liệu.
 * @return  esp_err_t ESP_OK nếu đẩy vào queue thành công.
 */
esp_err_t app_logic_mqtt_EnqueueData(const char *pcData, uint32_t u32DataLen);


#endif