/**
 * @file app_logic_relay.c
 * @brief Điều phối lệnh relay từ Application Layer bằng queue và task riêng.
 */

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

/**
 * @brief Task nhận và thực thi tuần tự các lệnh relay.
 * @param pArg Tham số task, hiện không sử dụng.
 * @return Không trả về; task chạy vô hạn.
 */
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

/**
 * @brief Khởi tạo driver relay, queue và task điều khiển.
 * @param None.
 * @return ESP_OK nếu thành công; ESP_ERR_NO_MEM hoặc mã lỗi driver khi thất bại.
 */
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

/**
 * @brief Đưa lệnh relay vào queue để task xử lý bất đồng bộ.
 * @param eCommand Lệnh relay cần gửi.
 * @return ESP_OK nếu gửi thành công; mã lỗi nếu chưa khởi tạo, lệnh sai hoặc queue đầy.
 */
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