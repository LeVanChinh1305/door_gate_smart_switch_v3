#include "app_logic_led.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "APP_LOGIC_LED";

#define DF_APP_LOGIC_LED_QUEUE_LENGTH  (8U)
#define DF_APP_LOGIC_LED_TASK_STACK    (3072U)
#define DF_APP_LOGIC_LED_TASK_PRIORITY (5U)

typedef enum {
    E_APP_LOGIC_LED_CMD_SET_COLOR = 0,
    E_APP_LOGIC_LED_CMD_SET_BRIGHTNESS,
    E_APP_LOGIC_LED_CMD_SHOW
} e_app_logic_led_cmd_t;

typedef struct {
    e_app_logic_led_cmd_t eType;
    app_led_color_t sColor;
    uint8_t u8Brightness;
} app_logic_led_queue_item_t;

static QueueHandle_t g_hLedCommandQueue = NULL;
static TaskHandle_t g_hLedTask = NULL;
static bool g_bIsReady = false;

static void app_logic_led_Task(void *pArg)
{
    app_logic_led_queue_item_t sItem;
    (void)pArg;
    while (true) {
        if (xQueueReceive(g_hLedCommandQueue, &sItem, portMAX_DELAY) == pdPASS) {
            esp_err_t eErr = ESP_OK;
            if (sItem.eType == E_APP_LOGIC_LED_CMD_SET_COLOR) {
                eErr = app_led_SetAllColor(sItem.sColor);
            } else if (sItem.eType == E_APP_LOGIC_LED_CMD_SET_BRIGHTNESS) {
                eErr = app_led_SetBrightness(sItem.u8Brightness);
            } else if (sItem.eType == E_APP_LOGIC_LED_CMD_SHOW) {
                eErr = app_led_Show();
            } else {
                eErr = ESP_ERR_INVALID_ARG;
            }
            if (eErr != ESP_OK) {
                ESP_LOGE(TAG, "Xử lý lệnh LED thất bại: %s", esp_err_to_name(eErr));
            }
        }
    }
}

static esp_err_t app_logic_led_SendItem(const app_logic_led_queue_item_t *pItem)
{
    if (!g_bIsReady || g_hLedCommandQueue == NULL || g_hLedTask == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueSend(g_hLedCommandQueue, pItem, 0U) == pdPASS
               ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t app_logic_led_Init(void)
{
    if (g_bIsReady) {
        return ESP_OK;
    }
    esp_err_t eErr = app_led_Init(DF_LED_GPIO);
    if (eErr != ESP_OK) {
        return eErr;
    }
    g_hLedCommandQueue = xQueueCreate(DF_APP_LOGIC_LED_QUEUE_LENGTH,
                                       sizeof(app_logic_led_queue_item_t));
    if (g_hLedCommandQueue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(app_logic_led_Task, "led_logic",
                    DF_APP_LOGIC_LED_TASK_STACK, NULL,
                    DF_APP_LOGIC_LED_TASK_PRIORITY, &g_hLedTask) != pdPASS) {
        vQueueDelete(g_hLedCommandQueue);
        g_hLedCommandQueue = NULL;
        return ESP_ERR_NO_MEM;
    }
    g_bIsReady = true;
    return ESP_OK;
}

esp_err_t app_logic_led_SetColor(app_led_color_t sColor)
{
    app_logic_led_queue_item_t sItem = {
        .eType = E_APP_LOGIC_LED_CMD_SET_COLOR,
        .sColor = sColor
    };
    return app_logic_led_SendItem(&sItem);
}

esp_err_t app_logic_led_SetBrightness(uint8_t u8Brightness)
{
    app_logic_led_queue_item_t sItem = {
        .eType = E_APP_LOGIC_LED_CMD_SET_BRIGHTNESS,
        .u8Brightness = u8Brightness
    };
    return app_logic_led_SendItem(&sItem);
}

esp_err_t app_logic_led_Show(void)
{
    app_logic_led_queue_item_t sItem = {.eType = E_APP_LOGIC_LED_CMD_SHOW};
    return app_logic_led_SendItem(&sItem);
}