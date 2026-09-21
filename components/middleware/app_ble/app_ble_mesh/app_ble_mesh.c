#include "app_ble_mesh.h"
#include "app_ble_manager.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "nimble/nimble_port.h"

static const char *TAG = "APP_BLE_MESH";

static bool s_bMeshRunning = false;

/* ======================== HÀM HỖ TRỢ ======================== */

static void get_device_name(char *out, size_t out_size)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BASE);
    (void)snprintf(out, out_size, "VCONNEX-%02X%02X", mac[4], mac[5]);
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

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, NULL, NULL);
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
    // Giai đoạn 1: chưa cần GATT service hay Mesh model
    ESP_LOGI(TAG, "BLE Mesh Profile Init (Advertising only)");
    return ESP_OK;
}

static esp_err_t ble_mesh_profile_deinit(void)
{
    (void)ble_mesh_profile_stop_adv();
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
    ESP_LOGI(TAG, "BLE Mesh (Advertising) đã khởi động thành công");
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
    // Giai đoạn 1 chưa hỗ trợ
    ESP_LOGW(TAG, "SendGateCommand chưa được hỗ trợ ở giai đoạn Advertising");
    return ESP_ERR_NOT_SUPPORTED;
}