#include "app_logic_touch.h"

#include "app_touch.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "APP_LOGIC_TOUCH";

#define DF_APP_LOGIC_TOUCH_QUEUE_LENGTH  (8U)
#define DF_APP_LOGIC_TOUCH_TASK_STACK    (3072U)
#define DF_APP_LOGIC_TOUCH_TASK_PRIORITY (5U)

static QueueHandle_t g_hTouchCommandQueue = NULL;
static TaskHandle_t g_hTouchTask = NULL;
static bool g_bIsReady = false;

static void app_logic_touch_Task(void *pArg)
{
    e_app_logic_touch_cmd_t eCommand;
    uint8_t u8Status = 0U;
    (void)pArg;

    while (true) {
        if (xQueueReceive(g_hTouchCommandQueue, &eCommand, portMAX_DELAY) == pdPASS) {
            esp_err_t eErr = ESP_OK;
            switch (eCommand) {
                case E_APP_LOGIC_TOUCH_CMD_READ_STATUS:
                    eErr = app_touch_ReadLatchedButtonStatus(&u8Status);
                    if (eErr == ESP_OK) {
                        ESP_LOGI(TAG, "Trạng thái nút cảm ứng: 0x%02X", u8Status);
                    }
                    break;
                case E_APP_LOGIC_TOUCH_CMD_CLEAR_LATCHED:
                    eErr = app_touch_ClearLatched();
                    break;
                case E_APP_LOGIC_TOUCH_CMD_SOFT_RESET:
                    eErr = app_touch_SoftReset();
                    break;
                default:
                    eErr = ESP_ERR_INVALID_ARG;
                    break;
            }
            if (eErr != ESP_OK) {
                ESP_LOGE(TAG, "Xử lý lệnh cảm ứng thất bại: %s", esp_err_to_name(eErr));
            }
        }
    }
}

esp_err_t app_logic_touch_Init(void)
{
    if (g_bIsReady) {
        return ESP_OK;
    }

    esp_err_t eErr = app_touch_Init();
    if (eErr != ESP_OK) {
        return eErr;
    }

    g_hTouchCommandQueue = xQueueCreate(DF_APP_LOGIC_TOUCH_QUEUE_LENGTH,
                                         sizeof(e_app_logic_touch_cmd_t));
    if (g_hTouchCommandQueue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(app_logic_touch_Task, "touch_logic",
                    DF_APP_LOGIC_TOUCH_TASK_STACK, NULL,
                    DF_APP_LOGIC_TOUCH_TASK_PRIORITY, &g_hTouchTask) != pdPASS) {
        vQueueDelete(g_hTouchCommandQueue);
        g_hTouchCommandQueue = NULL;
        return ESP_ERR_NO_MEM;
    }

    g_bIsReady = true;
    return ESP_OK;
}

esp_err_t app_logic_touch_SendCommand(e_app_logic_touch_cmd_t eCommand)
{
    if (!g_bIsReady || g_hTouchCommandQueue == NULL || g_hTouchTask == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (eCommand > E_APP_LOGIC_TOUCH_CMD_SOFT_RESET) {
        return ESP_ERR_INVALID_ARG;
    }
    return xQueueSend(g_hTouchCommandQueue, &eCommand, 0U) == pdPASS
               ? ESP_OK : ESP_ERR_TIMEOUT;
}