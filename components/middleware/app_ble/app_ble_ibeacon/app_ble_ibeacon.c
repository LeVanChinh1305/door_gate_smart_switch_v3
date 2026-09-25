/**
 * @file    app_ble_ibeacon.c
 * @brief   Hiện thực logic quét, lọc trùng và giải mã gói tin BLE iBeacon
 */

#include "app_ble_ibeacon.h"
#include "app_ble_manager.h"
#include "app_nvs.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "mbedtls/aes.h"
#include <string.h>

/* Tag ghi log */
static const char *TAG = "APP_BLE_IBEACON";

/* Hằng số và Macro theo chuẩn DF_ */
#define DF_IBEACON_DEBOUNCE_US          (5000000LL) /* Thời gian lọc trùng gói tin: 5 giây */
#define DF_IBEACON_APPLE_ID_BYTE0       (0x4CU)
#define DF_IBEACON_APPLE_ID_BYTE1       (0x00U)
#define DF_IBEACON_TYPE_BYTE            (0x02U)
#define DF_IBEACON_LEN_BYTE             (0x15U)
#define DF_IBEACON_MIN_PKT_LEN          (25U)
#define DF_IBEACON_AES_KEY_BYTES        (32U)
#define DF_IBEACON_AES_KEY_BITS         (256U)
#define DF_IBEACON_AD_TYPE_MANUFACTURER (0xFFU)

/* Biến toàn cục module (File scope) tuân theo g_ + kiểu biến */
static bool          g_bIsRunning                       = false;
static QueueHandle_t g_xIbeaconQueue                    = NULL;
static uint8_t       g_au8LastUuid[DF_IBEACON_UUID_LEN] = {0};
static int64_t       g_i64LastProcessTimeUs             = 0;
static uint8_t       g_au8OwnMac[DF_IBEACON_MAC_LEN]    = {0};

/* Cấu trúc payload sau giải mã (16 bytes nội bộ) */
typedef struct __attribute__((packed)) {
    uint64_t u64TimestampMs;
    uint8_t  au8DeviceMac[DF_IBEACON_MAC_LEN];
    uint8_t  u8Command;
    uint8_t  u8Value;    /*!< Giá trị điều khiển theo % (0 - 100%) */
} app_ble_ibeacon_decrypted_payload_t;

/* Khai báo nguyên mẫu hàm nội bộ */
static int       app_ble_ibeacon_GapEventCb(struct ble_gap_event *pxEvent, void *pvArg);
static bool      app_ble_ibeacon_IsDuplicate(const uint8_t *pau8Uuid);
static bool      app_ble_ibeacon_IsTargetForThisDevice(const uint8_t *pau8Uuid);
static void      app_ble_ibeacon_EnqueueData(const uint8_t *pu8Data, uint8_t u8Len, const ble_addr_t *pxAddr, int8_t i8Rssi);
static esp_err_t app_ble_ibeacon_StartScan(void);
static esp_err_t app_ble_ibeacon_StopScan(void);
static esp_err_t app_ble_ibeacon_ProfileInit(void);
static esp_err_t app_ble_ibeacon_ProfileDeinit(void);
static void      app_ble_ibeacon_OnSync(void);

/**
 * @brief  Lọc trùng gói tin trong cửa sổ debounce
 * @param  pau8Uuid Con trỏ đến chuỗi 16 bytes UUID
 * @return true nếu là gói trùng, false nếu là gói mới
 */
static bool app_ble_ibeacon_IsDuplicate(const uint8_t *pau8Uuid)
{
    if (pau8Uuid == NULL) {
        return true;
    }

    int64_t i64NowUs = esp_timer_get_time();

    if (memcmp(g_au8LastUuid, pau8Uuid, DF_IBEACON_UUID_LEN) == 0) {
        if ((i64NowUs - g_i64LastProcessTimeUs) < DF_IBEACON_DEBOUNCE_US) {
            return true;
        }
    }

    memcpy(g_au8LastUuid, pau8Uuid, DF_IBEACON_UUID_LEN);
    g_i64LastProcessTimeUs = i64NowUs;
    return false;
}

/**
 * @brief  Giải mã nhanh và kiểm tra MAC có khớp với thiết bị này hay không
 * @param  pau8Uuid Con trỏ payload mã hóa 16 bytes
 * @return true nếu gửi đúng cho ESP32 này, false nếu không hợp lệ
 */
static bool app_ble_ibeacon_IsTargetForThisDevice(const uint8_t *pau8Uuid)
{
    if (pau8Uuid == NULL) {
        return false;
    }

    char acSecretKey[DF_IBEACON_AES_KEY_BYTES + 1U] = {0};
    app_nvs_device_config_t sNvsCfg;

    if ((app_nvs_LoadDeviceConfig(&sNvsCfg) == ESP_OK) && 
        (strlen(sNvsCfg.api_secret_key) >= DF_IBEACON_AES_KEY_BYTES)) {
        memcpy(acSecretKey, sNvsCfg.api_secret_key, DF_IBEACON_AES_KEY_BYTES);
    } else {
        ESP_LOGE(TAG, "Chưa có secret key để giải mã iBeacon");
        return false;
    }

    uint8_t au8Iv[DF_IBEACON_UUID_LEN];
    memcpy(au8Iv, acSecretKey, sizeof(au8Iv));

    app_ble_ibeacon_decrypted_payload_t sPayload;
    mbedtls_aes_context sAes;
    mbedtls_aes_init(&sAes);

    int iRc = mbedtls_aes_setkey_dec(&sAes, (const unsigned char *)acSecretKey, DF_IBEACON_AES_KEY_BITS);
    if (iRc == 0) {
        iRc = mbedtls_aes_crypt_cbc(&sAes, MBEDTLS_AES_DECRYPT, (size_t)DF_IBEACON_UUID_LEN, 
                                    au8Iv, pau8Uuid, (uint8_t *)&sPayload);
    }
    mbedtls_aes_free(&sAes);

    if (iRc != 0) {
        return false;
    }

    /* So sánh MAC giải mã ra với MAC thực tế của ESP32 */
    if (memcmp(sPayload.au8DeviceMac, g_au8OwnMac, DF_IBEACON_MAC_LEN) == 0) {
        return true;
    }

    return false;
}

/**
 * @brief  Kiểm tra cú pháp gói tin, lọc trùng và đẩy vào Queue xử lý
 * @param  pu8Data  Dữ liệu gói iBeacon
 * @param  u8Len    Chiều dài dữ liệu
 * @param  pxAddr   Địa chỉ nguồn phát
 * @param  i8Rssi   Cường độ tín hiệu
 */
static void app_ble_ibeacon_EnqueueData(const uint8_t *pu8Data, uint8_t u8Len, const ble_addr_t *pxAddr, int8_t i8Rssi)
{
    if ((pu8Data == NULL) || (pxAddr == NULL)) {
        return;
    }

    /* 1. Kiểm tra header Apple iBeacon */
    if (u8Len < DF_IBEACON_MIN_PKT_LEN) {
        return;
    }

    if ((pu8Data[0] != DF_IBEACON_APPLE_ID_BYTE0) ||
        (pu8Data[1] != DF_IBEACON_APPLE_ID_BYTE1) ||
        (pu8Data[2] != DF_IBEACON_TYPE_BYTE) ||
        (pu8Data[3] != DF_IBEACON_LEN_BYTE)) {
        return;
    }

    const uint8_t *pau8Uuid = &pu8Data[4];

    /* 2. Lọc trùng gói tin */
    if (app_ble_ibeacon_IsDuplicate(pau8Uuid)) {
        return;
    }

    /* 3. Kiểm tra MAC chính chủ (Drop ngay tại middleware nếu sai MAC) */
    if (!app_ble_ibeacon_IsTargetForThisDevice(pau8Uuid)) {
        return;
    }

    /* 4. Bản tin hợp lệ -> Đẩy vào Queue */
    app_ble_ibeacon_msg_t sMsg;
    memcpy(sMsg.au8Mac, pxAddr->val, DF_IBEACON_MAC_LEN);
    memcpy(sMsg.au8Uuid, pau8Uuid, DF_IBEACON_UUID_LEN);
    sMsg.u16Major = ((uint16_t)pu8Data[20] << 8) | (uint16_t)pu8Data[21];
    sMsg.u16Minor = ((uint16_t)pu8Data[22] << 8) | (uint16_t)pu8Data[23];
    sMsg.i8Rssi   = i8Rssi;

    if (g_xIbeaconQueue != NULL) {
        if (xQueueSend(g_xIbeaconQueue, &sMsg, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Queue bị đầy -> Bỏ qua gói tin");
        } else {
            ESP_LOGI(TAG, "Xác thực chính chủ thành công -> Đã đẩy gói vào Queue");
        }
    }
}

/**
 * @brief  Callback xử lý sự kiện GAP từ BLE Stack NimBLE
 */
static int app_ble_ibeacon_GapEventCb(struct ble_gap_event *pxEvent, void *pvArg)
{
    if (pxEvent == NULL) {
        return 0;
    }

    switch (pxEvent->type) {
        case BLE_GAP_EVENT_DISC: {
            const struct ble_gap_disc_desc *pxDesc = &pxEvent->disc;
            if (pxDesc->length_data < 5U) {
                return 0;
            }

            const uint8_t *pu8Data    = pxDesc->data;
            uint8_t        u8Remaining = pxDesc->length_data;

            while (u8Remaining >= 2U) {
                uint8_t u8FieldLen  = pu8Data[0];
                uint8_t u8FieldType = pu8Data[1];

                if ((u8FieldLen == 0U) || ((u8FieldLen + 1U) > u8Remaining)) {
                    break;
                }

                if ((u8FieldType == DF_IBEACON_AD_TYPE_MANUFACTURER) && (u8FieldLen >= 4U)) {
                    app_ble_ibeacon_EnqueueData(&pu8Data[2], u8FieldLen - 1U, &pxDesc->addr, pxDesc->rssi);
                }

                pu8Data     += (u8FieldLen + 1U);
                u8Remaining -= (u8FieldLen + 1U);
            }
            return 0;
        }

        case BLE_GAP_EVENT_DISC_COMPLETE:
            if (g_bIsRunning) {
                struct ble_gap_disc_params sParams = {0};
                sParams.filter_duplicates = 0;
                sParams.passive           = 1;
                sParams.itvl              = BLE_GAP_SCAN_FAST_INTERVAL_MIN;
                sParams.window            = BLE_GAP_SCAN_FAST_WINDOW;
                (void)ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &sParams, app_ble_ibeacon_GapEventCb, NULL);
            }
            return 0;

        default:
            return 0;
    }
}

static esp_err_t app_ble_ibeacon_StartScan(void)
{
    if (ble_gap_disc_active()) {
        (void)ble_gap_disc_cancel();
    }

    struct ble_gap_disc_params sDiscParams = {0};
    sDiscParams.filter_duplicates = 0;
    sDiscParams.passive           = 1;
    sDiscParams.itvl              = BLE_GAP_SCAN_FAST_INTERVAL_MIN;
    sDiscParams.window            = BLE_GAP_SCAN_FAST_WINDOW;

    int iRc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &sDiscParams, app_ble_ibeacon_GapEventCb, NULL);
    if ((iRc != 0) && (iRc != BLE_HS_EALREADY)) {
        ESP_LOGE(TAG, "Bật Scan thất bại: %d", iRc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Middleware Scan đã khởi động");
    return ESP_OK;
}

static esp_err_t app_ble_ibeacon_StopScan(void)
{
    (void)ble_gap_disc_cancel();
    return ESP_OK;
}

static esp_err_t app_ble_ibeacon_ProfileInit(void)
{
    ble_svc_gap_init();
    ble_svc_gatt_init();
    return ESP_OK;
}

static esp_err_t app_ble_ibeacon_ProfileDeinit(void)
{
    return app_ble_ibeacon_StopScan();
}

static void app_ble_ibeacon_OnSync(void)
{
    (void)app_ble_ibeacon_StartScan();
}

static const app_ble_profile_t g_sIbeaconProfile = {
    .name           = "BLE_IBEACON",
    .profile_init   = app_ble_ibeacon_ProfileInit,
    .profile_deinit = app_ble_ibeacon_ProfileDeinit,
    .on_sync        = app_ble_ibeacon_OnSync,
    .stop_adv       = app_ble_ibeacon_StopScan,
};

esp_err_t app_ble_ibeacon_Init(QueueHandle_t xQueue)
{
    if (g_bIsRunning) {
        return ESP_OK;
    }
    if (xQueue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(g_au8LastUuid, 0, sizeof(g_au8LastUuid));
    g_i64LastProcessTimeUs = 0;

    /* Lấy địa chỉ MAC của ESP32 lưu sẵn vào RAM */
    esp_read_mac(g_au8OwnMac, ESP_MAC_WIFI_STA);

    g_xIbeaconQueue = xQueue;
    g_bIsRunning    = true;

    esp_err_t eRet = app_ble_manager_Init(&g_sIbeaconProfile);
    if (eRet != ESP_OK) {
        g_bIsRunning = false;
        return eRet;
    }

    return ESP_OK;
}

esp_err_t app_ble_ibeacon_Deinit(void)
{
    if (!g_bIsRunning) {
        return ESP_OK;
    }
    g_bIsRunning = false;
    (void)app_ble_manager_Deinit();
    g_xIbeaconQueue = NULL;
    return ESP_OK;
}

bool app_ble_ibeacon_IsRunning(void)
{
    return g_bIsRunning;
}