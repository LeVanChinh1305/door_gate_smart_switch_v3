#include "app_ble_manager.h"
#include "esp_bt.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_blufi.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "APP_BLE_MANAGER";

#if SOC_BT_CLASSIC_SUPPORTED
static bool s_bClassicBtMemReleased = false;
#endif

static app_ble_manager_blufi_event_cb_t g_pfnBlufiEventCb = NULL;

void ble_store_config_init(void);

static void ble_manager_on_reset(int reason) {
    ESP_LOGE(TAG, "NimBLE host reset, reason=%d", reason);
}

static void ble_manager_on_sync(void) {
    ESP_LOGI(TAG, "NimBLE host đã đồng bộ -> Khởi tạo BluFi Profile");
    esp_blufi_profile_init();
}

static void ble_manager_nimble_host_task(void *param) {
    ESP_LOGI(TAG, "NimBLE Host Task bắt đầu chạy");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void app_ble_manager_RegisterBlufiCallback(app_ble_manager_blufi_event_cb_t cb) {
    g_pfnBlufiEventCb = cb;
}

static void ble_manager_blufi_raw_event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param) {
    if (g_pfnBlufiEventCb != NULL) {
        g_pfnBlufiEventCb((int)event, (void *)param);
    }
}

static esp_blufi_callbacks_t s_sBlufiCallbacks = {
    .event_cb = ble_manager_blufi_raw_event_callback,
    .negotiate_data_handler = NULL,
    .encrypt_func = NULL,
    .decrypt_func = NULL,
    .checksum_func = NULL,
};

esp_err_t app_ble_manager_StartAdv(void) {
    esp_blufi_adv_start();
    return ESP_OK;
}

esp_err_t app_ble_manager_StopAdv(void) {
    esp_blufi_adv_stop();
    return ESP_OK;
}

esp_err_t app_ble_manager_Init(void) {
    esp_err_t ret;

#if SOC_BT_CLASSIC_SUPPORTED
    if (!s_bClassicBtMemReleased) {
        esp_err_t eReleaseErr = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
        if (eReleaseErr == ESP_OK) {
            s_bClassicBtMemReleased = true;
            ESP_LOGI(TAG, "Đã thu hồi RAM từ Classic BT!");
        }
    }
#endif

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) return ret;

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) return ret;

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BASE);
    char device_name[30];
    snprintf(device_name, sizeof(device_name), "VCONNEX-%02X%02X", mac[4], mac[5]);

    ret = esp_nimble_init();
    if (ret != ESP_OK) return ret;

    ble_hs_cfg.reset_cb = ble_manager_on_reset;
    ble_hs_cfg.sync_cb = ble_manager_on_sync;
    ble_hs_cfg.gatts_register_cb = esp_blufi_gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ret = esp_blufi_register_callbacks(&s_sBlufiCallbacks);
    if (ret != ESP_OK) return ret;

    int nimble_rc = esp_blufi_gatt_svr_init();
    if (nimble_rc != 0) return ESP_FAIL;

    nimble_rc = ble_svc_gap_device_name_set(device_name);
    if (nimble_rc != 0) return ESP_FAIL;

    ble_store_config_init();
    esp_blufi_btc_init();

    ret = esp_nimble_enable(ble_manager_nimble_host_task);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

esp_err_t app_ble_manager_Deinit(void) {
    (void)nimble_port_stop();
    esp_blufi_gatt_svr_deinit();
    esp_nimble_deinit();
    esp_blufi_profile_deinit();
    esp_blufi_btc_deinit();

    esp_bt_controller_disable();
    esp_bt_controller_deinit();

    ESP_LOGI(TAG, "Hủy khởi tạo BLE & Giải phóng tài nguyên phần cứng thành công");
    return ESP_OK;
}