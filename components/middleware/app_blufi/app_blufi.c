#include "cJSON.h"
#include "esp_app_format.h"
#include "esp_blufi.h"
#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_blufi.h"
#include "app_device_state.h"
#include "app_led_state.h"
#include "app_mqtt.h"
#include "app_nvs.h"
#include "app_wifi.h"
#include "app_sntp.h"

// Biến lưu SSID/pass tạm
static char g_cStaSsid[DF_BLUFI_STA_SSID_SIZE] = {0};
static char g_cStaPassword[DF_BLUFI_STA_PASSWORD_SIZE] = {0};

// Cờ trạng thái BluFi
static bool g_bBlufiActive = false;
static bool g_bBlufiBleConnected = false;
static bool g_bShutdownAfterDisconnect = false;

// Để lưu device config tạm (dùng khi gửi response)
static app_nvs_device_config_t g_sCurrentDeviceConfig = {0};

static const char *TAG = "APP_BLUFI";

// // Tham số cấu hình quảng bá BLE
static esp_ble_adv_params_t blufi_adv_params = {
    .adv_int_min = 0x20, // 20ms
    .adv_int_max = 0x40, // 40ms
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// Dữ liệu gói tin quảng bá (Adv Data)
static esp_ble_adv_data_t blufi_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006,
    .max_interval = 0x0010,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 0,
    .p_service_uuid = NULL,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static void blufi_delayed_deinit_task(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(1000)); /* Chờ 1 giây cho BLE truyền xong response */
    ESP_LOGI(TAG, "Timer hết hạn -> Kích hoạt Deinit BluFi tự động");
    (void)app_blufi_Deinit();
    vTaskDelete(NULL);
}

static void blufi_event_callback(esp_blufi_cb_event_t event,
                                 esp_blufi_cb_param_t *param);

// Cấu trúc đăng ký Callback cho BluFi
static esp_blufi_callbacks_t example_callbacks = {
    .event_cb = blufi_event_callback,
    .negotiate_data_handler = NULL,
    .encrypt_func = NULL,
    .decrypt_func = NULL,
    .checksum_func = NULL,
};

//
void app_blufi_gap_event_handler(esp_gap_ble_cb_event_t event,
                                 esp_ble_gap_cb_param_t *param) {
  switch (event) {
  case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
    ESP_LOGI(TAG, "Cấu hình dữ liệu quảng bá thành công -> Bắt đầu phát quảng bá!");
    esp_ble_gap_start_advertising(&blufi_adv_params);
    break;

  case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
    if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
      ESP_LOGE(TAG, "Phát quảng bá thất bại");
    } else {
      ESP_LOGI(TAG, "Phát quảng bá thành công");
    }
    break;
  default:
    break;
  }
}

void esp_blufi_gatt_event_handler(esp_gatts_cb_event_t event,
                                  esp_gatt_if_t gatts_if,
                                  esp_ble_gatts_cb_param_t *param) {
  switch (event) {
  case ESP_GATTS_REG_EVT:
    if (param->reg.status == ESP_GATT_OK) {
      esp_blufi_profile_init();
    } else {
      ESP_LOGE(TAG, "Đăng ký ứng dụng GATT thất bại, mã app_id %04x, trạng thái %d", param->reg.app_id, param->reg.status);
    }
    break;
  default:
    break;
  }
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
    (void)cJSON_AddStringToObject( resp_root, "software_version","1.1.1"); // Trường đặc thụ của SetDeviceConfig
    (void)cJSON_AddNumberToObject(resp_root, "timeStamp", (double)((uint32_t)time(NULL)));

    char *json_out = cJSON_PrintUnformatted(resp_root);
    if (json_out != NULL) {
      const size_t len = strlen(json_out);
      const esp_err_t err =
          esp_blufi_send_custom_data((uint8_t *)json_out, (uint32_t)len);
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

  (void)snprintf(out, out_size, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1],
                 mac[2], mac[3], mac[4], mac[5]);
}

static void send_response_get_device_id(int32_t dev_t,
                                        const char *dev_ext_addr) {
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
      const esp_err_t err =
          esp_blufi_send_custom_data((uint8_t *)json_out, (uint32_t)len);
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
      // Điền BSSID và SSID len để App di động xác nhận chính xác
      (void)memcpy(info.sta_bssid, ap_info.bssid, 6);
      info.sta_bssid_set = true;
      info.sta_ssid = (uint8_t *)g_cStaSsid;
      info.sta_ssid_len = (uint8_t)strlen(g_cStaSsid);
    }
    // Gửi báo cáo thành công về cho App di động
    esp_err_t ret = esp_blufi_send_wifi_conn_report(
        WIFI_MODE_STA, ESP_BLUFI_STA_CONN_SUCCESS, 0, &info);
    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "Đã gửi báo cáo Wi-Fi KẾT NỐI THÀNH CÔNG cho App");
      ESP_LOGI( TAG, "Gửi CmdGetDeviceID, chờ App gửi CmdSetDeviceConfig để lưu cấu hình");
      const int32_t i32DeviceType = (g_sCurrentDeviceConfig.dev_type != 0)
                                        ? g_sCurrentDeviceConfig.dev_type
                                        : DF_BLUFI_DEVICE_TYPE_DEFAULT;
      send_success_followup_commands(i32DeviceType);
    } else {
      ESP_LOGE(TAG, "Gửi báo cáo Wi-Fi thất bại: %s", esp_err_to_name(ret));
    }

  } else {
    // Gửi báo cáo thất bại
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
    ESP_LOGI(TAG,"Khởi tạo BLUFI hoàn tất -> Đang cấu hình dữ liệu quảng bá...");
    g_bBlufiActive = true;
    esp_ble_gap_config_adv_data(&blufi_adv_data);
    break;

  case ESP_BLUFI_EVENT_DEINIT_FINISH:
    ESP_LOGI(TAG, "Hủy khởi tạo BLUFI hoàn tất");
    g_bBlufiActive = false;
    g_bBlufiBleConnected = false;
    break;

  case ESP_BLUFI_EVENT_BLE_CONNECT:
    g_bBlufiBleConnected = true;
    ESP_LOGI(TAG, "Thiết bị Bluetooth đã kết nối -> Dừng phát quảng bá BLE");
    esp_ble_gap_stop_advertising();
    break;

  case ESP_BLUFI_EVENT_BLE_DISCONNECT:
    g_bBlufiBleConnected = false;
    if (g_bShutdownAfterDisconnect) {
      g_bShutdownAfterDisconnect = false;
      ESP_LOGI(TAG,"Đã nhận disconnect sau CmdSetDeviceConfig -> Tắt toàn bộ BLUFI");
      (void)app_blufi_Deinit();
    } else if (g_bBlufiActive) {
      ESP_LOGI(TAG, "Thiết bị Bluetooth đã ngắt kết nối -> Bắt đầu phát lại quảng bá BLE");
      ESP_LOGI(TAG, "Bắt đầu phát lại quảng bá BLE...");
      esp_ble_gap_start_advertising(&blufi_adv_params);
    }
    break;

  case ESP_BLUFI_EVENT_SET_WIFI_OPMODE:
    ESP_LOGI(TAG, "BLUFI yêu cầu cài đặt chế độ Wi-Fi: %d",
             param->wifi_mode.op_mode);
    esp_wifi_set_mode(param->wifi_mode.op_mode);
    break;

  case ESP_BLUFI_EVENT_RECV_STA_SSID:
    memset(g_cStaSsid, 0, sizeof(g_cStaSsid));
    strncpy(g_cStaSsid, (char *)param->sta_ssid.ssid, param->sta_ssid.ssid_len);
    ESP_LOGI(TAG, "Đã nhận tên Wi-Fi (SSID): %s", g_cStaSsid);
    break;

  case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
    memset(g_cStaPassword, 0, sizeof(g_cStaPassword));
    strncpy(g_cStaPassword, (char *)param->sta_passwd.passwd,
            param->sta_passwd.passwd_len);
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
    (void)strncpy((char *)sta_config.sta.ssid, g_cStaSsid,
                  sizeof(sta_config.sta.ssid) - 1U);
    (void)strncpy((char *)sta_config.sta.password, g_cStaPassword,
                  sizeof(sta_config.sta.password) - 1U);
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    // 1. Xóa cấu hình Wi‑Fi cũ trước khi lưu AP mới, tránh dùng subnet/SSID cũ
    // sau khi đổi mạng
    esp_err_t eClearErr = app_nvs_ClearWifiConfig();
    if ((eClearErr == ESP_OK) || (eClearErr == ESP_ERR_NOT_FOUND)) {
      (void)app_nvs_SaveWifiConfig(&sta_config);
      (void)app_nvs_SetProvisionedWifiConfig(true);
    } else {
      ESP_LOGW(TAG, "Xóa cấu hình Wi-Fi cũ thất bại, mã lỗi=%s", esp_err_to_name(eClearErr));
    }

    // 2. Cấu hình và Bắt đầu kết nối Wi-Fi
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
      esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0,&info);
    } else {
      esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, 0, &info);
    }
    ESP_LOGI(TAG, "Đã gửi phản hồi trạng thái Wi-Fi qua BLUFI");
    break;
  }

  case ESP_BLUFI_EVENT_RECV_CUSTOM_DATA: {
    ESP_LOGI(TAG, "Đã nhận dữ liệu tùy chỉnh: %s", param->custom_data);

    if ((param->custom_data.data == NULL) ||
        (param->custom_data.data_len == 0U) ||
        (param->custom_data.data_len >= DF_BLUFI_CUSTOM_DATA_BUFFER_SIZE)) {
      ESP_LOGE(TAG, "Dữ liệu tùy chỉnh không hợp lệ");
      break;
    }

    static char cJsonBuffer[DF_BLUFI_CUSTOM_DATA_BUFFER_SIZE];
    (void)memset(cJsonBuffer, 0, sizeof(cJsonBuffer));
    (void)memcpy(cJsonBuffer, param->custom_data.data,
                 (size_t)param->custom_data.data_len);
    cJsonBuffer[param->custom_data.data_len] = '\0';

    cJSON *root = cJSON_Parse(cJsonBuffer);
    if (root != NULL) {
      const cJSON *cmd_name = cJSON_GetObjectItem(root, "name");
      const cJSON *value = cJSON_GetObjectItem(root, "value");

      if ((cmd_name != NULL) && cJSON_IsString(cmd_name) &&
          (cmd_name->valuestring != NULL)) {

        int32_t dev_t = 0;
        char cDeviceAddress[DF_BLUFI_DEVICE_ADDRESS_SIZE] = {0};

        if (value != NULL) {
          const cJSON *js_dev_t = cJSON_GetObjectItem(value, "devT");
          if (cJSON_IsNumber(js_dev_t)) {
            dev_t = js_dev_t->valueint;
          }

          const cJSON *js_dev_addr = cJSON_GetObjectItem(value, "devExtAddr");
          if (cJSON_IsString(js_dev_addr) &&
              (js_dev_addr->valuestring != NULL)) {
            (void)strncpy(cDeviceAddress, js_dev_addr->valuestring,
                          sizeof(cDeviceAddress) - 1U);
          }
        }

        if (strcmp(cmd_name->valuestring, "CmdGetDeviceID") == 0) {
          int32_t req_dev_t =
              (dev_t != 0) ? dev_t : DF_BLUFI_DEVICE_TYPE_DEFAULT;
          char cRequestedDeviceAddress[DF_BLUFI_DEVICE_ADDRESS_SIZE] = {0};

          if (value != NULL) {
            const cJSON *js_dev_t = cJSON_GetObjectItem(value, "devT");
            if (cJSON_IsNumber(js_dev_t)) {
              req_dev_t = js_dev_t->valueint;
            }

            const cJSON *js_dev_addr = cJSON_GetObjectItem(value, "devExtAddr");
            if (cJSON_IsString(js_dev_addr) &&
                (js_dev_addr->valuestring != NULL)) {
              (void)snprintf(cRequestedDeviceAddress,
                             sizeof(cRequestedDeviceAddress), "%s",
                             js_dev_addr->valuestring);
            }
          }

          if (cRequestedDeviceAddress[0] == '\0') {
            uint8_t mac[6];
            esp_read_mac(mac, ESP_MAC_BASE);
            (void)snprintf(cRequestedDeviceAddress,
                           sizeof(cRequestedDeviceAddress),
                           "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2],
                           mac[3], mac[4], mac[5]);
          }

          send_response_get_device_id(req_dev_t, cRequestedDeviceAddress);
        }

        // XỬ LÝ LỆNH 1: CmdSetDeviceConfig
        else if (strcmp(cmd_name->valuestring, "CmdSetDeviceConfig") == 0) {
          int32_t status_code = 50000; 

          if (value != NULL) {
            (void)memset(&g_sCurrentDeviceConfig, 0,
                         sizeof(g_sCurrentDeviceConfig));
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
            if (cJSON_IsString(js_dev_addr) &&
                (js_dev_addr->valuestring != NULL)) {
              (void)snprintf(g_sCurrentDeviceConfig.dev_ext_addr,
                             sizeof(g_sCurrentDeviceConfig.dev_ext_addr), "%s",
                             js_dev_addr->valuestring);
            }

            const cJSON *broker = cJSON_GetObjectItem(value, "broker");
            set_string_field(g_sCurrentDeviceConfig.broker,
                             sizeof(g_sCurrentDeviceConfig.broker), broker);

            const cJSON *username = cJSON_GetObjectItem(value, "username");
            set_string_field(g_sCurrentDeviceConfig.username,
                             sizeof(g_sCurrentDeviceConfig.username), username);

            const cJSON *password = cJSON_GetObjectItem(value, "password");
            set_string_field(g_sCurrentDeviceConfig.password,
                             sizeof(g_sCurrentDeviceConfig.password), password);

            const cJSON *mqtt_sub = cJSON_GetObjectItem(value, "mqttsub");
            set_string_field(g_sCurrentDeviceConfig.mqtt_sub,
                             sizeof(g_sCurrentDeviceConfig.mqtt_sub), mqtt_sub);

            const cJSON *mqtt_pub = cJSON_GetObjectItem(value, "mqttpub");
            set_string_field(g_sCurrentDeviceConfig.mqtt_pub,
                             sizeof(g_sCurrentDeviceConfig.mqtt_pub), mqtt_pub);

            const cJSON *mqtt_alert = cJSON_GetObjectItem(value, "mqttalert");
            set_string_field(g_sCurrentDeviceConfig.mqtt_alert,
                             sizeof(g_sCurrentDeviceConfig.mqtt_alert),
                             mqtt_alert);

            const cJSON *force_ota_url =
                cJSON_GetObjectItem(value, "forceOtaUrl");
            set_string_field(g_sCurrentDeviceConfig.force_ota_url,
                             sizeof(g_sCurrentDeviceConfig.force_ota_url),
                             force_ota_url);

            const cJSON *be_shared_key =
                cJSON_GetObjectItem(value, "beSharedKey");
            set_string_field(g_sCurrentDeviceConfig.be_shared_key,
                             sizeof(g_sCurrentDeviceConfig.be_shared_key),
                             be_shared_key);

            const cJSON *api_url = cJSON_GetObjectItem(value, "apiUrl");
            set_string_field(g_sCurrentDeviceConfig.api_url,
                             sizeof(g_sCurrentDeviceConfig.api_url), api_url);

            const cJSON *api_secret_key =
                cJSON_GetObjectItem(value, "apiSecretKey");
            set_string_field(g_sCurrentDeviceConfig.api_secret_key,
                             sizeof(g_sCurrentDeviceConfig.api_secret_key),
                             api_secret_key);

            const cJSON *user_id = cJSON_GetObjectItem(value, "userId");
            set_number_as_string_field(g_sCurrentDeviceConfig.user_id,
                                       sizeof(g_sCurrentDeviceConfig.user_id),
                                       user_id);

            if ((g_sCurrentDeviceConfig.dev_type == 0) ||
                (g_sCurrentDeviceConfig.dev_ext_addr[0] == '\0')) {
              g_sCurrentDeviceConfig.dev_type = DF_BLUFI_DEVICE_TYPE_DEFAULT;
              status_code = 50004;
            }
            if (status_code == 50000) {
              const esp_err_t eErr =
                  app_nvs_SaveDeviceConfig(&g_sCurrentDeviceConfig);
              if (eErr == ESP_OK) {
                // Cập nhật lại trạng thái thiết bị sang Normal
                /* Tắt các cờ trạng thái kết nối trung gian */
                app_device_state_SetModeBit(DEVICE_MODE_UNCONNECTED, false);
                app_device_state_SetModeBit(DEVICE_MODE_CONNECT_AUTO, false);
                app_device_state_SetModeBit(DEVICE_MODE_CONNECT_MANUAL, false);

                /* Bật cờ trạng thái hoạt động bình thường */
                app_device_state_SetModeBit(DEVICE_MODE_NORMAL, true);
                app_led_state_SetState(E_LED_STATE_LOCKED);
                // Khởi tạo MQTT luôn
                //app_mqtt_StartInit(&g_sCurrentDeviceConfig);
              }
            }
          } else {
            status_code = 50004; // Lỗi thiếu payload
          }

          send_response_set_device_config(dev_t, cDeviceAddress, status_code);
          if (status_code == 50000) {
            /* TẦNG 1 (Chuẩn): Lấy timeStamp từ đối tượng 'value' do App truyền xuống */
            if (value != NULL) {
              const cJSON *js_timestamp = cJSON_GetObjectItem(value, "timeStamp");
              if (cJSON_IsNumber(js_timestamp) && (js_timestamp->valueint > 1700000000)) {
                app_sntp_SetSystemTime((int64_t)js_timestamp->valueint);
                ESP_LOGI(TAG, "=> TẦNG 1: Đã nạp thành công giờ từ App cho RTC: %lld", (long long)js_timestamp->valueint);
              }
            }
            g_bShutdownAfterDisconnect = true;
            ESP_LOGI(TAG, "Đã lưu CmdSetDeviceConfig -> Chờ App ngắt kết nối BLE");
            // vTaskDelay(pdMS_TO_TICKS(300));
            // (void)esp_blufi_disconnect();
            xTaskCreate(blufi_delayed_deinit_task, "blufi_deinit_task", 2048, NULL, 5, NULL);
          }
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
    ESP_LOGI(TAG,
             "Yêu cầu ngắt kết nối BLE từ phía Slave -> Tắt toàn bộ BLUFI");
    g_bShutdownAfterDisconnect = true;
    (void)esp_blufi_disconnect();

    break;

  default:
    break;
  }
}

esp_err_t app_blufi_Init(void) {
  esp_err_t ret;
  app_led_state_SetState(E_LED_STATE_BLUFI_AUTO);

  // Giải phóng bộ nhớ Classic BT, chỉ giữ BLE
  ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

  // Tạo cấu hình mặc định cho BT Controller thông qua macro
  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

  // Khởi tạo phần cứng và tài nguyên cho BT Controller với cấu hình vừa tạo
  ret = esp_bt_controller_init(&bt_cfg);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "%s Khởi tạo bộ điều khiển BT thất bại: %s", __func__,
             esp_err_to_name(ret));
    return ret;
  }

  // Kích hoạt Bluetooth Controller ở chế độ BLE
  ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "%s Kích hoạt bộ điều khiển BT thất bại: %s", __func__,
             esp_err_to_name(ret));
    return ret;
  }

  // Khởi tạo tầng giao thức Bluedroid (Bluetooth Stack Init)
  ret = esp_bluedroid_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "%s Khởi tạo Bluetooth Stack (Bluedroid) thất bại: %s",
             __func__, esp_err_to_name(ret));
    return ret;
  }

  // Bật tầng giao thức Bluedroid (Bluetooth Stack Enable)
  ret = esp_bluedroid_enable();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "%s Bật Bluetooth Stack (Bluedroid) thất bại: %s", __func__,
             esp_err_to_name(ret));
    return ret;
  }

  // Set tên thiết bị (dùng Base MAC của chip)
  uint8_t mac[6];
  esp_read_mac(mac,
               ESP_MAC_BASE); // Đọc địa chỉ MAC định danh phần cứng được ghi
                              // sẵn trong eFuse của chip ESP32. ESP_MAC_BASE:
                              // Lấy địa chỉ MAC cơ sở (Base MAC)
  char device_name[30];
  snprintf(device_name, sizeof(device_name), "VCONNEX-%02X%02X", mac[4],
           mac[5]); // ghép chuỗi

  // Thiết lập tên thiết bị cho tầng GAP BLE
  ret = esp_ble_gap_set_device_name(device_name);
  if (ret) {
    ESP_LOGE(TAG, "%s Thiết lập tên thiết bị thất bại: %s", __func__,
             esp_err_to_name(ret));
    return ret;
  }

  // === Đăng ký GAP callback ===
  // Đăng ký Handler xử lý sự kiện GAP (Generic Access Profile)
  // Khi có các sự kiện BLE ở cấp độ kết nối/quảng bá xảy ra (như bắt đầu/dừng
  // quảng bá, có thiết bị kết nối/ngắt kết nối, cập nhật tham số kết nối
  ret = esp_ble_gap_register_callback(app_blufi_gap_event_handler);
  if (ret) {
    ESP_LOGE(TAG, "%s Đăng ký GAP callback thất bại: %s", __func__,
             esp_err_to_name(ret));
    return ret;
  }

  // Đăng ký GATT callback (Quản lý Truyền/Nhận dữ liệu BluFi) - BẮT BUỘC
  ret = esp_ble_gatts_register_callback(esp_blufi_gatt_event_handler);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "%s Đăng ký GATT callback thất bại: %s", __func__,
             esp_err_to_name(ret));
    return ret;
  }

  // Đăng ký Callback cho bộ giao thức BluFi
  // lo phần nghiệp vụ BluFi (nhận SSID/Pass, cấu hình Wi-Fi).
  ret = esp_blufi_register_callbacks(&example_callbacks);
  if (ret) {
    ESP_LOGE(TAG, "%s Đăng ký BLUFI callback thất bại = %x", __func__, ret);
    return ret;
  }

  // chính thức khởi tạo, bắn ra sự kiện ESP_BLUFI_EVENT_INITIALIZE_COMPLETE.
  ret = esp_blufi_profile_init();
  if (ret) {
    ESP_LOGE(TAG, "%s blufi profile init failed, error code = %x", __func__,
             ret);
    return ret;
  }

  return ESP_OK;
}

esp_err_t app_blufi_Deinit(void) {
  if (!g_bBlufiActive) {
    return ESP_OK;
  }
  g_bShutdownAfterDisconnect = false;
  g_bBlufiActive = false;
  g_bBlufiBleConnected = false;
  (void)esp_ble_gap_stop_advertising();
  esp_blufi_profile_deinit();
  esp_bluedroid_disable();
  esp_bluedroid_deinit();
  esp_bt_controller_disable();
  esp_bt_controller_deinit();
  esp_err_t eMemRet = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
  if (eMemRet == ESP_OK) {
      ESP_LOGI(TAG, "=> Đã thu hồi thành công ~50KB RAM từ Bluetooth!");
  }

  ESP_LOGI(TAG, "Hủy khởi tạo BLUFI thành công");

  /*  Khởi tạo MQTT tại đây */
  if (g_sCurrentDeviceConfig.dev_type != 0) {
      ESP_LOGI(TAG, "Đang khởi tạo MQTT sau khi tắt Bluetooth...");
      app_mqtt_StartInit(&g_sCurrentDeviceConfig);
  }

  return ESP_OK;
}
