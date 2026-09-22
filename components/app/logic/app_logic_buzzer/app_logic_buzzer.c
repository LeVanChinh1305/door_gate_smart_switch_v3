/**
 * @file app_logic_buzzer.c
 * @brief Điều phối hoạt động buzzer bằng queue và task TĨNH (Static Task) để tối ưu Heap.
 */

#include "app_logic_buzzer.h"

#include "app_buzzer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "APP_LOGIC_BUZZER";

#define DF_APP_LOGIC_BUZZER_QUEUE_LENGTH  (8U)
/* Tối ưu Stack: Giảm từ 3072 xuống 1536 Bytes (đủ an toàn cho driver GPIO + Delay) */
#define DF_APP_LOGIC_BUZZER_TASK_STACK    (1536U) 
#define DF_APP_LOGIC_BUZZER_TASK_PRIORITY (5U)

typedef enum {
    E_APP_LOGIC_BUZZER_CMD_ON = 0,
    E_APP_LOGIC_BUZZER_CMD_OFF,
    E_APP_LOGIC_BUZZER_CMD_BEEP,
    E_APP_LOGIC_BUZZER_CMD_BEEP_REPEAT
} e_app_logic_buzzer_cmd_t;

typedef struct {
    e_app_logic_buzzer_cmd_t eType;
    uint32_t u32DurationMs;
    uint32_t u32DelayMs;
    uint8_t u8RepeatCount;
} app_logic_buzzer_queue_item_t;

static QueueHandle_t g_hBuzzerCommandQueue = NULL;
static TaskHandle_t g_hBuzzerTask = NULL;
static bool g_bIsReady = false;

/* ==================== CẤP PHÁT BỘ NHỚ TĨNH CHO TASK ==================== */
static StackType_t s_au8BuzzerTaskStack[DF_APP_LOGIC_BUZZER_TASK_STACK];
static StaticTask_t s_sBuzzerTaskTCB;

/**
 * @brief Task nhận và thực thi tuần tự các lệnh buzzer.
 * @param pArg Tham số task, hiện không sử dụng.
 * @return Không trả về; task chạy vô hạn.
 */
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
            } else if (sItem.eType == E_APP_LOGIC_BUZZER_CMD_BEEP_REPEAT) {
                eErr = ESP_OK;
                for (uint8_t i = 0; i < sItem.u8RepeatCount; i++) {
                    eErr = app_buzzer_Beep(sItem.u32DurationMs);
                    if (eErr != ESP_OK) {
                        break;
                    }
                    if (i + 1 < sItem.u8RepeatCount && sItem.u32DelayMs > 0) {
                        vTaskDelay(pdMS_TO_TICKS(sItem.u32DelayMs));
                    }
                }
            }
            if (eErr != ESP_OK) {
                ESP_LOGE(TAG, "Xử lý lệnh buzzer thất bại: %s", esp_err_to_name(eErr));
            }
        }
    }
}

/**
 * @brief Đưa một lệnh buzzer vào queue.
 * @param pItem Con trỏ đến dữ liệu lệnh cần gửi.
 * @return ESP_OK nếu gửi thành công; mã lỗi nếu driver chưa sẵn sàng hoặc queue đầy.
 */
static esp_err_t app_logic_buzzer_SendItem(const app_logic_buzzer_queue_item_t *pItem)
{
    if (!g_bIsReady || g_hBuzzerCommandQueue == NULL || g_hBuzzerTask == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueSend(g_hBuzzerCommandQueue, pItem, 0U) == pdPASS
               ? ESP_OK : ESP_ERR_TIMEOUT;
}

/**
 * @brief Khởi tạo driver buzzer, queue và task TĨNH điều khiển.
 * @return ESP_OK nếu thành công; mã lỗi nếu khởi tạo thất bại.
 */
esp_err_t app_logic_buzzer_Init(void)
{
    if (g_bIsReady) {
        return ESP_OK;
    }
    esp_err_t eErr = app_buzzer_Init();
    if (eErr != ESP_OK) {
        return eErr;
    }
    g_hBuzzerCommandQueue = xQueueCreate(DF_APP_LOGIC_BUZZER_QUEUE_LENGTH, sizeof(app_logic_buzzer_queue_item_t));
    if (g_hBuzzerCommandQueue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* Tạo Task Tĩnh (Static Task) - Không chiếm 1 byte Free Heap nào */
    g_hBuzzerTask = xTaskCreateStatic(
        app_logic_buzzer_Task,
        "buzzer_logic",
        DF_APP_LOGIC_BUZZER_TASK_STACK,
        NULL,
        DF_APP_LOGIC_BUZZER_TASK_PRIORITY,
        s_au8BuzzerTaskStack,
        &s_sBuzzerTaskTCB
    );

    if (g_hBuzzerTask == NULL) {
        vQueueDelete(g_hBuzzerCommandQueue);
        g_hBuzzerCommandQueue = NULL;
        return ESP_ERR_NO_MEM;
    }

    g_bIsReady = true;
    ESP_LOGI(TAG, "Khởi tạo app_logic_buzzer (Static Task) thành công");
    return ESP_OK;
}

/**
 * @brief Gửi lệnh bật buzzer.
 */
esp_err_t app_logic_buzzer_On(void)
{
    app_logic_buzzer_queue_item_t sItem = {.eType = E_APP_LOGIC_BUZZER_CMD_ON};
    return app_logic_buzzer_SendItem(&sItem);
}

/**
 * @brief Gửi lệnh tắt buzzer.
 */
esp_err_t app_logic_buzzer_Off(void)
{
    app_logic_buzzer_queue_item_t sItem = {.eType = E_APP_LOGIC_BUZZER_CMD_OFF};
    return app_logic_buzzer_SendItem(&sItem);
}

/**
 * @brief Gửi lệnh phát beep trong khoảng thời gian chỉ định.
 */
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

/**
 * @brief Gửi lệnh phát lặp lại tiếng beep.
 */
esp_err_t app_logic_buzzer_BeepRepeat(uint32_t u32DurationMs, uint32_t u32DelayMs, uint8_t u8RepeatCount)
{
    if (u32DurationMs == 0U || u8RepeatCount == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    app_logic_buzzer_queue_item_t sItem = {
        .eType = E_APP_LOGIC_BUZZER_CMD_BEEP_REPEAT,
        .u32DurationMs = u32DurationMs,
        .u32DelayMs = u32DelayMs,
        .u8RepeatCount = u8RepeatCount
    };
    return app_logic_buzzer_SendItem(&sItem);
}