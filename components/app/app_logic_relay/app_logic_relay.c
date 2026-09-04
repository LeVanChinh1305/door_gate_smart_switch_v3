#include "app_logic_relay.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "APP_LOGIC_RELAY";

#define DF_APP_LOGIC_RELAY_QUEUE_LENGTH (8U)
#define DF_APP_LOGIC_RELAY_TASK_STACK   (3072U)
#define DF_APP_LOGIC_RELAY_TASK_PRIORITY (5U)

static QueueHandle_t g_hRelayCommandQueue = NULL;
static TaskHandle_t g_hRelayTask = NULL;
static bool g_bIsReady = false;

static void app_logic_relay_Task(void *pArg)
{
    e_app_relay_cmd_t eCommand;
    (void)pArg;

    while (true) {
        if (xQueueReceive(g_hRelayCommandQueue, &eCommand, portMAX_DELAY) == pdPASS) {
            esp_err_t eErr = app_relay_ExecuteCmd(eCommand);
            if (eErr != ESP_OK) {
                ESP_LOGE(TAG, "Thực thi lệnh relay thất bại: %s", esp_err_to_name(eErr));
            }
        }
    }
}

esp_err_t app_logic_relay_Init(void)
{
    if (g_bIsReady) {
        return ESP_OK;
    }

    esp_err_t eErr = app_relay_Init();
    if (eErr != ESP_OK) {
        return eErr;
    }

    g_hRelayCommandQueue = xQueueCreate(DF_APP_LOGIC_RELAY_QUEUE_LENGTH, sizeof(e_app_relay_cmd_t));
    if (g_hRelayCommandQueue == NULL) {
        ESP_LOGE(TAG, "Tạo hàng đợi lệnh relay thất bại");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t xTaskResult = xTaskCreate(app_logic_relay_Task,
                                         "relay_logic",
                                         DF_APP_LOGIC_RELAY_TASK_STACK,
                                         NULL,
                                         DF_APP_LOGIC_RELAY_TASK_PRIORITY,
                                         &g_hRelayTask);
    if (xTaskResult != pdPASS) {
        ESP_LOGE(TAG, "Tạo task xử lý relay thất bại");
        vQueueDelete(g_hRelayCommandQueue);
        g_hRelayCommandQueue = NULL;
        return ESP_ERR_NO_MEM;
    }

    g_bIsReady = true;
    return ESP_OK;
}

esp_err_t app_logic_relay_SendCommand(e_app_relay_cmd_t eCommand)
{
    if ((!g_bIsReady) || (g_hRelayCommandQueue == NULL) || (g_hRelayTask == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    if ((eCommand < E_RELAY_CMD_CLOSE) || (eCommand > E_RELAY_CMD_ALL_OFF)) {
        return ESP_ERR_INVALID_ARG;
    }

    return xQueueSend(g_hRelayCommandQueue, &eCommand, 0U) == pdPASS
               ? ESP_OK : ESP_ERR_TIMEOUT;
}