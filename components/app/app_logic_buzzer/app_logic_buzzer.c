#include "app_logic_buzzer.h"

#include "app_buzzer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "APP_LOGIC_BUZZER";

#define DF_APP_LOGIC_BUZZER_QUEUE_LENGTH  (8U)
#define DF_APP_LOGIC_BUZZER_TASK_STACK    (3072U)
#define DF_APP_LOGIC_BUZZER_TASK_PRIORITY (5U)

typedef enum {
    E_APP_LOGIC_BUZZER_CMD_ON = 0,
    E_APP_LOGIC_BUZZER_CMD_OFF,
    E_APP_LOGIC_BUZZER_CMD_BEEP
} e_app_logic_buzzer_cmd_t;

typedef struct {
    e_app_logic_buzzer_cmd_t eType;
    uint32_t u32DurationMs;
} app_logic_buzzer_queue_item_t;

static QueueHandle_t g_hBuzzerCommandQueue = NULL;
static TaskHandle_t g_hBuzzerTask = NULL;
static bool g_bIsReady = false;

static void app_logic_buzzer_Task(void *pArg)
{
    app_logic_buzzer_queue_item_t sItem;
    (void)pArg;
    while (true) {
        if (xQueueReceive(g_hBuzzerCommandQueue, &sItem, portMAX_DELAY) == pdPASS) {
            esp_err_t eErr = ESP_ERR_INVALID_ARG;
            if (sItem.eType == E_APP_LOGIC_BUZZER_CMD_ON) {
                eErr = app_buzzer_On();
            } else if (sItem.eType == E_APP_LOGIC_BUZZER_CMD_OFF) {
                eErr = app_buzzer_Off();
            } else if (sItem.eType == E_APP_LOGIC_BUZZER_CMD_BEEP) {
                eErr = app_buzzer_Beep(sItem.u32DurationMs);
            }
            if (eErr != ESP_OK) {
                ESP_LOGE(TAG, "Xử lý lệnh buzzer thất bại: %s", esp_err_to_name(eErr));
            }
        }
    }
}

static esp_err_t app_logic_buzzer_SendItem(const app_logic_buzzer_queue_item_t *pItem)
{
    if (!g_bIsReady || g_hBuzzerCommandQueue == NULL || g_hBuzzerTask == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueSend(g_hBuzzerCommandQueue, pItem, 0U) == pdPASS
               ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t app_logic_buzzer_Init(void)
{
    if (g_bIsReady) {
        return ESP_OK;
    }
    esp_err_t eErr = app_buzzer_Init();
    if (eErr != ESP_OK) {
        return eErr;
    }
    g_hBuzzerCommandQueue = xQueueCreate(DF_APP_LOGIC_BUZZER_QUEUE_LENGTH,
                                          sizeof(app_logic_buzzer_queue_item_t));
    if (g_hBuzzerCommandQueue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(app_logic_buzzer_Task, "buzzer_logic",
                    DF_APP_LOGIC_BUZZER_TASK_STACK, NULL,
                    DF_APP_LOGIC_BUZZER_TASK_PRIORITY, &g_hBuzzerTask) != pdPASS) {
        vQueueDelete(g_hBuzzerCommandQueue);
        g_hBuzzerCommandQueue = NULL;
        return ESP_ERR_NO_MEM;
    }
    g_bIsReady = true;
    return ESP_OK;
}

esp_err_t app_logic_buzzer_On(void)
{
    app_logic_buzzer_queue_item_t sItem = {.eType = E_APP_LOGIC_BUZZER_CMD_ON};
    return app_logic_buzzer_SendItem(&sItem);
}

esp_err_t app_logic_buzzer_Off(void)
{
    app_logic_buzzer_queue_item_t sItem = {.eType = E_APP_LOGIC_BUZZER_CMD_OFF};
    return app_logic_buzzer_SendItem(&sItem);
}

esp_err_t app_logic_buzzer_Beep(uint32_t u32DurationMs)
{
    if (u32DurationMs == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    app_logic_buzzer_queue_item_t sItem = {
        .eType = E_APP_LOGIC_BUZZER_CMD_BEEP,
        .u32DurationMs = u32DurationMs
    };
    return app_logic_buzzer_SendItem(&sItem);
}