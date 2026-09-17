#include "app_ble_manager.h"
#include "esp_bt.h"
#include "esp_log.h"
#include "esp_mac.h"
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

static const app_ble_profile_t *s_pActiveProfile = NULL;
static bool s_bBleInited = false;

void ble_store_config_init(void);

static void ble_manager_on_reset(int reason) {
    ESP_LOGE(TAG, "NimBLE host reset, reason=%d", reason);
    if (s_pActiveProfile && s_pActiveProfile->on_reset) {
        s_pActiveProfile->on_reset(reason);
    }
}

static void ble_manager_on_sync(void) {
    ESP_LOGI(TAG, "NimBLE host đã đồng bộ. Kích hoạt profile: %s",
             (s_pActiveProfile && s_pActiveProfile->name) ? s_pActiveProfile->name : "UNKNOWN");
    if (s_pActiveProfile && s_pActiveProfile->on_sync) {
        s_pActiveProfile->on_sync();
    }
}

static void ble_manager_nimble_host_task(void *param) {
    ESP_LOGI(TAG, "NimBLE Host Task bắt đầu chạy");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t app_ble_manager_StartAdv(void) {
    if (s_pActiveProfile && s_pActiveProfile->start_adv) {
        return s_pActiveProfile->start_adv();
    }
    ESP_LOGW(TAG, "start_adv không được hỗ trợ trên profile hiện hành");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t app_ble_manager_StopAdv(void) {
    if (s_pActiveProfile && s_pActiveProfile->stop_adv) {
        return s_pActiveProfile->stop_adv();
    }
    ESP_LOGW(TAG, "stop_adv không được hỗ trợ trên profile hiện hành");
    return ESP_ERR_NOT_SUPPORTED;
}

bool app_ble_manager_IsRunning(void) {
    return s_bBleInited;
}

const app_ble_profile_t *app_ble_manager_GetActiveProfile(void) {
    return s_pActiveProfile;
}

esp_err_t app_ble_manager_Init(const app_ble_profile_t *p_profile) {
    if (p_profile == NULL) {
        ESP_LOGE(TAG, "Profile không được để trống (NULL)");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_bBleInited) {
        ESP_LOGW(TAG, "BLE Manager đã khởi tạo trước đó. Vui lòng Deinit trước khi Init lại!");
        return ESP_OK;
    }

    s_pActiveProfile = p_profile;
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
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_bt_controller_init thất bại: %s", esp_err_to_name(ret));
        s_pActiveProfile = NULL;
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_bt_controller_enable thất bại: %s", esp_err_to_name(ret));
        esp_bt_controller_deinit();
        s_pActiveProfile = NULL;
        return ret;
    }

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BASE);
    char device_name[30];
    snprintf(device_name, sizeof(device_name), "VCONNEX-%02X%02X", mac[4], mac[5]);

    ret = esp_nimble_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_nimble_init thất bại: %s", esp_err_to_name(ret));
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        s_pActiveProfile = NULL;
        return ret;
    }

    ble_hs_cfg.reset_cb = ble_manager_on_reset;
    ble_hs_cfg.sync_cb = ble_manager_on_sync;
    ble_hs_cfg.gatts_register_cb = s_pActiveProfile->gatts_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Khởi tạo GATT Server & callbacks riêng của Profile */
    if (s_pActiveProfile->profile_init) {
        ret = s_pActiveProfile->profile_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Khởi tạo profile '%s' thất bại: %s",
                     s_pActiveProfile->name ? s_pActiveProfile->name : "UNKNOWN",
                     esp_err_to_name(ret));
            esp_nimble_deinit();
            esp_bt_controller_disable();
            esp_bt_controller_deinit();
            s_pActiveProfile = NULL;
            return ret;
        }
    }

    int nimble_rc = ble_svc_gap_device_name_set(device_name);
    if (nimble_rc != 0) {
        ESP_LOGE(TAG, "ble_svc_gap_device_name_set thất bại: %d", nimble_rc);
        if (s_pActiveProfile->profile_deinit) {
            (void)s_pActiveProfile->profile_deinit();
        }
        esp_nimble_deinit();
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        s_pActiveProfile = NULL;
        return ESP_FAIL;
    }

    ble_store_config_init();

    ret = esp_nimble_enable(ble_manager_nimble_host_task);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_nimble_enable thất bại: %s", esp_err_to_name(ret));
        if (s_pActiveProfile->profile_deinit) {
            (void)s_pActiveProfile->profile_deinit();
        }
        esp_nimble_deinit();
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        s_pActiveProfile = NULL;
        return ret;
    }

    s_bBleInited = true;
    ESP_LOGI(TAG, "BLE Manager khởi tạo thành công với Profile: %s",
             s_pActiveProfile->name ? s_pActiveProfile->name : "UNKNOWN");
    return ESP_OK;
}

esp_err_t app_ble_manager_Deinit(void) {
    if (!s_bBleInited) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Đang tắt BLE Manager & giải phóng Profile: %s...",
             (s_pActiveProfile && s_pActiveProfile->name) ? s_pActiveProfile->name : "UNKNOWN");

    (void)nimble_port_stop();

    if (s_pActiveProfile && s_pActiveProfile->profile_deinit) {
        (void)s_pActiveProfile->profile_deinit();
    }

    esp_nimble_deinit();

    esp_bt_controller_disable();
    esp_bt_controller_deinit();

    s_pActiveProfile = NULL;
    s_bBleInited = false;

    ESP_LOGI(TAG, "Hủy khởi tạo BLE & Giải phóng tài nguyên phần cứng thành công");
    return ESP_OK;
}