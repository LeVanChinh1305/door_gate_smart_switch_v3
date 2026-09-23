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

static const char *TAG = "APP_BLE_IBEACON";

#define IBEACON_DEBOUNCE_US    (5000000LL) /* Lọc trùng 5s */

static bool s_bRunning = false;
static QueueHandle_t s_xIbeaconQueue = NULL;

static uint8_t s_au8LastUuid[16] = {0};
static int64_t s_i64LastProcessTimeUs = 0;
static uint8_t s_au8OwnMac[6] = {0};

/* Struct Payload 16 bytes chuẩn mã hóa Vconnex */
typedef struct __attribute__((packed)) {
    uint64_t u64TimestampMs;
    uint8_t  au8DeviceMac[6];
    uint8_t  u8Command;
    uint8_t  u8Value;    /*!< Giá trị điều khiển theo % (0 - 100%) */
} ibeacon_decrypted_payload_t;

static int ibeacon_gap_event_cb(struct ble_gap_event *event, void *arg);

/**
 * @brief Lọc trùng gói tin trong cửa sổ 5 giây ngay tại Middleware
 */
static bool is_duplicate_beacon(const uint8_t *pau8Uuid)
{
    int64_t i64NowUs = esp_timer_get_time();

    if (memcmp(s_au8LastUuid, pau8Uuid, 16) == 0) {
        if ((i64NowUs - s_i64LastProcessTimeUs) < IBEACON_DEBOUNCE_US) {
            return true;
        }
    }

    memcpy(s_au8LastUuid, pau8Uuid, 16);
    s_i64LastProcessTimeUs = i64NowUs;
    return false;
}

/**
 * @brief Thử giải mã nhanh để kiểm tra xem bản tin có đúng gửi cho MAC của ESP32 này không
 */
static bool is_target_for_this_device(const uint8_t *pau8Uuid)
{
    char acSecretKey[33] = {0};
    app_nvs_device_config_t sNvsCfg;

    if (app_nvs_LoadDeviceConfig(&sNvsCfg) == ESP_OK && strlen(sNvsCfg.api_secret_key) >= 32) {
        memcpy(acSecretKey, sNvsCfg.api_secret_key, 32);
    } else {
        ESP_LOGE(TAG, "chưa có secrect key để giải mã lệnh ibeacon"); 
    }

    uint8_t au8Iv[16];
    memcpy(au8Iv, acSecretKey, sizeof(au8Iv));

    ibeacon_decrypted_payload_t sPayload;
    mbedtls_aes_context sAes;
    mbedtls_aes_init(&sAes);

    int rc = mbedtls_aes_setkey_dec(&sAes, (const unsigned char *)acSecretKey, 256U);
    if (rc == 0) {
        rc = mbedtls_aes_crypt_cbc(&sAes, MBEDTLS_AES_DECRYPT, 16U, au8Iv, pau8Uuid, (uint8_t *)&sPayload);
    }
    mbedtls_aes_free(&sAes);

    if (rc != 0) return false;

    /* So sánh MAC giải mã ra với MAC thực tế của ESP32 */
    if (memcmp(sPayload.au8DeviceMac, s_au8OwnMac, 6) == 0) {
        return true; /* Đúng thiết bị này! */
    }

    return false; /* Thiết bị khác hoặc rác */
}

/**
 * @brief Kiểm tra & Lọc sạch rác TRƯỚC KHI đẩy vào Queue
 */
static void enqueue_ibeacon_data(const uint8_t *pData, uint8_t u8Len, const ble_addr_t *pAddr, int8_t i8Rssi)
{
    /* 1. Check header iBeacon */
    if (u8Len < 25) return;
    if (pData[0] != 0x4C || pData[1] != 0x00 || pData[2] != 0x02 || pData[3] != 0x15) return;

    const uint8_t *pau8Uuid = &pData[4];

    /* 2. BẢO VỆ 1: Lọc trùng 5s */
    if (is_duplicate_beacon(pau8Uuid)) {
        return;
    }

    /* 3. BẢO VỆ 2: Giải mã kiểm tra MAC chính chủ (DROP NGAY TẠI MIDDLEWARE NẾU SAI MAC) */
    if (!is_target_for_this_device(pau8Uuid)) {
        return;
    }

    /* 4. CHỈ BẢN TIN CHÍNH CHỦ MỚI CHO VÀO QUEUE */
    app_ble_ibeacon_msg_t sMsg;
    memcpy(sMsg.au8Mac, pAddr->val, 6);
    memcpy(sMsg.au8Uuid, pau8Uuid, 16);
    sMsg.u16Major = (pData[20] << 8) | pData[21];
    sMsg.u16Minor = (pData[22] << 8) | pData[23];
    sMsg.i8Rssi = i8Rssi;

    if (s_xIbeaconQueue != NULL) {
        if (xQueueSend(s_xIbeaconQueue, &sMsg, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Queue bị đầy -> Bỏ qua gói tin!");
        } else {
            ESP_LOGI(TAG, ">>> [Middleware] Xác thực chính chủ thành công -> Đã đẩy 1 gói vào Queue");
        }
    }
}

static int ibeacon_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_DISC: {
            const struct ble_gap_disc_desc *desc = &event->disc;
            if (desc->length_data < 5) return 0;

            const uint8_t *p = desc->data;
            uint8_t remaining = desc->length_data;

            while (remaining >= 2) {
                uint8_t field_len  = p[0];
                uint8_t field_type = p[1];

                if (field_len == 0 || (field_len + 1) > remaining) break;

                if (field_type == 0xFF && field_len >= 4) {
                    enqueue_ibeacon_data(&p[2], field_len - 1, &desc->addr, desc->rssi);
                }

                p += (field_len + 1);
                remaining -= (field_len + 1);
            }
            return 0;
        }

        case BLE_GAP_EVENT_DISC_COMPLETE:
            if (s_bRunning) {
                struct ble_gap_disc_params params = {0};
                params.filter_duplicates = 0;
                params.passive = 1;
                params.itvl = BLE_GAP_SCAN_FAST_INTERVAL_MIN;
                params.window = BLE_GAP_SCAN_FAST_WINDOW;
                (void)ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &params, ibeacon_gap_event_cb, NULL);
            }
            return 0;

        default:
            return 0;
    }
}

static esp_err_t ibeacon_start_scan(void)
{
    if (ble_gap_disc_active()) {
        (void)ble_gap_disc_cancel();
    }

    struct ble_gap_disc_params disc_params = {0};
    disc_params.filter_duplicates = 0;
    disc_params.passive = 1;
    disc_params.itvl = BLE_GAP_SCAN_FAST_INTERVAL_MIN;
    disc_params.window = BLE_GAP_SCAN_FAST_WINDOW;

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &disc_params, ibeacon_gap_event_cb, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "Bật Scan thất bại: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, ">>> Middleware Scan đã lọc sạch nhiễu & MAC thiết bị khác");
    return ESP_OK;
}

static esp_err_t ibeacon_stop_scan(void)
{
    (void)ble_gap_disc_cancel();
    return ESP_OK;
}

static esp_err_t ibeacon_profile_init(void)
{
    ble_svc_gap_init();
    ble_svc_gatt_init();
    return ESP_OK;
}

static esp_err_t ibeacon_profile_deinit(void)
{
    (void)ibeacon_stop_scan();
    return ESP_OK;
}

static void ibeacon_on_sync(void)
{
    (void)ibeacon_start_scan();
}

static const app_ble_profile_t s_sIbeaconProfile = {
    .name              = "BLE_IBEACON",
    .profile_init      = ibeacon_profile_init,
    .profile_deinit    = ibeacon_profile_deinit,
    .on_sync           = ibeacon_on_sync,
    .stop_adv          = ibeacon_stop_scan,
};

esp_err_t app_ble_ibeacon_Init(QueueHandle_t xQueue)
{
    if (s_bRunning) return ESP_OK;
    if (xQueue == NULL) return ESP_ERR_INVALID_ARG;

    memset(s_au8LastUuid, 0, sizeof(s_au8LastUuid));
    s_i64LastProcessTimeUs = 0;

    /* Lấy địa chỉ MAC của ESP32 lưu sẵn vào RAM */
    esp_read_mac(s_au8OwnMac, ESP_MAC_WIFI_STA);

    s_xIbeaconQueue = xQueue;
    s_bRunning = true;

    esp_err_t ret = app_ble_manager_Init(&s_sIbeaconProfile);
    if (ret != ESP_OK) {
        s_bRunning = false;
        return ret;
    }

    return ESP_OK;
}

esp_err_t app_ble_ibeacon_Deinit(void)
{
    if (!s_bRunning) return ESP_OK;
    s_bRunning = false;
    (void)app_ble_manager_Deinit();
    s_xIbeaconQueue = NULL;
    return ESP_OK;
}

bool app_ble_ibeacon_IsRunning(void)
{
    return s_bRunning;
}