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
#include "app_logic_extra_config.h"
#include "esp_task_wdt.h"


static const char *TAG = "APP_LOGIC_TOUCH";

static QueueHandle_t g_hTouchCommandQueue = NULL;
static TaskHandle_t g_hTouchTask = NULL;
static bool g_bIsReady = false;
/* Cờ bảo vệ: ngăn tạo reconfig task nhiều lần khi người dùng giữ nút liên tục */
static volatile bool s_bReconfigTaskPending = false;


#define DF_TOUCH_POLL_PERIOD_MS (50U)   // Thời gian quét 50ms một lần
#define DF_TOUCH_HOLD_3S_MS     (3000U) // Ngưỡng giữ 3 giây (BluFi)
#define DF_TOUCH_HOLD_7S_MS     (7000U) // Ngưỡng giữ 7 giây (Manual)
#define DF_CANCEL_HOLD_MS       (800U)  /* Giữ tối thiểu 800ms mới coi là chủ ý hủy, chống nhiễu/bounce */
#define DF_CANCEL_POLL_PERIOD_MS (50U) 

static void app_logic_touch_DelayedReconfigRestartTask(void *pvParameters)
{
    uint8_t u8Flag = (uint8_t)(uintptr_t)pvParameters;
    /* Cho LED/buzzer kịp phản hồi trước khi mất nguồn Bluetooth/Wifi cũ */
    vTaskDelay(pdMS_TO_TICKS(300));
    (void)app_nvs_SaveReconfigFlag(u8Flag);
    ESP_LOGI(TAG, "Ghi cờ reconfig=%u -> Khởi động lại...", (unsigned)u8Flag);
    esp_restart();
}

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

    /* Đăng ký task cảm ứng vào Task Watchdog Timer */
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    while (true) {
        /* Định kỳ feed Watchdog mỗi chu kỳ 50ms */
        esp_task_wdt_reset();

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
                        ESP_LOGI(TAG, ">>> Giữ > 7s -> Xử lý kết nối thủ công (UDP), reboot...");
                        app_led_state_SetState(E_LED_STATE_CONNECT_MANUAL);
                        if (!s_bReconfigTaskPending) {
                            s_bReconfigTaskPending = true;
                            xTaskCreate(app_logic_touch_DelayedReconfigRestartTask, "reconf_udp", DF_TASK_STACK_SMALL, (void *)(uintptr_t)E_APP_RECONFIG_UDP, 5, NULL);
                        }
                    }
                    if (u32HeldMs >= 5000U) {
                        ESP_LOGI(TAG, ">>> Giữ phím 5s -> Đổi LED nháy vàng, ghi cờ BLE MESH và RESTART thiết bị...");
                        
                        /* 1. Đặt LED nháy vàng chỉ báo Pairing */
                        app_led_state_SetState(E_LED_STATE_CONNECT_BLE_MESH);
                        
                        /* 2. Tạo task delay nhỏ rồi ghi cờ & esp_restart() */
                        if (!s_bReconfigTaskPending) {
                            s_bReconfigTaskPending = true;
                            xTaskCreate(app_logic_touch_DelayedReconfigRestartTask, "reconf_mesh", DF_TASK_STACK_SMALL, (void *)(uintptr_t)E_APP_RECONFIG_BLE_MESH, 5, NULL);
                        }
                    }
                    else if (u32HeldMs >= DF_TOUCH_HOLD_3S_MS) {
                        // th1: giữ đồng thời 2 nút đóng + mở 
                        if ((u8PressedBtn & (DF_TOUCH_BTN_CS5 | DF_TOUCH_BTN_CS7)) == (DF_TOUCH_BTN_CS5 | DF_TOUCH_BTN_CS7)) {
                            ESP_LOGI(TAG, ">>> Phát hiện giữ 2 nút Đóng/Mở 3s -> MỞ KHÓA TẠM THỜI!");
                            
                            /* Mở khóa và bắt đầu đếm ngược N giây để khóa lại */
                            app_logic_extra_config_StartAntiAnimalWindow();
                            // app_logic_buzzer_Beep(200);
                        }

                        // th2: giữ 1 nút đơn 3 giây
                        else {
                            ESP_LOGI(TAG, ">>> Giữ 3-7s -> Xử lý kết nối tự động BluFi, reboot...");
                            app_led_state_SetState(E_LED_STATE_BLUFI_AUTO);
                            if (!s_bReconfigTaskPending) {
                                s_bReconfigTaskPending = true;
                                xTaskCreate(app_logic_touch_DelayedReconfigRestartTask, "reconf_blufi", DF_TASK_STACK_SMALL, (void *)(uintptr_t)E_APP_RECONFIG_BLUFI, 5, NULL);
                            }
                        }
                    } 
                    else {
                        
                        /* Nếu đang ở chế độ UNCONNECTED hoặc NORMAL_IDLE (chờ cấu hình / chưa sẵn sàng) thì bỏ qua nút bấm đơn ngắn */
                        e_led_device_state_t eLedState = app_led_state_GetState();
                        if (eLedState == E_LED_STATE_UNCONNECTED || eLedState == E_LED_STATE_NORMAL_IDLE) {
                            ESP_LOGW(TAG, "Thiết bị đang ở trạng thái chưa cấu hình / chờ kết nối (LED state: %d)! Bỏ qua lệnh bấm nút (0x%02X)", (int)eLedState, u8PressedBtn);
                            u8PrevStatus = u8CurrentStatus;
                            continue;
                        }

                        if (app_device_state_HasMode(DEVICE_MODE_LOCKED_RF) || app_device_state_HasMode(DEVICE_MODE_LOCKED_TEMP)||app_device_state_HasMode(DEVICE_MODE_LOCKED_CHILD)) {
                            ESP_LOGW(TAG, "Thiết bị đang trong khung giờ KHÓA NGOẠI VI! Bỏ qua lệnh bấm nút (0x%02X)", u8PressedBtn);
                            
                            /* (Tùy chọn) Kêu còi báo hiệu từ chối thao tác */
                            // app_logic_buzzer_Beep(100);
                            
                            /* Kết thúc ngay nhánh Short Press, không kích hoạt Relay hay gửi Telemetry */
                            u8PrevStatus = u8CurrentStatus;
                            continue;
                        }
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
                            }
                        } else if ((u8PressedBtn & DF_TOUCH_BTN_CS7 ) != 0U) {
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
                        // Chỉ đặt lịch khóa tạm thời khi thiết bị đã có mạng 
                        if (!app_device_state_HasMode(DEVICE_MODE_UNCONNECTED)) {
                            app_logic_extra_config_StartAntiAnimalWindow();
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

    if (xTaskCreate(app_logic_touch_Task, "touch_logic", DF_TASK_STACK_NETWORK, NULL, DF_TASK_PRIO_NORMAL, &g_hTouchTask) != pdPASS) {
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

static void app_logic_touch_CancelConfigTask(void *pArg)
{
    (void)pArg;
    uint8_t u8PrevStatus = 0U;
    uint8_t u8CurrentStatus = 0U;
    bool bIsPressed = false;
    TickType_t xPressStartTick = 0;

    ESP_LOGI(TAG, "Bắt đầu task cảm ứng RÚT GỌN (chế độ config) - chờ nút hủy...");
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    while (true) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(DF_CANCEL_POLL_PERIOD_MS));

        u8CurrentStatus = 0U;
        if (app_touch_ReadButtonStatus(&u8CurrentStatus) != ESP_OK) {
            continue;
        }

        /* Cạnh lên: bắt đầu bấm */
        if ((u8CurrentStatus != 0U) && (u8PrevStatus == 0U) && !bIsPressed) {
            bIsPressed = true;
            xPressStartTick = xTaskGetTickCount();
        }

        /* Đang giữ, kiểm tra đã đủ thời gian chưa (không cần chờ nhả nút) */
        if (bIsPressed && (u8CurrentStatus != 0U)) {
            uint32_t u32HeldMs = (uint32_t)((xTaskGetTickCount() - xPressStartTick) * portTICK_PERIOD_MS);
            if (u32HeldMs >= DF_CANCEL_HOLD_MS) {
                ESP_LOGW(TAG, ">>> Nút vật lý giữ đủ lâu -> HỦY cấu hình, khởi động lại...");
                (void)app_nvs_SaveReconfigFlag(E_APP_RECONFIG_NONE);
                /* cho log kịp flush */
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_restart();
            }
        }

        /* Cạnh xuống: nhả nút trước khi đủ thời gian -> hủy trạng thái đang bấm */
        if (bIsPressed && (u8CurrentStatus == 0U)) {
            bIsPressed = false;
        }

        u8PrevStatus = u8CurrentStatus;
    }
}

esp_err_t app_logic_touch_InitCancelConfigMode(void)
{
    /* Vẫn cần init driver CY8CMBR3108 để đọc được nút */
    esp_err_t eErr = app_touch_Init();
    if (eErr != ESP_OK) {
        return eErr;
    }

    if (xTaskCreate(app_logic_touch_CancelConfigTask, "touch_cancel_cfg", DF_TASK_STACK_NETWORK, NULL, DF_TASK_PRIO_NORMAL, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}