/**
 * @file app_logic_led.c
 * @brief Điều phối màu sắc và độ sáng LED bằng queue và task Application Layer.
 */

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
    E_APP_LOGIC_LED_CMD_SHOW, 
    E_APP_LOGIC_LED_CMD_SET_PIXEL
} e_app_logic_led_cmd_t; 

typedef struct {
    e_app_logic_led_cmd_t eType;
    app_led_color_t sColor;
    uint8_t u8Brightness;
    uint8_t u8LedIndex; 
} app_logic_led_queue_item_t;

static QueueHandle_t g_hLedCommandQueue = NULL;
static TaskHandle_t g_hLedTask = NULL;
static bool g_bIsReady = false;

/**
 * @brief Task nhận và thực thi tuần tự các lệnh LED.
 * @param pArg Tham số task, hiện không sử dụng.
 * @return Không trả về; task chạy vô hạn.
 */
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
            } else if(sItem.eType == E_APP_LOGIC_LED_CMD_SET_PIXEL){
                eErr = app_led_SetPixelRgb((int)sItem.u8LedIndex, sItem.sColor);
            } else {
                eErr = ESP_ERR_INVALID_ARG;
            }
            if (eErr != ESP_OK) {
                ESP_LOGE(TAG, "Xử lý lệnh LED thất bại: %s", esp_err_to_name(eErr));
            }
        }
    }
}

/**
 * @brief Đưa một lệnh LED vào queue.
 * @param pItem Con trỏ đến dữ liệu lệnh cần gửi.
 * @return ESP_OK nếu gửi thành công; mã lỗi nếu driver chưa sẵn sàng hoặc queue đầy.
 */
static esp_err_t app_logic_led_SendItem(const app_logic_led_queue_item_t *pItem)
{
    if (!g_bIsReady || g_hLedCommandQueue == NULL || g_hLedTask == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueSend(g_hLedCommandQueue, pItem, 0U) == pdPASS
               ? ESP_OK : ESP_ERR_TIMEOUT;
}

/**
 * @brief Khởi tạo driver LED, queue và task điều khiển.
 * @param None.
 * @return ESP_OK nếu thành công; mã lỗi nếu khởi tạo thất bại.
 */
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
    if (xTaskCreate(app_logic_led_Task, "led_logic",DF_APP_LOGIC_LED_TASK_STACK, NULL,DF_APP_LOGIC_LED_TASK_PRIORITY, &g_hLedTask) != pdPASS) {
        vQueueDelete(g_hLedCommandQueue);
        g_hLedCommandQueue = NULL;
        return ESP_ERR_NO_MEM;
    }
    g_bIsReady = true;
    return ESP_OK;
}

/**
 * @brief Gửi lệnh đặt màu cho toàn bộ dải LED.
 * @param sColor Màu RGB cần hiển thị.
 * @return ESP_OK nếu gửi thành công; mã lỗi nếu queue chưa sẵn sàng hoặc đầy.
 */
esp_err_t app_logic_led_SetColor(app_led_color_t sColor)
{
    app_logic_led_queue_item_t sItem = {
        .eType = E_APP_LOGIC_LED_CMD_SET_COLOR,
        .sColor = sColor
    };
    return app_logic_led_SendItem(&sItem);
}

/**
 * @brief Gửi lệnh đặt độ sáng LED.
 * @param u8Brightness Độ sáng từ 0 đến 255.
 * @return ESP_OK nếu gửi thành công; mã lỗi nếu queue chưa sẵn sàng hoặc đầy.
 */
esp_err_t app_logic_led_SetBrightness(uint8_t u8Brightness)
{
    app_logic_led_queue_item_t sItem = {
        .eType = E_APP_LOGIC_LED_CMD_SET_BRIGHTNESS,
        .u8Brightness = u8Brightness
    };
    return app_logic_led_SendItem(&sItem);
}

/**
 * @brief Gửi lệnh xuất màu hiện tại ra dải LED.
 * @param None.
 * @return ESP_OK nếu gửi thành công; mã lỗi nếu queue chưa sẵn sàng hoặc đầy.
 */
esp_err_t app_logic_led_Show(void)
{
    app_logic_led_queue_item_t sItem = {.eType = E_APP_LOGIC_LED_CMD_SHOW};
    return app_logic_led_SendItem(&sItem);
}


esp_err_t app_logic_led_SetPixelColor(uint8_t u8LedIndex, app_led_color_t sColor){
    app_logic_led_queue_item_t sItem = {
        .eType = E_APP_LOGIC_LED_CMD_SET_PIXEL,
        .sColor = sColor,
        .u8LedIndex = u8LedIndex
    }; 
    return app_logic_led_SendItem(&sItem); 
}