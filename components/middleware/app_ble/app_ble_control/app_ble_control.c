#include "app_ble_control.h"
#include "app_ble_manager.h"
#include "esp_log.h"
#include <string.h>
#include <ctype.h>

#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "APP_BLE_CONTROL";

/* Service UUID: 0xFF00 (16-bit) cho Cổng Thông Minh */
static const ble_uuid16_t s_gate_svc_uuid = BLE_UUID16_INIT(0xFF00);

/* RX Characteristic: 0xFF01 (Write / Write No Response) - App gửi lệnh tới ESP32 */
static const ble_uuid16_t s_gate_rx_chr_uuid = BLE_UUID16_INIT(0xFF01);

/* TX Characteristic: 0xFF02 (Read / Notify) - ESP32 phản hồi trạng thái về App */
static const ble_uuid16_t s_gate_tx_chr_uuid = BLE_UUID16_INIT(0xFF02);

static uint8_t s_own_addr_type = 0;
static bool s_bIsRunning = false;
static bool s_bIsConnected = false;
static uint16_t s_conn_handle = 0;
static uint16_t s_tx_val_handle = 0;

static int gate_control_gap_event(struct ble_gap_event *event, void *arg);

/**
 * @brief Callback khi App ghi dữ liệu vào RX Characteristic (0xFF01)
 */
static int gate_control_rx_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                     struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len == 0) {
            ESP_LOGW(TAG, "Nhận gói tin rỗng từ App (len = 0)");
            return 0;
        }

        uint8_t rx_buf[256];
        uint16_t read_len = (len < sizeof(rx_buf) - 1) ? len : (sizeof(rx_buf) - 1);
        int rc = os_mbuf_copydata(ctxt->om, 0, read_len, rx_buf);
        if (rc != 0) {
            ESP_LOGE(TAG, "Lỗi đọc dữ liệu từ mbuf buffer: rc=%d", rc);
            return BLE_ATT_ERR_UNLIKELY;
        }
        rx_buf[read_len] = '\0';

        /* 1. Log độ dài */
        ESP_LOGI(TAG, "==================================================");
        ESP_LOGI(TAG, "[BLE CONTROL] DỮ LIỆU NHẬN TỪ APP (Độ dài: %u bytes)", (unsigned int)len);

        /* 2. Log dạng HEX */
        char hex_str[3 * sizeof(rx_buf) + 1] = {0};
        int hex_pos = 0;
        for (uint16_t i = 0; i < read_len; i++) {
            hex_pos += snprintf(hex_str + hex_pos, sizeof(hex_str) - hex_pos, "%02X ", rx_buf[i]);
        }
        ESP_LOGI(TAG, "   HEX   : %s", hex_str);

        /* 3. Log dạng STRING (nếu chứa các ký tự ASCII đọc được) */
        bool is_printable = true;
        for (uint16_t i = 0; i < read_len; i++) {
            if (!isprint((unsigned char)rx_buf[i]) && rx_buf[i] != '\r' && rx_buf[i] != '\n') {
                is_printable = false;
                break;
            }
        }
        if (is_printable) {
            ESP_LOGI(TAG, "   STRING: \"%s\"", (char *)rx_buf);
        } else {
            ESP_LOGI(TAG, "   STRING: (Chứa binary/non-printable data)");
        }
        ESP_LOGI(TAG, "==================================================");

        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

/**
 * @brief Callback khi App đọc dữ liệu từ TX Characteristic (0xFF02)
 */
static int gate_control_tx_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                     struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const char *resp = "OK";
        int rc = os_mbuf_append(ctxt->om, resp, strlen(resp));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/* Định nghĩa bảng dịch vụ GATT cho Gate Control */
static const struct ble_gatt_svc_def s_gate_control_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_gate_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* RX: App ghi lệnh xuống ESP32 */
                .uuid = &s_gate_rx_chr_uuid.u,
                .access_cb = gate_control_rx_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                /* TX: ESP32 phản hồi trạng thái cho App */
                .uuid = &s_gate_tx_chr_uuid.u,
                .access_cb = gate_control_tx_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_tx_val_handle,
            },
            {
                0, /* Kết thúc danh sách characteristics */
            }
        },
    },
    {
        0, /* Kết thúc danh sách services */
    }
};

/**
 * @brief Bắt đầu phát quảng bá BLE với Service UUID 0xFF00
 */
static esp_err_t gate_control_start_adv(void) {
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    const char *device_name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;

    ble_uuid16_t svcs[1] = { s_gate_svc_uuid };
    fields.uuids16 = svcs;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Lỗi cấu hình gói quảng bá ble_gap_adv_set_fields: rc=%d", rc);
        return ESP_FAIL;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, gate_control_gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "Lỗi ble_gap_adv_start: rc=%d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Đang phát quảng bá BLE Gate Control (Tên: %s, Service UUID: 0x%04X)...",
             device_name, s_gate_svc_uuid.value);
    return ESP_OK;
}

/**
 * @brief Dừng phát quảng bá BLE
 */
static esp_err_t gate_control_stop_adv(void) {
    int rc = ble_gap_adv_stop();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "ble_gap_adv_stop rc=%d", rc);
    }
    return ESP_OK;
}

/**
 * @brief Xử lý các sự kiện GAP (kết nối, ngắt kết nối, MTU, subscribe)
 */
static int gate_control_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                s_bIsConnected = true;
                s_conn_handle = event->connect.conn_handle;
                ESP_LOGI(TAG, "App đã kết nối BLE thành công (conn_handle=%u)", s_conn_handle);
            } else {
                ESP_LOGW(TAG, "Kết nối BLE thất bại, mã lỗi: %d -> Tiếp tục phát quảng bá", event->connect.status);
                (void)gate_control_start_adv();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "App đã ngắt kết nối BLE (lý do: %d) -> Bắt đầu phát lại quảng bá", event->disconnect.reason);
            s_bIsConnected = false;
            s_conn_handle = 0;
            (void)gate_control_start_adv();
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ESP_LOGI(TAG, "Quảng bá BLE hoàn thành -> Bắt đầu lại");
            (void)gate_control_start_adv();
            break;

        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI(TAG, "App đã đăng ký Subscribe/Notify (attr_handle=%u, cur_notify=%d)", event->subscribe.attr_handle, event->subscribe.cur_notify);
            break;

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "Cập nhật MTU: conn_handle=%u, MTU=%u", event->mtu.conn_handle, event->mtu.value);
            break;

        default:
            break;
    }
    return 0;
}

/**
 * @brief Khởi tạo bảng GATT Server của Gate Control
 */
static esp_err_t gate_control_profile_init(void) {
    int rc;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(s_gate_control_services);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg thất bại: %d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(s_gate_control_services);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs thất bại: %d", rc);
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief Hủy profile Gate Control
 */
static esp_err_t gate_control_profile_deinit(void) {
    (void)gate_control_stop_adv();
    s_bIsConnected = false;
    s_conn_handle = 0;
    return ESP_OK;
}

/**
 * @brief Hook khi NimBLE Host hoàn tất đồng bộ
 */
static void gate_control_on_sync(void) {
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr lỗi: %d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto lỗi: %d", rc);
        return;
    }

    (void)gate_control_start_adv();
}

/* Định nghĩa cấu trúc Profile theo chuẩn app_ble_manager */
static const app_ble_profile_t s_gate_control_profile = {
    .name = "GATE_CONTROL",
    .profile_init = gate_control_profile_init,
    .profile_deinit = gate_control_profile_deinit,
    .on_sync = gate_control_on_sync,
    .on_reset = NULL,
    .gatts_register_cb = NULL,
    .start_adv = gate_control_start_adv,
    .stop_adv = gate_control_stop_adv,
};

esp_err_t app_ble_control_Init(void) {
    if (s_bIsRunning) {
        ESP_LOGW(TAG, "BLE Control đang chạy, bỏ qua Init");
        return ESP_OK;
    }

    esp_err_t ret = app_ble_manager_Init(&s_gate_control_profile);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Khởi tạo app_ble_manager cho Gate Control thất bại: %s", esp_err_to_name(ret));
        return ret;
    }

    s_bIsRunning = true;
    ESP_LOGI(TAG, "Khởi tạo BLE Control Profile thành công");
    return ESP_OK;
}

esp_err_t app_ble_control_Deinit(void) {
    if (!s_bIsRunning) {
        return ESP_OK;
    }

    s_bIsRunning = false;
    s_bIsConnected = false;
    s_conn_handle = 0;

    esp_err_t ret = app_ble_manager_Deinit();
    ESP_LOGI(TAG, "Hủy khởi tạo BLE Control thành công");
    return ret;
}

bool app_ble_control_IsRunning(void) {
    return s_bIsRunning && app_ble_manager_IsRunning();
}

bool app_ble_control_IsConnected(void) {
    return s_bIsConnected;
}
