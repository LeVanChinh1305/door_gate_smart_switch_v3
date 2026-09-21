#include "app_blufi.h"
#include "app_ble_manager.h"
#include "app_device_state.h"
#include "app_led_state.h"
#include "app_mqtt.h"
#include "app_nvs.h"
#include "app_sntp.h"
#include "app_wifi.h"

/* HEADER BẮT BUỘC CHO BLUFI API */
#include "esp_blufi.h"
#include "esp_blufi_api.h"

#include "cJSON.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Biến lưu SSID/pass tạm
static char g_cStaSsid[DF_BLUFI_STA_SSID_SIZE] = {0};
static char g_cStaPassword[DF_BLUFI_STA_PASSWORD_SIZE] = {0};

// Cờ trạng thái BluFi
static bool g_bBlufiActive = false;
static bool g_bBlufiBleConnected = false;
static bool g_bShutdownAfterDisconnect = false;

// Lưu device config tạm (dùng khi gửi response)
static app_nvs_device_config_t g_sCurrentDeviceConfig = {0};

static const char *TAG = "APP_BLUFI";

static void blufi_delayed_deinit_task(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(1000)); /* Chờ 1 giây cho BLE truyền xong response rồi restart */
    ESP_LOGI(TAG, "BluFi: Đã lưu cấu hình vào NVS -> Khởi động lại thiết bị...");
    esp_restart();
}

static void set_string_field(char *dest, size_t dest_size, const cJSON *item) {
    if ((dest == NULL) || (dest_size == 0U) || (item == NULL)) {
        return;
    }
    if (cJSON_IsString(item) && (item->valuestring != NULL)) {
        (void)snprintf(dest, dest_size, "%s", item->valuestring);
    }
}

static void set_number_as_string_field(char *dest, size_t dest_size, const cJSON *item) {
    if ((dest == NULL) || (dest_size == 0U) || (item == NULL)) {
        return;
    }

    if (cJSON_IsNumber(item)) {
        (void)snprintf(dest, dest_size, "%lld", (long long)item->valueint);
    } else if (cJSON_IsString(item) && (item->valuestring != NULL)) {
        (void)snprintf(dest, dest_size, "%s", item->valuestring);
    }
}

static void send_response_set_device_config(int32_t dev_t, const char *dev_ext_addr, int32_t error_code) {
    if (dev_ext_addr == NULL) {
        ESP_LOGE(TAG, "Địa chỉ MAC không hợp lệ");
        return;
    }

    cJSON *resp_root = cJSON_CreateObject();
    if (resp_root != NULL) {
        (void)cJSON_AddStringToObject(resp_root, "name", "CmdSetDeviceConfig");
        (void)cJSON_AddNumberToObject(resp_root, "devT", (double)dev_t);
        (void)cJSON_AddStringToObject(resp_root, "devExtAddr", dev_ext_addr);
        (void)cJSON_AddNumberToObject(resp_root, "errorCode", (double)error_code);
        (void)cJSON_AddStringToObject(resp_root, "software_version", "1.1.1");
        (void)cJSON_AddNumberToObject(resp_root, "timeStamp", (double)((uint32_t)time(NULL)));

        char *json_out = cJSON_PrintUnformatted(resp_root);
        if (json_out != NULL) {
            const size_t len = strlen(json_out);
            const esp_err_t err = esp_blufi_send_custom_data((uint8_t *)json_out, (uint32_t)len);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Phản hồi CmdSetDeviceConfig: %s", json_out);
            } else {
                ESP_LOGE(TAG, "Gửi phản hồi thất bại: %d", (int)err);
            }
            cJSON_free(json_out);
        }
        cJSON_Delete(resp_root);
    }
}

static void send_response_exit_configuration(int32_t dev_t, const char *dev_ext_addr, int32_t error_code) {
    if (dev_ext_addr == NULL) {
        ESP_LOGE(TAG, "Địa chỉ MAC không hợp lệ");
        return;
    }

    cJSON *resp_root = cJSON_CreateObject();
    if (resp_root != NULL) {
        (void)cJSON_AddStringToObject(resp_root, "name", "CmdExitConfiguration");
        (void)cJSON_AddNumberToObject(resp_root, "devT", (double)dev_t);
        (void)cJSON_AddStringToObject(resp_root, "devExtAddr", dev_ext_addr);
        (void)cJSON_AddNumberToObject(resp_root, "errorCode", (double)error_code);
        (void)cJSON_AddNumberToObject(resp_root, "timeStamp", (double)((uint32_t)time(NULL)));

        char *json_out = cJSON_PrintUnformatted(resp_root);
        if (json_out != NULL) {
            const size_t len = strlen(json_out);
            const esp_err_t err = esp_blufi_send_custom_data((uint8_t *)json_out, (uint32_t)len);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Phản hồi CmdExitConfiguration: %s", json_out);
            } else {
                ESP_LOGE(TAG, "Gửi phản hồi CmdExitConfiguration thất bại: %d", (int)err);
            }
            cJSON_free(json_out);
        }
        cJSON_Delete(resp_root);
    }
}

static void get_esp_dev_ext_addr(char *out, size_t out_size) {
    if ((out == NULL) || (out_size == 0U)) {
        return;
    }

    uint8_t mac[6] = {0};
    esp_err_t err = esp_read_mac(mac, ESP_MAC_BASE);
    if (err != ESP_OK) {
        (void)snprintf(out, out_size, "UNKNOWN");
        return;
    }

    (void)snprintf(out, out_size, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void send_response_get_device_id(int32_t dev_t, const char *dev_ext_addr) {
    if (dev_ext_addr == NULL) {
        ESP_LOGE(TAG, "Địa chỉ MAC không hợp lệ");
        return;
    }

    cJSON *resp_root = cJSON_CreateObject();
    if (resp_root != NULL) {
        (void)cJSON_AddStringToObject(resp_root, "name", "CmdGetDeviceID");
        (void)cJSON_AddNumberToObject(resp_root, "devT", (double)dev_t);
        (void)cJSON_AddStringToObject(resp_root, "devExtAddr", dev_ext_addr);
        (void)cJSON_AddNumberToObject(resp_root, "timeStamp", (double)((uint32_t)time(NULL)));

        char *json_out = cJSON_PrintUnformatted(resp_root);
        if (json_out != NULL) {
            const size_t len = strlen(json_out);
            const esp_err_t err = esp_blufi_send_custom_data((uint8_t *)json_out, (uint32_t)len);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Phản hồi CmdGetDeviceID: %s", json_out);
            } else {
                ESP_LOGE(TAG, "Gửi phản hồi CmdGetDeviceID thất bại: %d", (int)err);
            }
            cJSON_free(json_out);
        }
        cJSON_Delete(resp_root);
    }
}

static void send_success_followup_commands(int32_t dev_t) {
    char cDeviceAddress[DF_BLUFI_DEVICE_ADDRESS_SIZE] = {0};
    get_esp_dev_ext_addr(cDeviceAddress, sizeof(cDeviceAddress));

    ESP_LOGI(TAG, "Gửi CmdGetDeviceID và chờ App phản hồi CmdSetDeviceConfig");
    ESP_LOGI(TAG, "devExtAddr tự lấy từ MAC chip ESP: %s", cDeviceAddress);

    ESP_LOGI(TAG, "ESP gửi CmdGetDeviceID");
    send_response_get_device_id(dev_t, cDeviceAddress);
}

void app_blufi_ReportWifiStatus(bool is_connected) {
    esp_blufi_extra_info_t info;
    (void)memset(&info, 0, sizeof(esp_blufi_extra_info_t));

    if (is_connected) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            (void)memcpy(info.sta_bssid, ap_info.bssid, 6);
            info.sta_bssid_set = true;
            info.sta_ssid = (uint8_t *)g_cStaSsid;
            info.sta_ssid_len = (uint8_t)strlen(g_cStaSsid);
        }
        esp_err_t ret = esp_blufi_send_wifi_conn_report(WIFI_MODE_STA, ESP_BLUFI_STA_CONN_SUCCESS, 0, &info);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Đã gửi báo cáo Wi-Fi KẾT NỐI THÀNH CÔNG cho App");
            ESP_LOGI(TAG, "Gửi CmdGetDeviceID, chờ App gửi CmdSetDeviceConfig để lưu cấu hình");
            const int32_t i32DeviceType = (g_sCurrentDeviceConfig.dev_type != 0) ? g_sCurrentDeviceConfig.dev_type : DF_BLUFI_DEVICE_TYPE_DEFAULT;
            send_success_followup_commands(i32DeviceType);
        } else {
            ESP_LOGE(TAG, "Gửi báo cáo Wi-Fi thất bại: %s", esp_err_to_name(ret));
        }

    } else {
        (void)esp_blufi_send_wifi_conn_report(WIFI_MODE_STA, ESP_BLUFI_STA_CONN_FAIL, 0, &info);
        ESP_LOGE(TAG, "Đã gửi báo cáo Wi-Fi KẾT NỐI THẤT BẠI cho App");
    }
}

bool app_blufi_IsConnected(void) { 
    return g_bBlufiActive && g_bBlufiBleConnected; 
}

static void blufi_event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param) {
    switch (event) {
        case ESP_BLUFI_EVENT_INIT_FINISH:
            ESP_LOGI(TAG, "Khởi tạo BLUFI hoàn tất -> Bắt đầu phát quảng bá BLE");
            g_bBlufiActive = true;
            (void)app_ble_manager_StartAdv();
            break;

        case ESP_BLUFI_EVENT_DEINIT_FINISH:
            ESP_LOGI(TAG, "Hủy khởi tạo BLUFI hoàn tất");
            g_bBlufiActive = false;
            g_bBlufiBleConnected = false;
            break;

        case ESP_BLUFI_EVENT_BLE_CONNECT:
            g_bBlufiBleConnected = true;
            ESP_LOGI(TAG, "Thiết bị Bluetooth đã kết nối -> Dừng phát quảng bá BLE");
            (void)app_ble_manager_StopAdv();
            break;

        case ESP_BLUFI_EVENT_BLE_DISCONNECT:
            g_bBlufiBleConnected = false;
            if (g_bShutdownAfterDisconnect) {
                g_bShutdownAfterDisconnect = false;
                ESP_LOGI(TAG, "Đã nhận disconnect sau CmdSetDeviceConfig -> Tắt toàn bộ BLUFI");
                (void)app_blufi_Deinit();
            } else if (g_bBlufiActive) {
                ESP_LOGI(TAG, "Thiết bị Bluetooth đã ngắt kết nối -> Bắt đầu phát lại quảng bá BLE");
                (void)app_ble_manager_StartAdv();
            }
            break;

        case ESP_BLUFI_EVENT_SET_WIFI_OPMODE:
            ESP_LOGI(TAG, "BLUFI yêu cầu cài đặt chế độ Wi-Fi: %d", param->wifi_mode.op_mode);
            esp_wifi_set_mode(param->wifi_mode.op_mode);
            break;

        case ESP_BLUFI_EVENT_RECV_STA_SSID:
            memset(g_cStaSsid, 0, sizeof(g_cStaSsid));
            strncpy(g_cStaSsid, (char *)param->sta_ssid.ssid, param->sta_ssid.ssid_len);
            ESP_LOGI(TAG, "Đã nhận tên Wi-Fi (SSID): %s", g_cStaSsid);
            break;

        case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
            memset(g_cStaPassword, 0, sizeof(g_cStaPassword));
            strncpy(g_cStaPassword, (char *)param->sta_passwd.passwd, param->sta_passwd.passwd_len);
            ESP_LOGI(TAG, "Đã nhận Mật khẩu Wi-Fi: %s", g_cStaPassword);
            break;

        case ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP: {
            ESP_LOGI(TAG, "BLUFI yêu cầu kết nối tới điểm truy cập Wi-Fi (AP)...");

            if (app_wifi_IsConnected()) {
                ESP_LOGI(TAG, "Wi-Fi đã ở trạng thái kết nối, gửi lại báo cáo cho App...");
                app_blufi_ReportWifiStatus(true);
                break;
            }

            wifi_config_t sta_config = {0};
            (void)strncpy((char *)sta_config.sta.ssid, g_cStaSsid, sizeof(sta_config.sta.ssid) - 1U);
            (void)strncpy((char *)sta_config.sta.password, g_cStaPassword, sizeof(sta_config.sta.password) - 1U);
            sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

            esp_err_t eClearErr = app_nvs_ClearWifiConfig();
            if ((eClearErr == ESP_OK) || (eClearErr == ESP_ERR_NOT_FOUND)) {
                (void)app_nvs_SaveWifiConfig(&sta_config);
                (void)app_nvs_SetProvisionedWifiConfig(true);
            } else {
                ESP_LOGW(TAG, "Xóa cấu hình Wi-Fi cũ thất bại, mã lỗi=%s", esp_err_to_name(eClearErr));
            }

            (void)esp_wifi_set_mode(WIFI_MODE_STA);
            (void)esp_wifi_set_config(WIFI_IF_STA, &sta_config);
            (void)esp_wifi_start();
            (void)esp_wifi_connect();
            break;
        }

        case ESP_BLUFI_EVENT_GET_WIFI_STATUS: {
            wifi_mode_t mode;
            esp_blufi_extra_info_t info;
            memset(&info, 0, sizeof(esp_blufi_extra_info_t));
            esp_wifi_get_mode(&mode);

            if (app_wifi_IsConnected()) {
                esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, &info);
            } else {
                esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, 0, &info);
            }
            ESP_LOGI(TAG, "Đã gửi phản hồi trạng thái Wi-Fi qua BLUFI");
            break;
        }

        case ESP_BLUFI_EVENT_RECV_CUSTOM_DATA: {
            if ((param->custom_data.data == NULL) || (param->custom_data.data_len == 0U) || (param->custom_data.data_len >= DF_BLUFI_CUSTOM_DATA_BUFFER_SIZE)) {
                ESP_LOGE(TAG, "Dữ liệu tùy chỉnh không hợp lệ");
                break;
            }

            static char cJsonBuffer[DF_BLUFI_CUSTOM_DATA_BUFFER_SIZE];
            (void)memset(cJsonBuffer, 0, sizeof(cJsonBuffer));
            (void)memcpy(cJsonBuffer, param->custom_data.data, (size_t)param->custom_data.data_len);
            cJsonBuffer[param->custom_data.data_len] = '\0';
            ESP_LOGI(TAG, "Đã nhận dữ liệu tùy chỉnh: %s", cJsonBuffer);

            cJSON *root = cJSON_Parse(cJsonBuffer);
            if (root != NULL) {
                const cJSON *cmd_name = cJSON_GetObjectItem(root, "name");
                const cJSON *value = cJSON_GetObjectItem(root, "value");

                if ((cmd_name != NULL) && cJSON_IsString(cmd_name) && (cmd_name->valuestring != NULL)) {
                    int32_t dev_t = 0;
                    char cDeviceAddress[DF_BLUFI_DEVICE_ADDRESS_SIZE] = {0};

                    if (value != NULL) {
                        const cJSON *js_dev_t = cJSON_GetObjectItem(value, "devT");
                        if (cJSON_IsNumber(js_dev_t)) {
                            dev_t = js_dev_t->valueint;
                        }

                        const cJSON *js_dev_addr = cJSON_GetObjectItem(value, "devExtAddr");
                        if (cJSON_IsString(js_dev_addr) && (js_dev_addr->valuestring != NULL)) {
                            (void)strncpy(cDeviceAddress, js_dev_addr->valuestring, sizeof(cDeviceAddress) - 1U);
                        }
                    }

                    if (strcmp(cmd_name->valuestring, "CmdGetDeviceID") == 0) {
                        int32_t req_dev_t = (dev_t != 0) ? dev_t : DF_BLUFI_DEVICE_TYPE_DEFAULT;
                        char cRequestedDeviceAddress[DF_BLUFI_DEVICE_ADDRESS_SIZE] = {0};

                        if (value != NULL) {
                            const cJSON *js_dev_t = cJSON_GetObjectItem(value, "devT");
                            if (cJSON_IsNumber(js_dev_t)) {
                                req_dev_t = js_dev_t->valueint;
                            }

                            const cJSON *js_dev_addr = cJSON_GetObjectItem(value, "devExtAddr");
                            if (cJSON_IsString(js_dev_addr) && (js_dev_addr->valuestring != NULL)) {
                                (void)snprintf(cRequestedDeviceAddress, sizeof(cRequestedDeviceAddress), "%s", js_dev_addr->valuestring);
                            }
                        }

                        if (cRequestedDeviceAddress[0] == '\0') {
                            uint8_t mac[6];
                            esp_read_mac(mac, ESP_MAC_BASE);
                            (void)snprintf(cRequestedDeviceAddress, sizeof(cRequestedDeviceAddress), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                        }

                        send_response_get_device_id(req_dev_t, cRequestedDeviceAddress);
                    }

                    // XỬ LÝ LỆNH 1: CmdSetDeviceConfig
                    else if (strcmp(cmd_name->valuestring, "CmdSetDeviceConfig") == 0) {
                        int32_t status_code = 50000;

                        if (value != NULL) {
                            (void)memset(&g_sCurrentDeviceConfig, 0, sizeof(g_sCurrentDeviceConfig));
                            (void)app_nvs_LoadDeviceConfig(&g_sCurrentDeviceConfig);

                            const cJSON *js_dev_t = cJSON_GetObjectItem(value, "devT");
                            if (cJSON_IsNumber(js_dev_t)) {
                                g_sCurrentDeviceConfig.dev_type = js_dev_t->valueint;
                                dev_t = js_dev_t->valueint;
                            } else {
                                g_sCurrentDeviceConfig.dev_type = DF_BLUFI_DEVICE_TYPE_DEFAULT;
                                dev_t = DF_BLUFI_DEVICE_TYPE_DEFAULT;
                            }

                            const cJSON *js_dev_addr = cJSON_GetObjectItem(value, "devExtAddr");
                            if (cJSON_IsString(js_dev_addr) && (js_dev_addr->valuestring != NULL)) {
                                (void)snprintf(g_sCurrentDeviceConfig.dev_ext_addr, sizeof(g_sCurrentDeviceConfig.dev_ext_addr), "%s", js_dev_addr->valuestring);
                            }

                            const cJSON *broker = cJSON_GetObjectItem(value, "broker");
                            set_string_field(g_sCurrentDeviceConfig.broker, sizeof(g_sCurrentDeviceConfig.broker), broker);

                            const cJSON *username = cJSON_GetObjectItem(value, "username");
                            set_string_field(g_sCurrentDeviceConfig.username, sizeof(g_sCurrentDeviceConfig.username), username);

                            const cJSON *password = cJSON_GetObjectItem(value, "password");
                            set_string_field(g_sCurrentDeviceConfig.password, sizeof(g_sCurrentDeviceConfig.password), password);

                            const cJSON *mqtt_sub = cJSON_GetObjectItem(value, "mqttsub");
                            set_string_field(g_sCurrentDeviceConfig.mqtt_sub, sizeof(g_sCurrentDeviceConfig.mqtt_sub), mqtt_sub);

                            const cJSON *mqtt_pub = cJSON_GetObjectItem(value, "mqttpub");
                            set_string_field(g_sCurrentDeviceConfig.mqtt_pub, sizeof(g_sCurrentDeviceConfig.mqtt_pub), mqtt_pub);

                            const cJSON *mqtt_alert = cJSON_GetObjectItem(value, "mqttalert");
                            set_string_field(g_sCurrentDeviceConfig.mqtt_alert, sizeof(g_sCurrentDeviceConfig.mqtt_alert), mqtt_alert);

                            const cJSON *force_ota_url = cJSON_GetObjectItem(value, "forceOtaUrl");
                            set_string_field(g_sCurrentDeviceConfig.force_ota_url, sizeof(g_sCurrentDeviceConfig.force_ota_url), force_ota_url);

                            const cJSON *be_shared_key = cJSON_GetObjectItem(value, "beSharedKey");
                            set_string_field(g_sCurrentDeviceConfig.be_shared_key, sizeof(g_sCurrentDeviceConfig.be_shared_key), be_shared_key);

                            const cJSON *api_url = cJSON_GetObjectItem(value, "apiUrl");
                            set_string_field(g_sCurrentDeviceConfig.api_url, sizeof(g_sCurrentDeviceConfig.api_url), api_url);

                            const cJSON *api_secret_key = cJSON_GetObjectItem(value, "apiSecretKey");
                            set_string_field(g_sCurrentDeviceConfig.api_secret_key, sizeof(g_sCurrentDeviceConfig.api_secret_key), api_secret_key);

                            const cJSON *user_id = cJSON_GetObjectItem(value, "userId");
                            set_number_as_string_field(g_sCurrentDeviceConfig.user_id, sizeof(g_sCurrentDeviceConfig.user_id), user_id);

                            if ((g_sCurrentDeviceConfig.dev_type == 0) || (g_sCurrentDeviceConfig.dev_ext_addr[0] == '\0')) {
                                g_sCurrentDeviceConfig.dev_type = DF_BLUFI_DEVICE_TYPE_DEFAULT;
                                status_code = 50004;
                            }
                            if (status_code == 50000) {
                                const esp_err_t eErr = app_nvs_SaveDeviceConfig(&g_sCurrentDeviceConfig);
                                if (eErr != ESP_OK) {
                                    status_code = 50005; /* Lỗi lưu NVS */
                                }
                            }
                        } else {
                            status_code = 50004;
                        }

                        send_response_set_device_config(dev_t, cDeviceAddress, status_code);
                        if (status_code == 50000) {
                            if (value != NULL) {
                                const cJSON *js_timestamp = cJSON_GetObjectItem(value, "timeStamp");
                                if (cJSON_IsNumber(js_timestamp) && (js_timestamp->valueint > 1700000000)) {
                                    app_sntp_SetSystemTime((int64_t)js_timestamp->valueint);
                                    ESP_LOGI(TAG, "=> TẦNG 1: Đã nạp thành công giờ từ App cho RTC: %lld", (long long)js_timestamp->valueint);
                                }
                            }
                            ESP_LOGI(TAG, "Đã lưu CmdSetDeviceConfig vào NVS -> Khởi động lại sau 1s...");
                            xTaskCreate(blufi_delayed_deinit_task, "blufi_deinit_task", 2048, NULL, 5, NULL);
                        }
                    }

                    // XỬ LÝ LỆNH 2: CmdExitConfiguration
                    else if (strcmp(cmd_name->valuestring, "CmdExitConfiguration") == 0) {
                        ESP_LOGI(TAG, "Nhận lệnh CmdExitConfiguration -> Thoát cấu hình BluFi");

                        if (cDeviceAddress[0] == '\0') {
                            get_esp_dev_ext_addr(cDeviceAddress, sizeof(cDeviceAddress));
                        }
                        if (dev_t == 0) {
                            dev_t = (g_sCurrentDeviceConfig.dev_type != 0) ? g_sCurrentDeviceConfig.dev_type : DF_BLUFI_DEVICE_TYPE_DEFAULT;
                        }

                        send_response_exit_configuration(dev_t, cDeviceAddress, 50000);

                        ESP_LOGI(TAG, "CmdExitConfiguration -> Khởi động lại sau 1s...");
                        xTaskCreate(blufi_delayed_deinit_task, "blufi_deinit_task", 2048, NULL, 5, NULL);
                    }

                    else {
                        ESP_LOGW(TAG, "Lệnh không hợp lệ: %s", cmd_name->valuestring);
                    }
                }
                cJSON_Delete(root);
            } else {
                ESP_LOGE(TAG, "Giải mã JSON thất bại");
            }
            break;
        }

        case ESP_BLUFI_EVENT_RECV_SLAVE_DISCONNECT_BLE:
            ESP_LOGI(TAG, "Yêu cầu ngắt kết nối BLE từ phía Slave -> Tắt toàn bộ BLUFI");
            g_bShutdownAfterDisconnect = true;
            (void)esp_blufi_disconnect();
            break;

        default:
            break;
    }
}

static esp_blufi_callbacks_t s_sBlufiCallbacks = {
    .event_cb = blufi_event_callback,
    .negotiate_data_handler = NULL,
    .encrypt_func = NULL,
    .decrypt_func = NULL,
    .checksum_func = NULL,
};

static esp_err_t blufi_profile_init(void) {
    esp_err_t ret = esp_blufi_register_callbacks(&s_sBlufiCallbacks);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_blufi_register_callbacks thất bại: %s", esp_err_to_name(ret));
        return ret;
    }

    int rc = esp_blufi_gatt_svr_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "esp_blufi_gatt_svr_init thất bại: %d", rc);
        return ESP_FAIL;
    }

    esp_blufi_btc_init();
    return ESP_OK;
}

static esp_err_t blufi_profile_deinit(void) {
    esp_blufi_gatt_svr_deinit();
    esp_blufi_profile_deinit();
    esp_blufi_btc_deinit();
    return ESP_OK;
}

static esp_err_t blufi_profile_start_adv(void) {
    esp_blufi_adv_start();
    return ESP_OK;
}

static esp_err_t blufi_profile_stop_adv(void) {
    esp_blufi_adv_stop();
    return ESP_OK;
}

static void blufi_profile_on_sync(void) {
    (void)esp_blufi_profile_init();
}

static const app_ble_profile_t s_sBlufiProfile = {
    .name = "BLUFI",
    .profile_init = blufi_profile_init,
    .profile_deinit = blufi_profile_deinit,
    .on_sync = blufi_profile_on_sync,
    .on_reset = NULL,
    .gatts_register_cb = esp_blufi_gatt_svr_register_cb,
    .start_adv = blufi_profile_start_adv,
    .stop_adv = blufi_profile_stop_adv,
};

esp_err_t app_blufi_Init(void) {
    app_led_state_SetState(E_LED_STATE_BLUFI_AUTO);

    esp_err_t ret = app_ble_manager_Init(&s_sBlufiProfile);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Khởi tạo BLE Manager cho BluFi thất bại: %s", esp_err_to_name(ret));
        return ret;
    }

    g_bBlufiActive = true;
    return ESP_OK;
}

esp_err_t app_blufi_Deinit(void) {
    if (!g_bBlufiActive) {
        return ESP_OK;
    }
    g_bShutdownAfterDisconnect = false;
    g_bBlufiActive = false;
    g_bBlufiBleConnected = false;

    (void)app_ble_manager_Deinit();

    ESP_LOGI(TAG, "Hủy khởi tạo BLUFI thành công");

    if (g_sCurrentDeviceConfig.broker[0] == '\0') {
        (void)app_nvs_LoadDeviceConfig(&g_sCurrentDeviceConfig);
    }
    if (g_sCurrentDeviceConfig.broker[0] != '\0') {
        ESP_LOGI(TAG, "Đang khởi tạo MQTT sau khi tắt Bluetooth...");
        app_mqtt_StartInit(&g_sCurrentDeviceConfig);
    }

    return ESP_OK;
}