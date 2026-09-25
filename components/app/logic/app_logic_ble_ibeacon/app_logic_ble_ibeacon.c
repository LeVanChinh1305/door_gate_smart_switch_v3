/**
 * @file    app_logic_ble_ibeacon.c
 * @brief   Hiện thực logic nhận tin từ queue, giải mã AES-256 và điều khiển relay
 */

#include "app_logic_ble_ibeacon.h"
#include "app_ble_ibeacon.h"
#include "app_logic_relay.h"
#include "app_nvs.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "mbedtls/aes.h"
#include <string.h>
#include <time.h>
#include "app_led_state.h"
#include "app_logic_telemetry.h"
#include "app_common.h"

static const char *TAG = "APP_LOGIC_BLE_IBEACON";

/* Hằng số cấu hình theo chuẩn DF_ */
#define DF_LOGIC_IBEACON_QUEUE_SIZE          (4U)
#define DF_TIMESTAMP_MAX_ALLOWED_DELTA_MS    (10000ULL) /* Ngưỡng lệch thời gian tối đa: 10 giây */
#define DF_IBEACON_DEFAULT_SECRET_KEY        "VCONNEX_SMART_GATE_SECRET_KEY_32"
#define DF_IBEACON_SECRET_KEY_LEN            (32U)
#define DF_IBEACON_AES_KEY_BITS              (256U)
#define DF_IBEACON_SNTP_MIN_VALID_TIME_SEC   (1600000000ULL)

/* Định nghĩa mã lệnh điều khiển iBeacon */
#define DF_IBEACON_CMD_CLOSE                 (1U)
#define DF_IBEACON_CMD_STOP                  (2U)
#define DF_IBEACON_CMD_OPEN                  (3U)
#define DF_IBEACON_CMD_SET_LEVEL             (4U)
#define DF_IBEACON_CMD_VENT_GAP              (5U)

/* Cấu trúc payload sau giải mã 16 bytes từ App */
typedef struct __attribute__((packed)) {
    uint64_t u64TimestampMs;                    /*!< 8 bytes: Timestamp dạng Milliseconds (Big Endian) */
    uint8_t  au8DeviceMac[DF_IBEACON_MAC_LEN];  /*!< 6 bytes: MAC của thiết bị ESP32 */
    uint8_t  u8Command;                          /*!< 1 byte : Mã lệnh điều khiển */
    uint8_t  u8Value;                            /*!< 1 byte : Giá trị điều khiển theo % (0 - 100%) */
} app_logic_ble_ibeacon_payload_t;

/* Biến toàn cục module tuân theo g_ + kiểu biến */
static bool          g_bTaskRunning   = false;
static QueueHandle_t g_xIbeaconQueue  = NULL;
static TaskHandle_t  g_xTaskHandle    = NULL;

/* Khai báo nguyên mẫu hàm nội bộ */
static uint64_t app_logic_ble_ibeacon_Ntoh64(uint64_t u64Val);
static void     app_logic_ble_ibeacon_DecryptAndDispatch(const app_ble_ibeacon_msg_t *pxMsg);
static void     app_logic_ble_ibeacon_Task(void *pvParameters);

/**
 * @brief Đảo ngược byte order từ Big Endian (Network) sang Little Endian (MCU) cho uint64_t
 */
static uint64_t app_logic_ble_ibeacon_Ntoh64(uint64_t u64Val)
{
    return ((u64Val & 0x00000000000000FFULL) << 56) |
           ((u64Val & 0x000000000000FF00ULL) << 40) |
           ((u64Val & 0x0000000000FF0000ULL) << 24) |
           ((u64Val & 0x00000000FF000000ULL) << 8)  |
           ((u64Val & 0x000000FF00000000ULL) >> 8)  |
           ((u64Val & 0x0000FF0000000000ULL) >> 24) |
           ((u64Val & 0x00FF000000000000ULL) >> 40) |
           ((u64Val & 0xFF00000000000000ULL) >> 56);
}

/**
 * @brief Giải mã AES-256-CBC và Thực thi lệnh Relay Local
 */
static void app_logic_ble_ibeacon_DecryptAndDispatch(const app_ble_ibeacon_msg_t *pxMsg)
{
    if (pxMsg == NULL) {
        return;
    }

    char acSecretKey[DF_IBEACON_SECRET_KEY_LEN + 1U] = {0};
    app_nvs_device_config_t sNvsCfg;

    /* 1. Nạp Secret Key 32 bytes */
    if ((app_nvs_LoadDeviceConfig(&sNvsCfg) == ESP_OK) && 
        (strlen(sNvsCfg.api_secret_key) >= DF_IBEACON_SECRET_KEY_LEN)) {
        memcpy(acSecretKey, sNvsCfg.api_secret_key, DF_IBEACON_SECRET_KEY_LEN);
        acSecretKey[DF_IBEACON_SECRET_KEY_LEN] = '\0';
    } else {
        const char *pcDefaultKey = DF_IBEACON_DEFAULT_SECRET_KEY;
        memcpy(acSecretKey, pcDefaultKey, DF_IBEACON_SECRET_KEY_LEN);
        acSecretKey[DF_IBEACON_SECRET_KEY_LEN] = '\0';
    }

    /* 2. Tạo Vector IV (16 bytes đầu của Secret Key) */
    uint8_t au8Iv[DF_IBEACON_UUID_LEN];
    memcpy(au8Iv, acSecretKey, sizeof(au8Iv));

    /* 3. Giải mã AES-256-CBC */
    app_logic_ble_ibeacon_payload_t sPayload;
    mbedtls_aes_context sAes;
    mbedtls_aes_init(&sAes);

    int iMbedResult = mbedtls_aes_setkey_dec(&sAes, (const unsigned char *)acSecretKey, DF_IBEACON_AES_KEY_BITS);
    if (iMbedResult == 0) {
        iMbedResult = mbedtls_aes_crypt_cbc(&sAes, MBEDTLS_AES_DECRYPT, (size_t)DF_IBEACON_UUID_LEN,
                                            au8Iv, pxMsg->au8Uuid, (uint8_t *)&sPayload);
    }
    mbedtls_aes_free(&sAes);

    if (iMbedResult != 0) {
        ESP_LOGE(TAG, "Giải mã AES-256-CBC thất bại: %d", iMbedResult);
        return;
    }

    /* 4. Kiểm tra Lệch thời gian (Timestamp Expiry) - Chống Replay Attack */
    uint64_t u64MsgTimeMs = app_logic_ble_ibeacon_Ntoh64(sPayload.u64TimestampMs);
    time_t xNowSec = time(NULL);
    if ((uint64_t)xNowSec > DF_IBEACON_SNTP_MIN_VALID_TIME_SEC) { /* Chỉ kiểm tra nếu hệ thống đã đồng bộ giờ SNTP */
        uint64_t u64CurrentTimeMs = (uint64_t)xNowSec * 1000ULL;
        int64_t i64TimeDeltaMs = (int64_t)u64CurrentTimeMs - (int64_t)u64MsgTimeMs;
        if (i64TimeDeltaMs < 0) {
            i64TimeDeltaMs = -i64TimeDeltaMs;
        }

        if ((uint64_t)i64TimeDeltaMs > DF_TIMESTAMP_MAX_ALLOWED_DELTA_MS) {
            ESP_LOGW(TAG, "Bỏ qua: Gói tin quá cũ/lệch giờ (%lld ms)", i64TimeDeltaMs);
            return;
        }
    }

    ESP_LOGI(TAG, "================ [XÁC THỰC IBEACON THÀNH CÔNG] ================");
    ESP_LOGI(TAG, "Phone MAC : %02X:%02X:%02X:%02X:%02X:%02X (RSSI: %d dBm)",
             pxMsg->au8Mac[0], pxMsg->au8Mac[1], pxMsg->au8Mac[2],
             pxMsg->au8Mac[3], pxMsg->au8Mac[4], pxMsg->au8Mac[5], pxMsg->i8Rssi);
    ESP_LOGI(TAG, "Timestamp : %llu ms", u64MsgTimeMs);
    ESP_LOGI(TAG, "Command ID: %u | Control Value: %u%%", sPayload.u8Command, sPayload.u8Value);
    ESP_LOGI(TAG, "===============================================================");

    int iValue = sPayload.u8Value;
    app_logic_control_history_item_t sLog;
    (void)memset(&sLog, 0, sizeof(app_logic_control_history_item_t));
    const char *pcSrcIdBle = "";

    /* 5. Dispatch lệnh điều khiển Rơ-le local */
    switch (sPayload.u8Command) {
        case DF_IBEACON_CMD_CLOSE:
            if (iValue == 1) {
                ESP_LOGI(TAG, "-> LỆNH BLE LOCAL HỢP LỆ: Đóng CỔNG");
                (void)app_logic_relay_Close();
                (void)app_led_state_SetState(E_LED_STATE_GATE_DOWN);
                app_logic_telemetry_BuildControlItem(&sLog, "gate_1", E_TELEMETRY_MODE_CLOSE, E_TELEMETRY_SRC_BLE_BACKUP, pcSrcIdBle, NULL);
                (void)app_logic_telemetry_ReportControlHistory(&sLog, 1);
            }
            break;

        case DF_IBEACON_CMD_STOP:
            if (iValue == 1) {
                ESP_LOGI(TAG, "-> LỆNH BLE LOCAL HỢP LỆ: Dừng cổng");
                (void)app_logic_relay_Stop();
                (void)app_led_state_SetState(E_LED_STATE_GATE_STOP);
                app_logic_telemetry_BuildControlItem(&sLog, "gate_2", E_TELEMETRY_MODE_STOP, E_TELEMETRY_SRC_BLE_BACKUP, pcSrcIdBle, NULL);
                (void)app_logic_telemetry_ReportControlHistory(&sLog, 1);
            }
            break;

        case DF_IBEACON_CMD_OPEN:
            if (iValue == 1) {
                ESP_LOGI(TAG, "-> LỆNH BLE LOCAL HỢP LỆ: Mở cổng");
                (void)app_logic_relay_Open();
                (void)app_led_state_SetState(E_LED_STATE_GATE_UP);
                app_logic_telemetry_BuildControlItem(&sLog, "gate_3", E_TELEMETRY_MODE_OPEN, E_TELEMETRY_SRC_BLE_BACKUP, pcSrcIdBle, NULL);
                (void)app_logic_telemetry_ReportControlHistory(&sLog, 1);
            }
            break;

        case DF_IBEACON_CMD_SET_LEVEL:
            if ((iValue >= 0) && (iValue <= 100)) {
                ESP_LOGI(TAG, "-> Lệnh BLE LOCAL HỢP LỆ: Mở đến %u%%", sPayload.u8Value);
                uint8_t u8TargetVal = (uint8_t)iValue;
                uint8_t u8CurrentVal = app_logic_relay_GetCurrentLevel();

                ESP_LOGI(TAG, "-> Thực thi lệnh: Điều khiển cửa đến mức %u%% (Hiện tại: %u%%)", u8TargetVal, u8CurrentVal);

                if (u8TargetVal > u8CurrentVal) {
                    (void)app_led_state_SetState(E_LED_STATE_GATE_UP);
                } else if (u8TargetVal < u8CurrentVal) {
                    (void)app_led_state_SetState(E_LED_STATE_GATE_DOWN);
                } else {
                    (void)app_led_state_SetState(E_LED_STATE_GATE_STOP);
                }
                app_logic_relay_SetLevel(u8TargetVal);
                app_logic_telemetry_BuildControlItem(&sLog, "open_level", E_TELEMETRY_MODE_PERCENT, E_TELEMETRY_SRC_BLE_BACKUP, pcSrcIdBle, NULL);
                sLog.i32Value = (int32_t)u8TargetVal;
                (void)app_logic_telemetry_ReportControlHistory(&sLog, 1);
            }
            break;

        case DF_IBEACON_CMD_VENT_GAP:
            if (iValue == 1) {
                ESP_LOGI(TAG, "-> Khớp lệnh mở khe thoáng (gate_open_gap)");
                app_logic_relay_OpenVentilationGap();
            }
            break;

        default:
            ESP_LOGW(TAG, "Mã lệnh không hợp lệ: %u", sPayload.u8Command);
            break;
    }
}

static void app_logic_ble_ibeacon_Task(void *pvParameters)
{
    app_ble_ibeacon_msg_t sMsg;
    ESP_LOGI(TAG, "app_logic_ble_ibeacon_Task bắt đầu chạy (Đã bật bộ lọc bảo mật)");

    while (g_bTaskRunning) {
        if (xQueueReceive(g_xIbeaconQueue, &sMsg, portMAX_DELAY) == pdTRUE) {
            app_logic_ble_ibeacon_DecryptAndDispatch(&sMsg);
        }
    }

    vTaskDelete(NULL);
}

esp_err_t app_logic_ble_ibeacon_Init(void)
{
    if (g_bTaskRunning) {
        return ESP_OK;
    }

    g_xIbeaconQueue = xQueueCreate(DF_LOGIC_IBEACON_QUEUE_SIZE, sizeof(app_ble_ibeacon_msg_t));
    if (g_xIbeaconQueue == NULL) {
        ESP_LOGE(TAG, "Tạo Queue thất bại!");
        return ESP_ERR_NO_MEM;
    }

    g_bTaskRunning = true;

    BaseType_t xRet = xTaskCreate(app_logic_ble_ibeacon_Task, "app_logic_ibeacon_task", 
                                  DF_TASK_STACK_LARGE, NULL, DF_TASK_PRIO_CRITICAL, &g_xTaskHandle);
    if (xRet != pdPASS) {
        ESP_LOGE(TAG, "Tạo Task thất bại!");
        vQueueDelete(g_xIbeaconQueue);
        g_xIbeaconQueue = NULL;
        g_bTaskRunning  = false;
        return ESP_FAIL;
    }

    esp_err_t eErr = app_ble_ibeacon_Init(g_xIbeaconQueue);
    if (eErr != ESP_OK) {
        ESP_LOGE(TAG, "Khởi tạo middleware BLE iBeacon thất bại: %s", esp_err_to_name(eErr));
        (void)app_logic_ble_ibeacon_Deinit();
        return eErr;
    }

    ESP_LOGI(TAG, "Khởi tạo app_logic_ble_ibeacon thành công!");
    return ESP_OK;
}

esp_err_t app_logic_ble_ibeacon_Deinit(void)
{
    if (!g_bTaskRunning) {
        return ESP_OK;
    }

    g_bTaskRunning = false;
    (void)app_ble_ibeacon_Deinit();

    if (g_xIbeaconQueue != NULL) {
        vQueueDelete(g_xIbeaconQueue);
        g_xIbeaconQueue = NULL;
    }

    g_xTaskHandle = NULL;
    ESP_LOGI(TAG, "Đã tắt app_logic_ble_ibeacon");
    return ESP_OK;
}