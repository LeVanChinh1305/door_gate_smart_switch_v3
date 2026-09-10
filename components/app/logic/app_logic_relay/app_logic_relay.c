/**
 * @file app_logic_relay.c
 * @brief Điều phối lệnh relay từ Application Layer bằng queue và task riêng, kết hợp khóa chéo an toàn.
 */

#include "app_logic_relay.h"
#include "app_relay_state.h"
#include "app_common.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "app_logic_mqtt_publisher.h"
#include "app_nvs.h"
#include "app_logic_telemetry.h"
#include "app_logic_extra_config.h"
#include "app_led_state.h"

static const char *TAG = "APP_LOGIC_RELAY";

static QueueHandle_t g_hRelayCommandQueue = NULL;
static TaskHandle_t g_hRelayTask = NULL;
static bool g_bIsReady = false;


/* Các biến quản lý vị trí cửa chuẩn MISRA C */
static uint8_t g_u8CurrentLevel = 0U;      // 0 = Đóng hoàn toàn, 100 = Mở hoàn toàn
static uint8_t g_u8TargetLevel = 0U;       // Mục tiêu muốn chạy đến
static int8_t  g_i8Direction = 0;          // Hướng di chuyển: 1 (Lên), -1 (Xuống), 0 (Dừng)





/**
 * @brief trả về giá trị thực tế hiện tại của của , đang ở bao nhiêu %
 */
uint8_t app_logic_relay_GetCurrentLevel(void) {
  return g_u8CurrentLevel;
}



/**
 * @brief Tính toán logic hiển thị nút bấm trên App và gửi báo cáo MQTT
 */
void app_logic_relay_UpdateAppUI(void) {
    uint8_t u8Gate1 = 0U; // Trạng thái báo cáo nút đóng 
    uint8_t u8Gate2 = 0U; // Trạng thái báo cáo nút STOP ()
    uint8_t u8Gate3 = 0U; // Trạng thái báo cáo nút mở 

    if (g_u8CurrentLevel == g_u8TargetLevel || g_i8Direction == 0) {
        /* 1. TRẠNG THÁI DỪNG (Thực tế đã bằng mong muốn) */
       
        u8Gate3 = 0U;
        u8Gate1 = 0U;
        u8Gate2 = 0U;
    } 
    else {
        /* 2. TRẠNG THÁI ĐANG DI CHUYỂN (Thực tế khác mong muốn) */        
        if (g_i8Direction == 1) { 
            /* Đang chạy lên (Mở): Tắt chân thuận (Mở), Sáng chân đối ngược (Đóng) */
            u8Gate1 = 0U; 
            u8Gate2 = 0U; 
            u8Gate3 = 1U; 
        }   
        else if (g_i8Direction == -1) {
            /* Đang chạy xuống (Đóng): Tắt chân thuận (Đóng), Sáng chân đối ngược (Mở) */
            u8Gate1 = 1U; 
            u8Gate2 = 0U; 
            u8Gate3 = 0U; 

        }else{
            
        }
    }

    /* Gọi API đẩy bản tin lên MQTT */
    (void)app_logic_mqtt_publisher_ReportGateData(u8Gate1, u8Gate2, u8Gate3, g_u8CurrentLevel);
}

/**
 * @brief Task chạy ngầm theo dõi hành trình cửa và báo cáo trạng thái sensor lên Cloud 
 * @note 
 */

static void app_logic_relay_TrackingTask(void *arg) {
    uint8_t u8LastReportLevel = 255U; 
    while (1) {
        if (g_i8Direction != 0) {
            uint32_t u32TimePerOnePercentMs = g_sExtraConfig.sgmCycle * 10U; 
            if (u32TimePerOnePercentMs == 0U) {
                u32TimePerOnePercentMs = 100U;
            }

            vTaskDelay(pdMS_TO_TICKS(u32TimePerOnePercentMs));

            /* Cập nhật mức thực tế an toàn chống tràn số âm */
            if (g_i8Direction == 1) {
                if (g_u8CurrentLevel < 100U) {
                    g_u8CurrentLevel++;
                }
            } else if (g_i8Direction == -1) {
                if (g_u8CurrentLevel > 0U) {
                    g_u8CurrentLevel--;
                }
            }

            /* 2. Kiểm tra xem đã đến đích hoặc chạm giới hạn chưa */
            if (g_u8CurrentLevel == g_u8TargetLevel || g_u8CurrentLevel == 0U || g_u8CurrentLevel == 100U) {
                app_logic_relay_Stop(); 
                g_i8Direction = 0;       
                g_u8TargetLevel = g_u8CurrentLevel; /* Ép đồng bộ chốt chặn */
                if((g_u8CurrentLevel == 0U) || (g_u8CurrentLevel == 100U)){
                    app_logic_sensor_history_item_t sSensorLog;
                    sSensorLog.i64Time = 0; // hàm report tự động gán rồi
                    sSensorLog.u8SensorState = (g_u8CurrentLevel == 100U) ? 1U : 0U; // 1 mở / 0 đóng
                    (void) app_logic_telemetry_ReportSensorHistory(&sSensorLog, 1); 
                }
                app_logic_relay_UpdateAppUI();
            }

            /* 3. Gọi hàm xử lý logic App và xuất báo cáo MQTT (Chỉ cần 1 dòng duy nhất) */
            if ((abs((int)g_u8CurrentLevel - (int)u8LastReportLevel) >= 5) || 
                (g_u8CurrentLevel == g_u8TargetLevel) || 
                (g_u8CurrentLevel == 0U) || 
                (g_u8CurrentLevel == 100U)) 
            {
                u8LastReportLevel = g_u8CurrentLevel;
                app_logic_relay_UpdateAppUI();
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(100U));
        }
    }
}

/**
 * @brief Task nhận và thực thi tuần tự các lệnh relay với logic bảo vệ an toàn.
 * @param pArg Tham số task (không sử dụng).
 */
static void app_logic_relay_Task(void *pArg)
{
    app_logic_relay_msg_t sMsg;
    (void)pArg;

    while (true) {
        if (xQueueReceive(g_hRelayCommandQueue, &sMsg, portMAX_DELAY) == pdPASS) {
            e_relay_state_t eCurrentState = app_relay_state_GetState();

            // Kiểm tra an toàn: Nếu đang bị khóa mà không phải cờ cưỡng chế (ForceOverride) thì bỏ qua
            if ((eCurrentState == E_RELAY_STATE_SAFETY_LOCKED) && (!sMsg.bForceOverride)) {
                ESP_LOGW(TAG, "Thiết bị đang bị khóa an toàn (SAFETY_LOCKED), từ chối thực thi lệnh: %d", sMsg.eCmd);
                continue;
            }

            esp_err_t eErr = ESP_OK;
            uint32_t u32PulseTime = (sMsg.u32PulseDurationMs > 0) ? sMsg.u32PulseDurationMs : DF_RELAY_DEFAULT_PULSE_DURATION_MS;

            switch (sMsg.eCmd) {
                case E_APP_LOGIC_RELAY_CMD_OPEN:
                    // Khóa chéo an toàn: Nếu đang đóng, phải dừng trước khi kích mở để tránh sốc dòng cơ khí
                    if (eCurrentState == E_RELAY_STATE_CLOSING) {
                        ESP_LOGI(TAG, "Đang đóng -> Tự động kích STOP trước khi đảo chiều mở...");
                        (void)app_relay_TriggerPulse(E_RELAY_CMD_STOP, u32PulseTime);
                        vTaskDelay(pdMS_TO_TICKS(DF_INTERLOCK_DELAY_MS));
                    }
                    (void)app_relay_state_SetState(E_RELAY_STATE_OPENING);
                    eErr = app_relay_TriggerPulse(E_RELAY_CMD_OPEN, u32PulseTime);
                    break;

                case E_APP_LOGIC_RELAY_CMD_CLOSE:
                    // Khóa chéo an toàn: Nếu đang mở, phải dừng trước khi kích đóng
                    if (eCurrentState == E_RELAY_STATE_OPENING) {
                        ESP_LOGI(TAG, "Đang mở -> Tự động kích STOP trước khi đảo chiều đóng...");
                        (void)app_relay_TriggerPulse(E_RELAY_CMD_STOP, u32PulseTime);
                        vTaskDelay(pdMS_TO_TICKS(DF_INTERLOCK_DELAY_MS));
                    }
                    (void)app_relay_state_SetState(E_RELAY_STATE_CLOSING);
                    eErr = app_relay_TriggerPulse(E_RELAY_CMD_CLOSE, u32PulseTime);
                    break;

                case E_APP_LOGIC_RELAY_CMD_STOP:
                    eErr = app_relay_TriggerPulse(E_RELAY_CMD_STOP, u32PulseTime);
                    (void)app_relay_state_SetState(E_RELAY_STATE_STOPPED);
                    break;

                case E_APP_LOGIC_RELAY_CMD_EMERGENCY_STOP:
                    eErr = app_relay_EmergencyStop();
                    (void)app_relay_state_SetState(E_RELAY_STATE_STOPPED);
                    break;

                case E_APP_LOGIC_RELAY_CMD_ALL_OFF:
                    eErr = app_relay_ExecuteCmd(E_RELAY_CMD_ALL_OFF);
                    (void)app_relay_state_SetState(E_RELAY_STATE_STOPPED);
                    break;

                default:
                    ESP_LOGW(TAG, "Mã lệnh relay không hợp lệ: %d", sMsg.eCmd);
                    eErr = ESP_ERR_INVALID_ARG;
                    break;
            }

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

    g_hRelayCommandQueue = xQueueCreate(DF_QUEUE_LENGTH_MEDIUM, sizeof(app_logic_relay_msg_t));
    if (g_hRelayCommandQueue == NULL) {
        ESP_LOGE(TAG, "Tạo hàng đợi lệnh relay thất bại");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t xTaskResult = xTaskCreate(app_logic_relay_Task,
                                         "relay_logic",
                                         DF_TASK_STACK_MEDIUM,
                                         NULL,
                                         DF_TASK_PRIO_CRITICAL,
                                         &g_hRelayTask);
    if (xTaskResult != pdPASS) {
        ESP_LOGE(TAG, "Tạo task xử lý relay thất bại");
        vQueueDelete(g_hRelayCommandQueue);
        g_hRelayCommandQueue = NULL;
        return ESP_ERR_NO_MEM;
    }
    xTaskResult = xTaskCreate(app_logic_relay_TrackingTask,
                                         "relay_tracking",
                                         DF_TASK_STACK_NETWORK, 
                                         NULL,
                                         DF_TASK_PRIO_NORMAL,
                                         NULL);
    if (xTaskResult != pdPASS) {
        ESP_LOGE(TAG, "Tạo task tracking relay thất bại");
        vQueueDelete(g_hRelayCommandQueue);
        vTaskDelete(g_hRelayTask);
        g_hRelayCommandQueue = NULL;
        return ESP_ERR_NO_MEM;
    }

    g_bIsReady = true;
    ESP_LOGI(TAG, "Khởi tạo module app_logic_relay thành công");
    return ESP_OK;
}

esp_err_t app_logic_relay_SendMsg(const app_logic_relay_msg_t *pMsg)
{
  if ((!g_bIsReady) || (g_hRelayCommandQueue == NULL) || (g_hRelayTask == NULL)) {
    return ESP_ERR_INVALID_STATE;
  }

  if (pMsg == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  return (xQueueSend(g_hRelayCommandQueue, pMsg, 0U) == pdPASS) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t app_logic_relay_Open(void)
{
    app_logic_relay_msg_t sMsg = {
        .eCmd = E_APP_LOGIC_RELAY_CMD_OPEN,
        .u32PulseDurationMs = 0U,
        .bForceOverride = false
    };
    g_u8TargetLevel = 100U;  
    g_i8Direction = 1;
    /* KIỂM TRA TÍNH NĂNG CẢNH BÁO BAN ĐÊM (Áp dụng cho mọi nguồn Mở cửa) */
    if (app_logic_extra_config_IsWarningNightActive()) {
        ESP_LOGW(TAG, ">>> CẢNH BÁO BAN ĐÊM: Phát hiện cửa mở trong khung giờ bảo vệ!");

        /* 1. Báo động còi & Nháy LED đỏ tại chỗ */
        // app_logic_buzzer_BeepWarning();
        app_led_state_SetState(E_LED_STATE_WARNING);

        /* 2. Gửi bản tin ReportWarningSgm (sensor=1) lên Topic Alert (mqtt_alert) */
        (void)app_logic_telemetry_ReportWarningSgm(1, 0, 0, 14, NULL);
    }
    return app_logic_relay_SendMsg(&sMsg);
}

esp_err_t app_logic_relay_Close(void)
{
    app_logic_relay_msg_t sMsg = {
        .eCmd = E_APP_LOGIC_RELAY_CMD_CLOSE,
        .u32PulseDurationMs = 0U,
        .bForceOverride = false
    };
    g_u8TargetLevel = 0U;    
    g_i8Direction = -1;
    return app_logic_relay_SendMsg(&sMsg);
    }

esp_err_t app_logic_relay_Stop(void)
{
    app_logic_relay_msg_t sMsg = {
        .eCmd = E_APP_LOGIC_RELAY_CMD_STOP,
        .u32PulseDurationMs = 0U,
        .bForceOverride = false
    };
    g_u8TargetLevel = g_u8CurrentLevel; 
    g_i8Direction = 0;
    return app_logic_relay_SendMsg(&sMsg);
}

esp_err_t app_logic_relay_EmergencyStop(void)
{
    app_logic_relay_msg_t sMsg = {
        .eCmd = E_APP_LOGIC_RELAY_CMD_EMERGENCY_STOP,
        .u32PulseDurationMs = 0U,
        .bForceOverride = true
    };
    g_u8TargetLevel = g_u8CurrentLevel; 
    g_i8Direction = 0;
    return app_logic_relay_SendMsg(&sMsg);
}

esp_err_t app_logic_relay_SendCommand(e_app_relay_cmd_t eCommand)
{
    app_logic_relay_msg_t sMsg = {
        .u32PulseDurationMs = 0U,
        .bForceOverride = false
    };

    switch (eCommand) {
        case E_RELAY_CMD_OPEN:
            sMsg.eCmd = E_APP_LOGIC_RELAY_CMD_OPEN;
            break;
        case E_RELAY_CMD_CLOSE:
            sMsg.eCmd = E_APP_LOGIC_RELAY_CMD_CLOSE;
            break;
        case E_RELAY_CMD_STOP:
            sMsg.eCmd = E_APP_LOGIC_RELAY_CMD_STOP;
            break;
        case E_RELAY_CMD_ALL_OFF:
            sMsg.eCmd = E_APP_LOGIC_RELAY_CMD_ALL_OFF;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    return app_logic_relay_SendMsg(&sMsg);
}


esp_err_t app_logic_relay_SetLevel(uint8_t u8Level) {
  if (u8Level > 100U) {
    u8Level = 100U;
  }

  if (u8Level > g_u8CurrentLevel) {
    app_logic_relay_Open(); 
    g_u8TargetLevel = u8Level; 
  }else if (u8Level < g_u8CurrentLevel) {
    app_logic_relay_Close();
    g_u8TargetLevel = u8Level; 
  }
  return ESP_OK; 
}