#include "app_ble_mesh.h"
#include "app_ble_manager.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "nimble/nimble_port.h"
#include <string.h>

static const char *TAG = "APP_BLE_MESH";

static bool s_bMeshRunning = false;
static uint16_t s_u16ConnHandle = BLE_HS_CONN_HANDLE_NONE;

/* Handle của Characteristic 0x2ADE (Data Out) để gửi Notify */
static uint16_t s_u16ChrDataOutValHandle = 0;

/* ======================== DEFINE GATT SERVICES & UUIDs ======================== */

/* Mesh Proxy Service UUID: 0x1828 */
static const ble_uuid16_t s_sSvcUuid = BLE_UUID16_INIT(0x1828);

/* Mesh Proxy Data In (0x2ADD) - App WRITE NO RESPONSE */
static const ble_uuid16_t s_sChrDataInUuid = BLE_UUID16_INIT(0x2ADD);

/* Mesh Proxy Data Out (0x2ADE) - Device NOTIFY */
static const ble_uuid16_t s_sChrDataOutUuid = BLE_UUID16_INIT(0x2ADE);

/* Khai báo trước hàm GAP Event Callback */
static int ble_mesh_gap_event_cb(struct ble_gap_event *event, void *arg);

/* ======================== HÀM HỖ TRỢ ======================== */

static void get_device_name(char *out, size_t out_size)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BASE);
    (void)snprintf(out, out_size, "VCONNEX_MESH_%02X%02X", mac[4], mac[5]);
}

/* ======================== PUBLIC API: GỬI NOTIFICATION VỀ APP ======================== */

/**
 * @brief  Gửi dữ liệu thông báo (Notification) từ ESP32 về App qua Characteristic 0x2ADE
 */
esp_err_t app_ble_mesh_NotifyData(const uint8_t *pData, uint16_t u16Len)
{
    if (s_u16ConnHandle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "Chưa có kết nối BLE -> Bỏ qua gửi Notification");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_u16ChrDataOutValHandle == 0) {
        ESP_LOGE(TAG, "Handle Data Out (0x2ADE) chưa sẵn sàng");
        return ESP_FAIL;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(pData, u16Len);
    if (!om) {
        ESP_LOGE(TAG, "Cấp phát mbuf thất bại");
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(s_u16ConnHandle, s_u16ChrDataOutValHandle, om);
    if (rc == 0) {
        ESP_LOGI(TAG, ">>> Đã gửi Notification (%u bytes) qua 0x2ADE thành công", u16Len);
        ESP_LOG_BUFFER_HEX(TAG, pData, u16Len);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Gửi Notification qua 0x2ADE thất bại: rc=%d", rc);
        return ESP_FAIL;
    }
}

/* ======================== GATT ACCESS CALLBACKS ======================== */

/**
 * @brief Callback khi App ghi dữ liệu vào Mesh Proxy Data In (0x2ADD)
 */
static int app_ble_mesh_GattAccessDataIn(uint16_t conn_handle, uint16_t attr_handle,
                                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t u16Len = OS_MBUF_PKTLEN(ctxt->om);
        if (u16Len == 0) {
            return 0;
        }

        static uint8_t au8RxBuf[256];
        uint16_t u16CopiedLen = 0;

        if (u16Len >= sizeof(au8RxBuf)) {
            u16Len = sizeof(au8RxBuf) - 1;
        }

        int iRc = ble_hs_mbuf_to_flat(ctxt->om, au8RxBuf, u16Len, &u16CopiedLen);
        if (iRc == 0) {
            ESP_LOGI(TAG, "================ MESH PROXY DATA IN (0x2ADD) ================");
            ESP_LOGI(TAG, "Conn Handle: %d | Length: %u bytes", conn_handle, u16CopiedLen);
            ESP_LOG_BUFFER_HEX(TAG, au8RxBuf, u16CopiedLen);
            ESP_LOGI(TAG, "=============================================================");

            /* Tạm thời trả lời ACK để test */
            const char *pcAck = "OK";
            (void)app_ble_mesh_NotifyData((const uint8_t *)pcAck, strlen(pcAck));
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/**
 * @brief Callback cho Mesh Proxy Data Out (0x2ADE)
 */
static int app_ble_mesh_GattAccessDataOut(uint16_t conn_handle, uint16_t attr_handle,
                                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    /* Data Out chủ yếu dùng để Notify, ít khi App ghi vào */
    return 0;
}

/* Bảng định nghĩa GATT Services & Characteristics */
static const struct ble_gatt_svc_def g_asGattSvcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_sSvcUuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* 0x2ADD: Mesh Proxy Data In - App WRITE NO RESPONSE */
                .uuid = &s_sChrDataInUuid.u,
                .access_cb = app_ble_mesh_GattAccessDataIn,
                .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                /* 0x2ADE: Mesh Proxy Data Out - Device NOTIFY */
                .uuid = &s_sChrDataOutUuid.u,
                .access_cb = app_ble_mesh_GattAccessDataOut,
                .val_handle = &s_u16ChrDataOutValHandle,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 } // Kết thúc Characteristics
        },
    },
    { 0 } // Kết thúc Services
};

/* ======================== GAP EVENT CALLBACK ======================== */

static int ble_mesh_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {

        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                s_u16ConnHandle = event->connect.conn_handle;

                struct ble_gap_conn_desc desc;
                if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0) {
                    ESP_LOGI(TAG, ">>> Kết nối từ: %02X:%02X:%02X:%02X:%02X:%02X",
                             desc.peer_id_addr.val[5], desc.peer_id_addr.val[4],
                             desc.peer_id_addr.val[3], desc.peer_id_addr.val[2],
                             desc.peer_id_addr.val[1], desc.peer_id_addr.val[0]);
                }

                ESP_LOGI(TAG, ">>> BLE KẾT NỐI THÀNH CÔNG! Conn Handle: %d", s_u16ConnHandle);
            } else {
                ESP_LOGW(TAG, ">>> CÓ TÍN HIỆU KẾT NỐI NHƯNG THẤT BẠI! Status: %d → Bật lại Adv",
                         event->connect.status);
                s_u16ConnHandle = BLE_HS_CONN_HANDLE_NONE;
                (void)app_ble_manager_StartAdv();
            }
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, ">>> BLE ĐÃ NGẮT KẾT NỐI! Reason: %d. Khởi động lại Adv...",
                     event->disconnect.reason);
            s_u16ConnHandle = BLE_HS_CONN_HANDLE_NONE;
            (void)app_ble_manager_StartAdv();
            return 0;

        case BLE_GAP_EVENT_CONN_UPDATE: {
            ESP_LOGI(TAG, ">>> Connection Update: status=%d, conn_handle=%d",
                     event->conn_update.status,
                     event->conn_update.conn_handle);

            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->conn_update.conn_handle, &desc) == 0) {
                ESP_LOGI(TAG, "    → interval=%d, latency=%d, timeout=%d",
                         desc.conn_itvl,
                         desc.conn_latency,
                         desc.supervision_timeout);
            }
            return 0;
        }

        case BLE_GAP_EVENT_CONN_UPDATE_REQ:
            ESP_LOGI(TAG, ">>> Central yêu cầu cập nhật tham số kết nối");
            return 0;

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, ">>> MTU đã thay đổi: %d (conn_handle=%d)",
                     event->mtu.value, event->mtu.conn_handle);
            return 0;

        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI(TAG, ">>> APP SUBSCRIBE CCCD (Attr Handle: %d | Notify: %d | Indicate: %d)",
                     event->subscribe.attr_handle,
                     event->subscribe.cur_notify,
                     event->subscribe.cur_indicate);
            return 0;

        case BLE_GAP_EVENT_ENC_CHANGE:
            ESP_LOGI(TAG, ">>> Encryption thay đổi: status=%d, conn_handle=%d",
                     event->enc_change.status, event->enc_change.conn_handle);
            return 0;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ESP_LOGI(TAG, ">>> Advertising đã dừng (reason=%d)", event->adv_complete.reason);
            return 0;

        case BLE_GAP_EVENT_NOTIFY_TX:
            ESP_LOGI(TAG, ">>> Notification TX xong: status=%d, attr_handle=%d",
                     event->notify_tx.status, event->notify_tx.attr_handle);
            return 0;

        default:
            ESP_LOGI(TAG, ">>> GAP Event khác: type=%d", event->type);
            return 0;
    }
}

/* ======================== PROFILE CALLBACKS ======================== */

static esp_err_t ble_mesh_profile_start_adv(void)
{
    char device_name[32] = {0};
    get_device_name(device_name, sizeof(device_name));

    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;

    /* Gắn Mesh Proxy Service UUID 0x1828 vào Advertising */
    fields.uuids16 = (ble_uuid16_t *)&s_sSvcUuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
        return ESP_FAIL;
    }

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_mesh_gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Đang Advertising tên: %s (Mesh Proxy Service 0x1828)", device_name);
    return ESP_OK;
}

static esp_err_t ble_mesh_profile_stop_adv(void)
{
    (void)ble_gap_adv_stop();
    ESP_LOGI(TAG, "Đã dừng Advertising");
    return ESP_OK;
}

static esp_err_t ble_mesh_profile_init(void)
{
    /* Khởi tạo 2 service chuẩn của BLE */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    /* Đặt tên thiết bị cho Generic Access */
    char device_name[32] = {0};
    get_device_name(device_name, sizeof(device_name));
    ble_svc_gap_device_name_set(device_name);

    int rc = ble_gatts_count_cfg(g_asGattSvcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(g_asGattSvcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "BLE Mesh Profile Init (Mesh Proxy Service 0x1828 + GAP/GATT sẵn sàng)");
    return ESP_OK;
}

static esp_err_t ble_mesh_profile_deinit(void)
{
    (void)ble_mesh_profile_stop_adv();
    s_u16ConnHandle = BLE_HS_CONN_HANDLE_NONE;
    ESP_LOGI(TAG, "BLE Mesh Profile Deinit");
    return ESP_OK;
}

static void ble_mesh_on_sync(void)
{
    ESP_LOGI(TAG, "NimBLE Host đã sync → Bắt đầu Advertising");
    (void)ble_mesh_profile_start_adv();
}

/* ======================== PROFILE DEFINITION ======================== */

static const app_ble_profile_t s_sBleMeshProfile = {
    .name              = "BLE_MESH",
    .profile_init      = ble_mesh_profile_init,
    .profile_deinit    = ble_mesh_profile_deinit,
    .on_sync           = ble_mesh_on_sync,
    .on_reset          = NULL,
    .gatts_register_cb = NULL,
    .start_adv         = ble_mesh_profile_start_adv,
    .stop_adv          = ble_mesh_profile_stop_adv,
};

/* ======================== PUBLIC API ======================== */

esp_err_t app_ble_mesh_Init(void)
{
    if (s_bMeshRunning) {
        ESP_LOGW(TAG, "BLE Mesh đã đang chạy");
        return ESP_OK;
    }

    esp_err_t ret = app_ble_manager_Init(&s_sBleMeshProfile);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Khởi tạo BLE Manager thất bại: %s", esp_err_to_name(ret));
        return ret;
    }

    s_bMeshRunning = true;
    ESP_LOGI(TAG, "BLE Mesh (Mesh Proxy Service 0x1828) đã khởi động thành công");
    return ESP_OK;
}

esp_err_t app_ble_mesh_Deinit(void)
{
    if (!s_bMeshRunning) {
        return ESP_OK;
    }

    (void)app_ble_manager_Deinit();
    s_bMeshRunning = false;

    ESP_LOGI(TAG, "BLE Mesh đã tắt");
    return ESP_OK;
}

bool app_ble_mesh_IsRunning(void)
{
    return s_bMeshRunning;
}

esp_err_t app_ble_mesh_SendGateCommand(uint16_t dest_addr, uint8_t command)
{
    ESP_LOGW(TAG, "SendGateCommand chưa được triển khai");
    return ESP_ERR_NOT_SUPPORTED;
}