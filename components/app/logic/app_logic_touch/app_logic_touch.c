/**
 * @file app_logic_touch.c
 * @brief Điều phối các thao tác cảm ứng bằng queue và task Application Layer.
 */

#include "app_logic_touch.h"

#include "app_touch.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "app_common.h"
#include "app_logic_relay.h"
#include "app_led_state.h"
#include "app_device_state.h"
#include "app_blufi.h"
#include "app_logic_telemetry.h"
#include "app_nvs.h"
#include "app_logic_mqtt_publisher.h"
#include "app_mqtt.h"

static const char *TAG = "APP_LOGIC_TOUCH";

static QueueHandle_t g_hTouchCommandQueue = NULL;
static TaskHandle_t g_hTouchTask = NULL;
static bool g_bIsReady = false;


#define DF_TOUCH_POLL_PERIOD_MS (50U)   // Thời gian quét 50ms một lần
#define DF_TOUCH_HOLD_3S_MS     (3000U) // Ngưỡng giữ 3 giây (BluFi)
#define DF_TOUCH_HOLD_7S_MS     (7000U) // Ngưỡng giữ 7 giây (Manual)
/**
 * @brief Task nhận và xử lý các lệnh cảm ứng.
 * @param pArg Tham số task, hiện không sử dụng.
 * @return Không trả về; task chạy vô hạn.
 */
static void app_logic_touch_Task(void *pArg)
{
    e_app_logic_touch_cmd_t eCommand;
    (void)pArg;

    uint8_t u8PrevStatus = 0U;
    uint8_t u8CurrentStatus = 0U;
    bool bIsPressed = false;
    TickType_t xPressStartTick = 0;
    uint8_t u8PressedBtn = 0U;
    uint8_t u8RisingEdge = 0U;

    ESP_LOGI(TAG, "Bắt đầu vòng lặp quét (Polling) Cảm ứng...");

    while (true) {
        /* 1. Nhận lệnh từ Queue với timeout chính là chu kỳ quét (50ms) */
        if (xQueueReceive(g_hTouchCommandQueue, &eCommand, pdMS_TO_TICKS(DF_TOUCH_POLL_PERIOD_MS)) == pdPASS) {
            /* Xử lý các lệnh từ bên ngoài (nếu có) */
            if (eCommand == E_APP_LOGIC_TOUCH_CMD_SOFT_RESET) {
                (void)app_touch_SoftReset();
            } else if (eCommand == E_APP_LOGIC_TOUCH_CMD_CLEAR_LATCHED) {
                (void)app_touch_ClearLatched();
            }
        } 
        else {
            /* 2. Timeout xảy ra -> Tới lúc đi quét cảm ứng */
            u8CurrentStatus = 0U;
            if (app_touch_ReadButtonStatus(&u8CurrentStatus) == ESP_OK) {
                
                /* Phát hiện cạnh lên (Vừa bấm xuống) */
                u8RisingEdge = (uint8_t)(u8CurrentStatus & (~u8PrevStatus));

                if ((u8RisingEdge != 0U) && (!bIsPressed)) {
                    bIsPressed = true;
                    xPressStartTick = xTaskGetTickCount();
                    u8PressedBtn = u8RisingEdge;
                    ESP_LOGI(TAG, "Nút được bấm: 0x%02X", u8PressedBtn);
                    
                    /* TODO: Bạn có thể thêm code đổi màu LED tại đây */
                }

                /* Xử lý sự kiện Nhả nút (Cạnh xuống) */
                if (bIsPressed && ((u8CurrentStatus & u8PressedBtn) == 0U)) {
                    uint32_t u32HeldMs = (uint32_t)((xTaskGetTickCount() - xPressStartTick) * portTICK_PERIOD_MS);
                    bIsPressed = false;

                    if (u32HeldMs >= DF_TOUCH_HOLD_7S_MS) {
                        ESP_LOGI(TAG, ">>> Giữ > 7s -> Xử lý kết nối thủ công");
                        /* TODO: Gọi hàm Config Manual */

                    } 
                    else if (u32HeldMs >= DF_TOUCH_HOLD_3S_MS) {
                        ESP_LOGI(TAG, ">>> Giữ 3-7s -> Xử lý kết nối tự động bằng blufi");
                        set_current_door_mode(DEVICE_MODE_CONNECT_AUTO);
                        app_led_state_SetState(E_LED_STATE_BLUFI_AUTO);
                        (void)app_blufi_Init();
                    } 
                    else {
                        const app_nvs_device_config_t *psConfig = app_mqtt_GetDeviceConfig();
                        app_logic_control_history_item_t sLog;
                        (void)memset(&sLog, 0, sizeof(app_logic_control_history_item_t));
                        /* Nhấn nhả bình thường (Short Press) -> Gọi Relay */
                        if ((u8PressedBtn & DF_TOUCH_BTN_CS5) != 0U) {
                            ESP_LOGI(TAG, "→ Lệnh: MỞ (CS5)");
                            app_logic_relay_Open();
                            (void)app_led_state_SetState(E_LED_STATE_GATE_UP);
                            if (psConfig != NULL) {
                                app_logic_telemetry_BuildControlItem(&sLog, "gate_3", E_TELEMETRY_MODE_OPEN, E_TELEMETRY_SRC_PHYSICAL_DEVICE, psConfig->dev_ext_addr, "");
                                (void)app_logic_telemetry_ReportControlHistory(&sLog, 1);
                            }
                        } else if ((u8PressedBtn & DF_TOUCH_BTN_CS6) != 0U) {
                            ESP_LOGI(TAG, "→ Lệnh: DỪNG (CS6)");
                            app_logic_relay_Stop();
                            (void)app_led_state_SetState(E_LED_STATE_GATE_STOP);
                            if (psConfig != NULL) {
                                uint8_t u8StopLevel = app_logic_relay_GetCurrentLevel();
                                app_logic_telemetry_BuildControlItem(&sLog, "gate_2", E_TELEMETRY_MODE_STOP, E_TELEMETRY_SRC_PHYSICAL_DEVICE, psConfig->dev_ext_addr, "");
                                sLog.i32Value = (int32_t)u8StopLevel;
                                (void)app_logic_telemetry_ReportControlHistory(&sLog, 1);

                                /* Cập nhật UI thanh trượt trên App về mốc dừng thực tế */
                                // (void)app_logic_mqtt_publisher_ReportGateData(0, 1, 0, u8StopLevel);
                            }
                        } else if ((u8PressedBtn & DF_TOUCH_BTN_CS7) != 0U) {
                            ESP_LOGI(TAG, "→ Lệnh: ĐÓNG (CS7)");
                            app_logic_relay_Close();
                            (void)app_led_state_SetState(E_LED_STATE_GATE_DOWN);
                            if (psConfig != NULL) {
                                app_logic_telemetry_BuildControlItem(&sLog, "gate_1", E_TELEMETRY_MODE_CLOSE, E_TELEMETRY_SRC_PHYSICAL_DEVICE, psConfig->dev_ext_addr, "");
                                (void)app_logic_telemetry_ReportControlHistory(&sLog, 1);
                            }
                        } else {
                            ESP_LOGW(TAG, "Nhấn nhả nút không xác định: 0x%02X", u8PressedBtn);
                        }
                    }
                }

                u8PrevStatus = u8CurrentStatus;
            }
        }
    }
}

/**
 * @brief Khởi tạo driver cảm ứng, queue và task điều khiển.
 * @param None.
 * @return ESP_OK nếu thành công; mã lỗi nếu khởi tạo thất bại.
 */
esp_err_t app_logic_touch_Init(void)
{
    if (g_bIsReady) {
        return ESP_OK;
    }

    esp_err_t eErr = app_touch_Init();
    if (eErr != ESP_OK) {
        return eErr;
    }

    g_hTouchCommandQueue = xQueueCreate(DF_QUEUE_LENGTH_MEDIUM, sizeof(e_app_logic_touch_cmd_t));
    if (g_hTouchCommandQueue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(app_logic_touch_Task, "touch_logic",DF_TASK_STACK_MEDIUM, NULL,DF_TASK_PRIO_NORMAL, &g_hTouchTask) != pdPASS) {
        vQueueDelete(g_hTouchCommandQueue);
        g_hTouchCommandQueue = NULL;
        return ESP_ERR_NO_MEM;
    }

    g_bIsReady = true;
    return ESP_OK;
}

/**
 * @brief Đưa lệnh cảm ứng vào queue để task xử lý bất đồng bộ.
 * @param eCommand Lệnh cảm ứng cần gửi.
 * @return ESP_OK nếu gửi thành công; mã lỗi nếu tham số hoặc queue không hợp lệ.
 */
esp_err_t app_logic_touch_SendCommand(e_app_logic_touch_cmd_t eCommand)
{
    if (!g_bIsReady || g_hTouchCommandQueue == NULL || g_hTouchTask == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (eCommand > E_APP_LOGIC_TOUCH_CMD_SOFT_RESET) {
        return ESP_ERR_INVALID_ARG;
    }
    return xQueueSend(g_hTouchCommandQueue, &eCommand, 0U) == pdPASS ? ESP_OK : ESP_ERR_TIMEOUT;
}