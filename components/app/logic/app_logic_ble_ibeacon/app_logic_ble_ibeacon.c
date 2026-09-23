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

static const char *TAG = "APP_LOGIC_BLE_IBEACON";

#define LOGIC_IBEACON_QUEUE_SIZE          4
#define LOGIC_IBEACON_TASK_STACK_SIZE     4096
#define LOGIC_IBEACON_TASK_PRIO           5

/* Ngưỡng lệch thời gian tối đa cho phép (10 giây = 10000 ms) */
#define TIMESTAMP_MAX_ALLOWED_DELTA_MS    (10000ULL)

/* Struct Payload 16 bytes chuẩn mã hóa từ App Vconnex */
typedef struct __attribute__((packed)) {
    uint64_t u64TimestampMs;  /*!< 8 bytes: Timestamp dạng Milliseconds (Big Endian) */
    uint8_t  au8DeviceMac[6];  /*!< 6 bytes: MAC của thiết bị ESP32 */
    uint8_t  u8Command;        /*!< 1 byte : Mã lệnh (1: Gate 1, 2: Gate 2, 3: Stop) */
    uint8_t  u8Source;         /*!< 1 byte : Sub-command / Source */
} ibeacon_decrypted_payload_t;

static bool s_bTaskRunning = false;
static QueueHandle_t s_xIbeaconQueue = NULL;
static TaskHandle_t s_xTaskHandle = NULL;

/**
 * @brief Đảo ngược byte order từ Big Endian (Network) sang Little Endian (MCU) cho uint64_t
 */
static uint64_t ntoh64(uint64_t val)
{
    return ((val & 0x00000000000000FFULL) << 56) |
           ((val & 0x000000000000FF00ULL) << 40) |
           ((val & 0x0000000000FF0000ULL) << 24) |
           ((val & 0x00000000FF000000ULL) << 8)  |
           ((val & 0x000000FF00000000ULL) >> 8)  |
           ((val & 0x0000FF0000000000ULL) >> 24) |
           ((val & 0x00FF000000000000ULL) >> 40) |
           ((val & 0xFF00000000000000ULL) >> 56);
}

/**
 * @brief Giải mã AES-256-CBC và Thực thi lệnh Relay Local
 */
static void decrypt_and_dispatch(const app_ble_ibeacon_msg_t *pMsg)
{
    char acSecretKey[33] = {0};
    app_nvs_device_config_t sNvsCfg;

    /* 1. Nạp Secret Key 32 bytes */
    if (app_nvs_LoadDeviceConfig(&sNvsCfg) == ESP_OK && strlen(sNvsCfg.api_secret_key) >= 32) {
        memcpy(acSecretKey, sNvsCfg.api_secret_key, 32);
        acSecretKey[32] = '\0';
    } else {
        const char *pcDefaultKey = "VCONNEX_SMART_GATE_SECRET_KEY_32";
        memcpy(acSecretKey, pcDefaultKey, 32);
        acSecretKey[32] = '\0';
    }

    /* 2. Tạo Vector IV (16 bytes đầu của Secret Key) */
    uint8_t au8Iv[16];
    memcpy(au8Iv, acSecretKey, sizeof(au8Iv));

    /* 3. Giải mã AES-256-CBC */
    ibeacon_decrypted_payload_t sPayload;
    mbedtls_aes_context sAes;
    mbedtls_aes_init(&sAes);

    int iMbedResult = mbedtls_aes_setkey_dec(&sAes, (const unsigned char *)acSecretKey, 256U);
    if (iMbedResult == 0) {
        iMbedResult = mbedtls_aes_crypt_cbc(&sAes, MBEDTLS_AES_DECRYPT, 16U,
                                            au8Iv, pMsg->au8Uuid, (uint8_t *)&sPayload);
    }
    mbedtls_aes_free(&sAes);

    if (iMbedResult != 0) {
        ESP_LOGE(TAG, "Giải mã AES-256-CBC thất bại: %d", iMbedResult);
        return;
    }

    /* 4. Kiểm tra Lệch thời gian (Timestamp Expiry) - Chống Replay Attack */
    uint64_t u64MsgTimeMs = ntoh64(sPayload.u64TimestampMs);
    time_t nowSec = time(NULL);
    if (nowSec > 1600000000) { /* Chỉ kiểm tra nếu hệ thống đã đồng bộ giờ SNTP */
        uint64_t u64CurrentTimeMs = (uint64_t)nowSec * 1000ULL;
        int64_t i64TimeDeltaMs = (int64_t)u64CurrentTimeMs - (int64_t)u64MsgTimeMs;
        if (i64TimeDeltaMs < 0) i64TimeDeltaMs = -i64TimeDeltaMs;

        if ((uint64_t)i64TimeDeltaMs > TIMESTAMP_MAX_ALLOWED_DELTA_MS) {
            ESP_LOGW(TAG, "Bỏ qua: Gói tin quá cũ/lệch giờ (%lld ms)", i64TimeDeltaMs);
            return;
        }
    }

    ESP_LOGI(TAG, "================ [XÁC THỰC IBEACON THÀNH CÔNG] ================");
    ESP_LOGI(TAG, "Phone MAC : %02X:%02X:%02X:%02X:%02X:%02X (RSSI: %d dBm)",
             pMsg->au8Mac[0], pMsg->au8Mac[1], pMsg->au8Mac[2],
             pMsg->au8Mac[3], pMsg->au8Mac[4], pMsg->au8Mac[5], pMsg->i8Rssi);
    ESP_LOGI(TAG, "Timestamp : %llu ms", u64MsgTimeMs);
    ESP_LOGI(TAG, "Command ID: %u", sPayload.u8Command);
    ESP_LOGI(TAG, "===============================================================");

    /* 5. Dispatch lệnh điều khiển Rơ-le local */
    switch (sPayload.u8Command) {
        case 1:
            ESP_LOGI(TAG, "-> LỆNH BLE LOCAL HỢP LỆ: MỞ CỔNG 1");
            break;
        case 2:
            ESP_LOGI(TAG, "-> LỆNH BLE LOCAL HỢP LỆ: MỞ CỔNG 2");
            break;
        case 3:
            ESP_LOGI(TAG, "-> LỆNH BLE LOCAL HỢP LỆ: DỪNG CỔNG (STOP)");
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

    while (s_bTaskRunning) {
        if (xQueueReceive(s_xIbeaconQueue, &sMsg, portMAX_DELAY) == pdTRUE) {
            decrypt_and_dispatch(&sMsg);
        }
    }

    vTaskDelete(NULL);
}

esp_err_t app_logic_ble_ibeacon_Init(void)
{
    if (s_bTaskRunning) return ESP_OK;

    s_xIbeaconQueue = xQueueCreate(LOGIC_IBEACON_QUEUE_SIZE, sizeof(app_ble_ibeacon_msg_t));
    if (s_xIbeaconQueue == NULL) {
        ESP_LOGE(TAG, "Tạo Queue thất bại!");
        return ESP_ERR_NO_MEM;
    }

    s_bTaskRunning = true;

    BaseType_t xRet = xTaskCreate(app_logic_ble_ibeacon_Task, "app_logic_ibeacon_task",
                                  LOGIC_IBEACON_TASK_STACK_SIZE, NULL, LOGIC_IBEACON_TASK_PRIO, &s_xTaskHandle);
    if (xRet != pdPASS) {
        ESP_LOGE(TAG, "Tạo Task thất bại!");
        vQueueDelete(s_xIbeaconQueue);
        s_xIbeaconQueue = NULL;
        s_bTaskRunning = false;
        return ESP_FAIL;
    }

    esp_err_t err = app_ble_ibeacon_Init(s_xIbeaconQueue);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khởi tạo middleware BLE iBeacon thất bại: %s", esp_err_to_name(err));
        app_logic_ble_ibeacon_Deinit();
        return err;
    }

    ESP_LOGI(TAG, "Khởi tạo app_logic_ble_ibeacon thành công!");
    return ESP_OK;
}

esp_err_t app_logic_ble_ibeacon_Deinit(void)
{
    if (!s_bTaskRunning) return ESP_OK;

    s_bTaskRunning = false;
    (void)app_ble_ibeacon_Deinit();

    if (s_xIbeaconQueue != NULL) {
        vQueueDelete(s_xIbeaconQueue);
        s_xIbeaconQueue = NULL;
    }

    s_xTaskHandle = NULL;
    ESP_LOGI(TAG, "Đã tắt app_logic_ble_ibeacon");
    return ESP_OK;
}