#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DF_APP_STORAGE_NVS_NAMESPACE       "app_storage" // vị trí lưu trữ trong NVS
#define DF_APP_STORAGE_KEY_PROVISIONED     "provisioned" // đánh dấu đã lưu cấu hình Wi-Fi
#define DF_APP_STORAGE_KEY_WIFI_SSID       "wifi_ssid"   // tên mạng Wi-Fi (SSID)
#define DF_APP_STORAGE_KEY_WIFI_PASS       "wifi_pass"   // mật khẩu mạng Wi-Fi
#define DF_APP_STORAGE_KEY_DEV_PROVISIONED "dev_prov"    // đánh dấu đã lưu cấu hình thiết bị

#define DF_APP_STORAGE_DEV_EXT_ADDR_SIZE   (18U)         // kích thước chuỗi địa chỉ thiết bị (ví dụ: "AA:BB:CC:DD:EE:FF")
#define DF_APP_STORAGE_BROKER_SIZE         (128U)        // kích thước chuỗi địa chỉ broker MQTT
#define DF_APP_STORAGE_USERNAME_SIZE       (64U)         // kích thước chuỗi username MQTT
#define DF_APP_STORAGE_PASSWORD_SIZE       (64U)         // kích thước chuỗi password MQTT
#define DF_APP_STORAGE_MQTT_TOPIC_SIZE     (128U)        // kích thước chuỗi topic MQTT
#define DF_APP_STORAGE_OTA_URL_SIZE        (256U)        // kích thước chuỗi đường dẫn OTA URL
#define DF_APP_STORAGE_SHARED_KEY_SIZE     (256U)        // kích thước chuỗi khóa chia sẻ backend
#define DF_APP_STORAGE_API_URL_SIZE        (256U)        // kích thước chuỗi URL API
#define DF_APP_STORAGE_API_SECRET_SIZE     (128U)        // kích thước chuỗi mã bí mật API
#define DF_APP_STORAGE_USER_ID_SIZE        (64U)         // kích thước chuỗi định danh người dùng


esp_err_t app_nvs_InitNvs(void);

// Function to check for Wi-Fi credentials stored in NVS
bool app_nvs_IsProvisionedWifiConfig(void);

// Mark or update the Wi-Fi configuration status.
esp_err_t app_nvs_SetProvisionedWifiConfig(bool provisioned);

// Retrieve the Wi-Fi configuration and save it to NVS.
esp_err_t app_nvs_SaveWifiConfig(const wifi_config_t *config);

// Read the Wi-Fi configuration from NVS.
esp_err_t app_nvs_LoadWifiConfig(wifi_config_t *config);

// Delete Wi-Fi configuration from NVS.
esp_err_t app_nvs_ClearWifiConfig(void);

// === Device Config (từ CmdSetDeviceConfig) ===

typedef struct {
  int32_t dev_type;         // devT
  char dev_ext_addr[DF_APP_STORAGE_DEV_EXT_ADDR_SIZE];
  char broker[DF_APP_STORAGE_BROKER_SIZE];
  char username[DF_APP_STORAGE_USERNAME_SIZE];
  char password[DF_APP_STORAGE_PASSWORD_SIZE];
  char mqtt_sub[DF_APP_STORAGE_MQTT_TOPIC_SIZE];
  char mqtt_pub[DF_APP_STORAGE_MQTT_TOPIC_SIZE];
  char mqtt_alert[DF_APP_STORAGE_MQTT_TOPIC_SIZE];
  char force_ota_url[DF_APP_STORAGE_OTA_URL_SIZE];
  char be_shared_key[DF_APP_STORAGE_SHARED_KEY_SIZE];
  char api_url[DF_APP_STORAGE_API_URL_SIZE];
  char api_secret_key[DF_APP_STORAGE_API_SECRET_SIZE];
  char user_id[DF_APP_STORAGE_USER_ID_SIZE];
} app_nvs_device_config_t;

// Lưu device config vào NVS
esp_err_t app_nvs_SaveDeviceConfig(const app_nvs_device_config_t *config);

// Đọc device config từ NVS
esp_err_t app_nvs_LoadDeviceConfig(app_nvs_device_config_t *config);

// Kiểm tra đã có device config trong NVS chưa
bool app_nvs_IsProvisionedDeviceConfig(void);

#ifdef __cplusplus
}
#endif