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

/* ======================== DEFINE GATT SERVICES & UUIDs ======================== */

/* Service UUID: 0xFFFF | Characteristic Write UUID: 0xFF01 */
static const ble_uuid16_t s_sSvcUuid = BLE_UUID16_INIT(0xFFFF);
static const ble_uuid16_t s_sChrWriteUuid = BLE_UUID16_INIT(0xFF01);

/* Khai báo trước hàm GAP Event Callback */
static int ble_mesh_gap_event_cb(struct ble_gap_event *event, void *arg);

/* ======================== HÀM HỖ TRỢ ======================== */

static void get_device_name(char *out, size_t out_size)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BASE);
    (void)snprintf(out, out_size, "VCONNEX_MESH_%02X%02X", mac[4], mac[5]);
}

/* ======================== GATT ACCESS CALLBACK ======================== */

/**
 * @brief Callback xử lý khi App (nRF Connect) gửi lệnh Write xuống Characteristic 0xFF01
 */
static int app_ble_mesh_GattAccessCb(uint16_t conn_handle, uint16_t attr_handle,
                                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_WRITE_CHR: {
            uint16_t u16Len = OS_MBUF_PKTLEN(ctxt->om);
            if (u16Len == 0) {
                return 0;
            }

            /* Dùng static buffer để nhận dữ liệu, tránh ngốn Stack của NimBLE Task */
            static uint8_t au8RxBuf[256];
            uint16_t u16CopiedLen = 0;

            if (u16Len >= sizeof(au8RxBuf)) {
                u16Len = sizeof(au8RxBuf) - 1;
            }

            int iRc = ble_hs_mbuf_to_flat(ctxt->om, au8RxBuf, u16Len, &u16CopiedLen);
            if (iRc == 0) {
                au8RxBuf[u16CopiedLen] = '\0'; // Kết thúc chuỗi C

                ESP_LOGI(TAG, "================ BLE DATA RECEIVED ================");
                ESP_LOGI(TAG, "Conn Handle: %d | Length: %u bytes", conn_handle, u16CopiedLen);
                
                /* Log dạng String ASCII */
                ESP_LOGI(TAG, "Data String: %s", (char *)au8RxBuf);

                /* Log dạng Hex Bytes */
                ESP_LOG_BUFFER_HEX(TAG, au8RxBuf, u16CopiedLen);
                ESP_LOGI(TAG, "===================================================");
            }
            return 0;
        }

        default:
            return BLE_ATT_ERR_UNLIKELY;
    }
}

/* Bảng định nghĩa GATT Service */
static const struct ble_gatt_svc_def g_asGattSvcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_sSvcUuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_sChrWriteUuid.u,
                .access_cb = app_ble_mesh_GattAccessCb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            { 0 } // Kết thúc danh sách Characteristics
        },
    },
    { 0 } // Kết thúc danh sách Services
};

/* ======================== GAP EVENT CALLBACK ======================== */

static int ble_mesh_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                s_u16ConnHandle = event->connect.conn_handle;
                ESP_LOGI(TAG, ">>> BLE KẾT NỐI THÀNH CÔNG! Conn Handle: %d", s_u16ConnHandle);
            } else {
                ESP_LOGW(TAG, "Kết nối BLE thất bại, bật lại Adv... Status: %d", event->connect.status);
                (void)app_ble_manager_StartAdv();
            }
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, ">>> BLE ĐÃ NGẮT KẾT NỐI! Lý do: %d. Khởi động lại Advertising...", event->disconnect.reason);
            s_u16ConnHandle = BLE_HS_CONN_HANDLE_NONE;
            (void)app_ble_manager_StartAdv();
            return 0;

        default:
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

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
        return ESP_FAIL;
    }

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;   // Undirected connectable
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;   // General discoverable

    /* Đăng ký ble_mesh_gap_event_cb để hứng sự kiện Connect/Disconnect */
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_mesh_gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Đang Advertising tên: %s", device_name);
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
    /* Đăng ký GATT Services với NimBLE Host */
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

    ESP_LOGI(TAG, "BLE Mesh Profile Init (GATT Server Ready)");
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
    ESP_LOGI(TAG, "BLE Mesh (GATT Server & Adv) đã khởi động thành công");
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