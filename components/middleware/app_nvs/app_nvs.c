#include "app_nvs.h"
#include "app_common.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "APP_NVS";

#define DF_APP_STORAGE_KEY_DEV_TYPE "dev_type"
#define DF_APP_STORAGE_KEY_DEV_EXT_ADDR "dev_ext_addr"
#define DF_APP_STORAGE_KEY_BROKER "broker"
#define DF_APP_STORAGE_KEY_USERNAME "mqtt_user"
#define DF_APP_STORAGE_KEY_PASSWORD "mqtt_pass"
#define DF_APP_STORAGE_KEY_MQTT_SUB "mqtt_sub"
#define DF_APP_STORAGE_KEY_MQTT_PUB "mqtt_pub"
#define DF_APP_STORAGE_KEY_MQTT_ALERT "mqtt_alert"
#define DF_APP_STORAGE_KEY_FORCE_OTA_URL "ota_url"
#define DF_APP_STORAGE_KEY_BE_SHARED_KEY "be_key"
#define DF_APP_STORAGE_KEY_API_URL "api_url"
#define DF_APP_STORAGE_KEY_API_SECRET_KEY "api_secret"
#define DF_APP_STORAGE_KEY_USER_ID "user_id"

static esp_err_t app_nvs_SaveDeviceConfigToHandle(nvs_handle_t hHandle, const app_nvs_device_config_t *config)
{
  DF_RETURN_IF_ERROR(nvs_set_i32(hHandle, DF_APP_STORAGE_KEY_DEV_TYPE, config->dev_type));
  DF_RETURN_IF_ERROR(nvs_set_str(hHandle, DF_APP_STORAGE_KEY_DEV_EXT_ADDR, config->dev_ext_addr));
  DF_RETURN_IF_ERROR(nvs_set_str(hHandle, DF_APP_STORAGE_KEY_BROKER, config->broker));
  DF_RETURN_IF_ERROR(nvs_set_str(hHandle, DF_APP_STORAGE_KEY_USERNAME, config->username));
  DF_RETURN_IF_ERROR(nvs_set_str(hHandle, DF_APP_STORAGE_KEY_PASSWORD, config->password));
  DF_RETURN_IF_ERROR(nvs_set_str(hHandle, DF_APP_STORAGE_KEY_MQTT_SUB, config->mqtt_sub));
  DF_RETURN_IF_ERROR(nvs_set_str(hHandle, DF_APP_STORAGE_KEY_MQTT_PUB, config->mqtt_pub));
  DF_RETURN_IF_ERROR(nvs_set_str(hHandle, DF_APP_STORAGE_KEY_MQTT_ALERT, config->mqtt_alert));
  DF_RETURN_IF_ERROR(nvs_set_str(hHandle, DF_APP_STORAGE_KEY_FORCE_OTA_URL, config->force_ota_url));
  DF_RETURN_IF_ERROR(nvs_set_str(hHandle, DF_APP_STORAGE_KEY_BE_SHARED_KEY, config->be_shared_key));
  DF_RETURN_IF_ERROR(nvs_set_str(hHandle, DF_APP_STORAGE_KEY_API_URL, config->api_url));
  DF_RETURN_IF_ERROR(nvs_set_str(hHandle, DF_APP_STORAGE_KEY_API_SECRET_KEY, config->api_secret_key));
  DF_RETURN_IF_ERROR(nvs_set_str(hHandle, DF_APP_STORAGE_KEY_USER_ID, config->user_id));
  DF_RETURN_IF_ERROR(nvs_set_u8(hHandle, DF_APP_STORAGE_KEY_DEV_PROVISIONED, 1U));
  return nvs_commit(hHandle);
}

static esp_err_t app_nvs_LoadDeviceConfigFromHandle(nvs_handle_t hHandle, app_nvs_device_config_t *config)
{
  size_t zLen;
  int32_t i32DevType = 0;

  DF_RETURN_IF_ERROR(nvs_get_i32(hHandle, DF_APP_STORAGE_KEY_DEV_TYPE, &i32DevType));
  config->dev_type = i32DevType;

  zLen = sizeof(config->dev_ext_addr);
  DF_RETURN_IF_ERROR(nvs_get_str(hHandle, DF_APP_STORAGE_KEY_DEV_EXT_ADDR, config->dev_ext_addr, &zLen));
  zLen = sizeof(config->broker);
  DF_RETURN_IF_ERROR(nvs_get_str(hHandle, DF_APP_STORAGE_KEY_BROKER, config->broker, &zLen));
  zLen = sizeof(config->username);
  DF_RETURN_IF_ERROR(nvs_get_str(hHandle, DF_APP_STORAGE_KEY_USERNAME, config->username, &zLen));
  zLen = sizeof(config->password);
  DF_RETURN_IF_ERROR(nvs_get_str(hHandle, DF_APP_STORAGE_KEY_PASSWORD, config->password, &zLen));
  zLen = sizeof(config->mqtt_sub);
  DF_RETURN_IF_ERROR(nvs_get_str(hHandle, DF_APP_STORAGE_KEY_MQTT_SUB, config->mqtt_sub, &zLen));
  zLen = sizeof(config->mqtt_pub);
  DF_RETURN_IF_ERROR(nvs_get_str(hHandle, DF_APP_STORAGE_KEY_MQTT_PUB, config->mqtt_pub, &zLen));
  zLen = sizeof(config->mqtt_alert);
  DF_RETURN_IF_ERROR(nvs_get_str(hHandle, DF_APP_STORAGE_KEY_MQTT_ALERT, config->mqtt_alert, &zLen));
  zLen = sizeof(config->force_ota_url);
  DF_RETURN_IF_ERROR(nvs_get_str(hHandle, DF_APP_STORAGE_KEY_FORCE_OTA_URL, config->force_ota_url, &zLen));
  zLen = sizeof(config->be_shared_key);
  DF_RETURN_IF_ERROR(nvs_get_str(hHandle, DF_APP_STORAGE_KEY_BE_SHARED_KEY, config->be_shared_key, &zLen));
  zLen = sizeof(config->api_url);
  DF_RETURN_IF_ERROR(nvs_get_str(hHandle, DF_APP_STORAGE_KEY_API_URL, config->api_url, &zLen));
  zLen = sizeof(config->api_secret_key);
  DF_RETURN_IF_ERROR(nvs_get_str(hHandle, DF_APP_STORAGE_KEY_API_SECRET_KEY, config->api_secret_key, &zLen));
  zLen = sizeof(config->user_id);
  return nvs_get_str(hHandle, DF_APP_STORAGE_KEY_USER_ID, config->user_id, &zLen);
}

esp_err_t app_nvs_InitNvs(void) {
  ESP_LOGI(TAG, "Bắt đầu khởi tạo bộ nhớ NVS");
  esp_err_t ret = ESP_OK;
  ret = nvs_flash_erase();
  if(ret != ESP_OK){
    ESP_LOGE(TAG, "Đang tiến hành xóa NVS để test");
  }
  ret = nvs_flash_init();
  if ((ret == ESP_ERR_NVS_NO_FREE_PAGES) ||
      (ret == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
    ESP_LOGW(TAG, "Khởi tạo NVS thất bại, xóa và thử khởi tạo lại");
    ret = nvs_flash_erase();
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Xóa NVS thất bại");
      return ret;
    }
    ret = nvs_flash_init();
  }
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Khởi tạo NVS thành công");
  } else {
    ESP_LOGE(TAG, "Khởi tạo NVS thất bại");
    return ret;
  }
  return ESP_OK;
}

// Function to check for Wi-Fi credentials stored in NVS
bool app_nvs_IsProvisionedWifiConfig(void) {
  nvs_handle_t hHandle = 0U;
  uint8_t u8Value = 0U;
  bool bIsProvisioned = false;
  esp_err_t eErr = ESP_OK;

  eErr = nvs_open(DF_APP_STORAGE_NVS_NAMESPACE, NVS_READONLY, &hHandle);
  if (eErr == ESP_OK) {
    eErr = nvs_get_u8(hHandle, DF_APP_STORAGE_KEY_PROVISIONED, &u8Value);
    if ((eErr == ESP_OK) && (u8Value == 1U)) {
      bIsProvisioned = true;
    }
    (void)nvs_close(hHandle);
  }
  return bIsProvisioned;
}

// Mark or update the Wi-Fi configuration status.
esp_err_t app_nvs_SetProvisionedWifiConfig(bool provisioned) {
  nvs_handle_t hHandle = 0U;
  esp_err_t eErr = ESP_OK;
  uint8_t u8ValueToSet = 0U;
  if (provisioned == true) {
    u8ValueToSet = 1U;
  }
  eErr = nvs_open(DF_APP_STORAGE_NVS_NAMESPACE, NVS_READWRITE, &hHandle);
  if (eErr == ESP_OK) {
    eErr = nvs_set_u8(hHandle, DF_APP_STORAGE_KEY_PROVISIONED, u8ValueToSet);
    if (eErr == ESP_OK) {
      eErr = nvs_commit(hHandle);
      ESP_LOGI(TAG,"Đã đánh dấu lưu cấu hình wifi"); 
    }
    (void)nvs_close(hHandle);
  }
  return eErr;
}

// Retrieve the Wi-Fi configuration and save it to NVS.
esp_err_t app_nvs_SaveWifiConfig(const wifi_config_t *config) {
  nvs_handle_t hHandle = 0U;
  esp_err_t eErr = ESP_OK;
  DF_CHECK_NULL_PARAM(config);
  {
    eErr = nvs_open(DF_APP_STORAGE_NVS_NAMESPACE, NVS_READWRITE, &hHandle);
    if (eErr == ESP_OK) {
      eErr = nvs_set_str(hHandle, DF_APP_STORAGE_KEY_WIFI_SSID, (const char *)&config->sta.ssid);
      if (eErr == ESP_OK) {
        eErr = nvs_set_str(hHandle, DF_APP_STORAGE_KEY_WIFI_PASS,
                          (const char *)&config->sta.password);
      }
      if (eErr == ESP_OK) {
        eErr = nvs_commit(hHandle);
      }
      (void)nvs_close(hHandle);
    }
  }
  if (eErr == ESP_OK) {
    ESP_LOGI(TAG, "Đã lưu cấu hình Wi-Fi");
  }
  return eErr;
}

// Read the Wi-Fi configuration from NVS.
esp_err_t app_nvs_LoadWifiConfig(wifi_config_t *config) {
  nvs_handle_t hHandle = 0U;
  esp_err_t eErr = ESP_OK;
  DF_CHECK_NULL_PARAM(config);
  {
    (void)memset((void *)config, 0, sizeof(wifi_config_t));
    eErr = nvs_open(DF_APP_STORAGE_NVS_NAMESPACE, NVS_READONLY, &hHandle);
    if (eErr == ESP_OK) {
      size_t zLen = sizeof(config->sta.ssid);
      eErr = nvs_get_str(hHandle, DF_APP_STORAGE_KEY_WIFI_SSID, (char *)config->sta.ssid, &zLen);
      if (eErr == ESP_OK) {
        zLen = sizeof(config->sta.password);
        eErr = nvs_get_str(hHandle, DF_APP_STORAGE_KEY_WIFI_PASS, (char *)config->sta.password, &zLen);
      }
      (void)nvs_close(hHandle);
    }
  }
  return eErr;
}

// Delete Wi-Fi configuration from NVS.
esp_err_t app_nvs_ClearWifiConfig(void) {
  nvs_handle_t hHandle = 0U;
  esp_err_t eErr = ESP_OK;
  eErr = nvs_open(DF_APP_STORAGE_NVS_NAMESPACE, NVS_READWRITE, &hHandle);
  if (eErr == ESP_OK) {
    (void)nvs_erase_key(hHandle, DF_APP_STORAGE_KEY_PROVISIONED);
    (void)nvs_erase_key(hHandle, DF_APP_STORAGE_KEY_WIFI_PASS);
    (void)nvs_erase_key(hHandle, DF_APP_STORAGE_KEY_WIFI_SSID);
    eErr = nvs_commit(hHandle);
    (void)nvs_close(hHandle);
  }
  if (eErr == ESP_OK) {
    ESP_LOGI(TAG, "Đã xóa cấu hình Wi-Fi");
  }

  return eErr;
}

// === Device Config functions ===

esp_err_t app_nvs_SaveDeviceConfig(const app_nvs_device_config_t *config) {
  DF_CHECK_NULL_PARAM(config);

  nvs_handle_t hHandle = 0U;
  esp_err_t eErr = nvs_open(DF_APP_STORAGE_NVS_NAMESPACE, NVS_READWRITE, &hHandle);
  if (eErr != ESP_OK) {
    ESP_LOGE(TAG, "Mở NVS để lưu cấu hình thiết bị thất bại: %s", esp_err_to_name(eErr));
    return eErr;
  }

  eErr = app_nvs_SaveDeviceConfigToHandle(hHandle, config);
  if (eErr == ESP_OK) {
    ESP_LOGI(TAG, "Đã lưu cấu hình thiết bị vào NVS (devT=%d)", config->dev_type);
  }

  if (eErr != ESP_OK) {
    ESP_LOGE(TAG, "Lưu cấu hình thiết bị thất bại: %s", esp_err_to_name(eErr));
  }
  nvs_close(hHandle);
  return eErr;
}

esp_err_t app_nvs_LoadDeviceConfig(app_nvs_device_config_t *config) {
  DF_CHECK_NULL_PARAM(config);

  memset(config, 0, sizeof(app_nvs_device_config_t));

  nvs_handle_t hHandle = 0U;
  esp_err_t eErr = nvs_open(DF_APP_STORAGE_NVS_NAMESPACE, NVS_READONLY, &hHandle);
  if (eErr != ESP_OK) {
    return eErr;
  }

  eErr = app_nvs_LoadDeviceConfigFromHandle(hHandle, config);
  nvs_close(hHandle);
  return eErr;
}

bool app_nvs_IsProvisionedDeviceConfig(void) {
  nvs_handle_t hHandle = 0U;
  uint8_t u8Value = 0U;
  bool bIsProvisioned = false;

  esp_err_t eErr = nvs_open(DF_APP_STORAGE_NVS_NAMESPACE, NVS_READONLY, &hHandle);
  if (eErr == ESP_OK) {
    eErr = nvs_get_u8(hHandle, DF_APP_STORAGE_KEY_DEV_PROVISIONED, &u8Value);
    if ((eErr == ESP_OK) && (u8Value == 1U)) {
      bIsProvisioned = true;
    }
    nvs_close(hHandle);
  }
  return bIsProvisioned;
}
