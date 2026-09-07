/**
 * @file    app_led_state.c
 * @brief   Triển khai bộ điều phối hiệu ứng LED dựa trên trạng thái thiết bị 
 */

#include "app_led_state.h"
#include "app_logic_led.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_common.h"

static const char *TAG = "APP_LED_STATE";

static e_led_device_state_t g_eCurrentState = E_LED_STATE_MAX;
static TaskHandle_t g_hLedStateTask = NULL;
static bool g_bTaskRunning = false;

/**
 * @brief   Task nền chuyên trách thực thi các hiệu ứng chớp nháy hoặc đổi màu theo State.
 */
static void app_led_state_Task(void *pArg)
{
    (void)pArg;
    bool bToggle = false;

    while (g_bTaskRunning) {
        switch (g_eCurrentState) {
            case E_LED_STATE_BLUFI_AUTO:
                /* Nhấp nháy xanh dương (BluFi - Giữ 3s) */
                if (bToggle) {
                    (void)app_logic_led_SetColor(APP_LED_COLOR_BLUE);
                } else {
                    (void)app_logic_led_SetColor(APP_LED_COLOR_OFF);
                }
                vTaskDelay(pdMS_TO_TICKS(500U));
                break;
            case E_LED_STATE_CONNECT_MANUAL:
                /* Nhấp nháy đỏ (Thủ công - Giữ 7s) */
                if (bToggle) {
                    (void)app_logic_led_SetColor(APP_LED_COLOR_RED);
                } else {
                    (void)app_logic_led_SetColor(APP_LED_COLOR_OFF);
                }
                vTaskDelay(pdMS_TO_TICKS(500U));
                break;

            case E_LED_STATE_LOCKED:
                /* Trạng thái khóa: Sáng mờ hoặc màu vàng tĩnh */
                (void)app_logic_led_SetColor(APP_LED_COLOR_YELLOW); 
                vTaskDelay(pdMS_TO_TICKS(1000U));
                break;

            case E_LED_STATE_WARNING:
                /* Cảnh báo: Nhấp nháy đỏ liên tục tốc độ cao */
                if (bToggle) {
                    (void)app_logic_led_SetColor(APP_LED_COLOR_RED);
                } else {
                    (void)app_logic_led_SetColor(APP_LED_COLOR_OFF);
                }
                vTaskDelay(pdMS_TO_TICKS(200U));
                break;
            case E_LED_STATE_GATE_UP:
                (void)app_logic_led_SetPixelColor(0, APP_LED_COLOR_OFF);
                (void)app_logic_led_SetPixelColor(1, APP_LED_COLOR_OFF);
                (void)app_logic_led_SetPixelColor(2, APP_LED_COLOR_OFF);
                (void)app_logic_led_SetPixelColor(2, APP_LED_COLOR_GREEN); // Chỉ bật LED 0
                (void)app_logic_led_Show();
                vTaskDelay(pdMS_TO_TICKS(500U));
                break;

            case E_LED_STATE_GATE_DOWN:
                (void)app_logic_led_SetPixelColor(0, APP_LED_COLOR_OFF);
                (void)app_logic_led_SetPixelColor(1, APP_LED_COLOR_OFF);
                (void)app_logic_led_SetPixelColor(2, APP_LED_COLOR_OFF);
                (void)app_logic_led_SetPixelColor(0, APP_LED_COLOR_GREEN);   // Chỉ bật LED 1
                (void)app_logic_led_Show();
                vTaskDelay(pdMS_TO_TICKS(500U));
                break;

            case E_LED_STATE_GATE_STOP:
                (void)app_logic_led_SetPixelColor(0, APP_LED_COLOR_OFF);
                (void)app_logic_led_SetPixelColor(1, APP_LED_COLOR_OFF);
                (void)app_logic_led_SetPixelColor(2, APP_LED_COLOR_OFF);
                (void)app_logic_led_SetPixelColor(1, APP_LED_COLOR_GREEN); // Chỉ bật LED 2
                (void)app_logic_led_Show();
                vTaskDelay(pdMS_TO_TICKS(500U));
                break;

            case E_LED_STATE_NORMAL_IDLE:
            default:
                /* Trạng thái bình thường: sáng nhẹ hoặc tắt tùy thiết kế */
                (void)app_logic_led_SetColor(APP_LED_COLOR_OFF);
                vTaskDelay(pdMS_TO_TICKS(2000U));
                break;
        }
        bToggle = !bToggle;
    }

    vTaskDelete(NULL);
}

esp_err_t app_led_state_Init(void)
{
    if (g_bTaskRunning) {
        return ESP_OK;
    }

    g_eCurrentState = E_LED_STATE_NORMAL_IDLE;
    g_bTaskRunning = true;

    BaseType_t xRet = xTaskCreate(app_led_state_Task,
                                  "led_state_task",
                                  DF_TASK_STACK_MIN,
                                  NULL,
                                  DF_TASK_PRIO_LOW,
                                  &g_hLedStateTask);
    if (xRet != pdPASS) {
        g_bTaskRunning = false;
        ESP_LOGE(TAG, "Tạo task quản lý trạng thái LED thất bại");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Khởi tạo module led_device_state thành công");
    return ESP_OK;
}

esp_err_t app_led_state_SetState(e_led_device_state_t eState)
{
    if (eState >= E_LED_STATE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (g_eCurrentState != eState) {
        g_eCurrentState = eState;
        ESP_LOGI(TAG, "Chuyển trạng thái LED sang mode: %d", (int)eState);
    }
    return ESP_OK;
}

e_led_device_state_t app_led_state_GetState(void)
{
    return g_eCurrentState;
}