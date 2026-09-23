#include "app_ble_ibeacon.h"
#include "app_ble_manager.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>

static const char *TAG = "APP_BLE_IBEACON";

static bool s_bRunning = false;

/* Thời gian chống lặp gói tin (tính bằng microsecond: 5000000us = 5 giây) */
#define IBEACON_DEBOUNCE_INTERVAL_US    (5000000LL)

/* Biến lưu trữ thông tin gói tin gần nhất để lọc trùng */
static uint8_t s_au8LastUuid[16] = {0};
static int64_t s_i64LastProcessTimeUs = 0;

/* Khai báo trước hàm GAP Event Callback */
static int ibeacon_gap_event_cb(struct ble_gap_event *event, void *arg);

/* ======================== XỬ LÝ LỌC TRÙNG & NGIỆM VỤ ======================== */

/**
 * @brief Kiểm tra xem gói iBeacon có bị lặp lại trong khoảng thời gian Cooldown hay không
 * @return true nếu là gói tin trùng cần BỎ QUA, false nếu là gói tin MỚI cần XỬ LÝ
 */
static bool is_duplicate_beacon(const uint8_t *pau8Uuid)
{
    int64_t i64NowUs = esp_timer_get_time();

    /* Kiểm tra xem UUID có trùng với gói vừa xử lý không */
    if (memcmp(s_au8LastUuid, pau8Uuid, 16) == 0) {
        /* Nếu trùng UUID và chưa hết thời gian Cooldown (5 giây) -> Xác nhận trùng */
        if ((i64NowUs - s_i64LastProcessTimeUs) < IBEACON_DEBOUNCE_INTERVAL_US) {
            return true;
        }
    }

    /* Lưu vết gói tin mới và thời điểm xử lý */
    memcpy(s_au8LastUuid, pau8Uuid, 16);
    s_i64LastProcessTimeUs = i64NowUs;
    return false;
}

/**
 * @brief In chi tiết dữ liệu gói tin iBeacon và xử lý logic
 */
static void process_ibeacon_payload(const uint8_t *data, uint8_t len, const ble_addr_t *addr)
{
    /* Gói iBeacon Apple tiêu chuẩn: Company(2) + Type(1) + Len(1) + UUID(16) + Major(2) + Minor(2) + TX(1) = 25 bytes */
    if (len < 25) {
        return;
    }

    /* Kiểm tra Company ID = Apple (0x004C) */
    if (data[0] != 0x4C || data[1] != 0x00) {
        return;
    }

    /* iBeacon Type = 0x02, Length = 0x15 (21 bytes) */
    if (data[2] != 0x02 || data[3] != 0x15) {
        return;
    }

    const uint8_t *uuid = &data[4];
    uint16_t major = (data[20] << 8) | data[21];
    uint16_t minor = (data[22] << 8) | data[23];
    int8_t tx_power = (int8_t)data[24];

    /* --- BƯỚC LỌC TRÙNG GÓI TIN --- */
    if (is_duplicate_beacon(uuid)) {
        /* Bỏ qua gói tin trùng lặp trong cửa sổ 5 giây, không in log dồn dập */
        return;
    }

    /* Chỉ chạy tới đây NẾU LÀ GÓI TIN MỚI (xử lý 1 lần duy nhất) */
    ESP_LOGI(TAG, "================ [NHẬN DỮ LIỆU IBEACON MỚI] ================");
    ESP_LOGI(TAG, "Từ MAC App/Phone : %02X:%02X:%02X:%02X:%02X:%02X",
             addr->val[5], addr->val[4], addr->val[3],
             addr->val[2], addr->val[1], addr->val[0]);

    ESP_LOGI(TAG, "UUID (16 bytes)  : %02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
             uuid[0], uuid[1], uuid[2], uuid[3],
             uuid[4], uuid[5], uuid[6], uuid[7],
             uuid[8], uuid[9], uuid[10], uuid[11],
             uuid[12], uuid[13], uuid[14], uuid[15]);

    ESP_LOGI(TAG, "Major (2 bytes)  : 0x%04X (%u)", major, major);
    ESP_LOGI(TAG, "Minor (2 bytes)  : 0x%04X (%u)", minor, minor);
    ESP_LOGI(TAG, "TX Power         : %d dBm", tx_power);
    
    ESP_LOGI(TAG, "Raw Payload HEX:");
    ESP_LOG_BUFFER_HEX(TAG, data, len);
    ESP_LOGI(TAG, "============================================================");

    /* TODO: Gọi hàm giải mã AES-128 và đóng/ngắt Relay ở đây */
}

/* ======================== GAP EVENT CALLBACK ======================== */

static int ibeacon_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {

        case BLE_GAP_EVENT_DISC: {
            const struct ble_gap_disc_desc *desc = &event->disc;

            if (desc->length_data < 5) {
                return 0;
            }

            const uint8_t *p = desc->data;
            uint8_t remaining = desc->length_data;

            while (remaining >= 2) {
                uint8_t field_len  = p[0];
                uint8_t field_type = p[1];

                if (field_len == 0 || (field_len + 1) > remaining) {
                    break;
                }

                /* Trường Manufacturer Specific Data (Type 0xFF) */
                if (field_type == 0xFF && field_len >= 4) {
                    process_ibeacon_payload(&p[2], field_len - 1, &desc->addr);
                }

                p += (field_len + 1);
                remaining -= (field_len + 1);
            }
            return 0;
        }

        case BLE_GAP_EVENT_DISC_COMPLETE:
            /* Tự động lặp lại Scan khi hết lượt */
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

/* ======================== START / STOP SCAN ======================== */

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
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER,
                          &disc_params, ibeacon_gap_event_cb, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "Bật Scan iBeacon thất bại: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, ">>> ĐÃ BẬT SCAN IBEACON (Đã tích hợp lọc trùng gói tin 5s)...");
    return ESP_OK;
}

static esp_err_t ibeacon_stop_scan(void)
{
    (void)ble_gap_disc_cancel();
    ESP_LOGI(TAG, "Đã dừng Scan iBeacon");
    return ESP_OK;
}

/* ======================== PROFILE CALLBACKS ======================== */

static esp_err_t ibeacon_profile_init(void)
{
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ESP_LOGI(TAG, "iBeacon Profile Init");
    return ESP_OK;
}

static esp_err_t ibeacon_profile_deinit(void)
{
    (void)ibeacon_stop_scan();
    return ESP_OK;
}

static void ibeacon_on_sync(void)
{
    ESP_LOGI(TAG, "NimBLE sync thành công → Bắt đầu SCAN");
    (void)ibeacon_start_scan();
}

static const app_ble_profile_t s_sIbeaconProfile = {
    .name              = "BLE_IBEACON",
    .profile_init      = ibeacon_profile_init,
    .profile_deinit    = ibeacon_profile_deinit,
    .on_sync           = ibeacon_on_sync,
    .on_reset          = NULL,
    .gatts_register_cb = NULL,
    .start_adv         = NULL,
    .stop_adv          = ibeacon_stop_scan,
};

/* ======================== PUBLIC API ======================== */

esp_err_t app_ble_ibeacon_Init(void)
{
    if (s_bRunning) {
        ESP_LOGW(TAG, "iBeacon Module đã đang chạy");
        return ESP_OK;
    }

    /* Reset bộ đệm lọc trùng */
    memset(s_au8LastUuid, 0, sizeof(s_au8LastUuid));
    s_i64LastProcessTimeUs = 0;

    esp_err_t ret = app_ble_manager_Init(&s_sIbeaconProfile);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Khởi tạo BLE Manager thất bại: %s", esp_err_to_name(ret));
        return ret;
    }

    s_bRunning = true;
    ESP_LOGI(TAG, "Khởi động Module iBeacon thành công!");
    return ESP_OK;
}

esp_err_t app_ble_ibeacon_Deinit(void)
{
    if (!s_bRunning) {
        return ESP_OK;
    }

    (void)app_ble_manager_Deinit();
    s_bRunning = false;
    ESP_LOGI(TAG, "Đã tắt Module iBeacon");
    return ESP_OK;
}

bool app_ble_ibeacon_IsRunning(void)
{
    return s_bRunning;
}