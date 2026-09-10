#include "app_nvs.h"
#include "app_common.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "APP_NVS";
app_extra_config_t g_sExtraConfig;

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

static esp_err_t
app_nvs_SaveDeviceConfigToHandle(nvs_handle_t hHandle,
                                 const app_nvs_device_config_t *config) {
  DF_RETURN_IF_ERROR(
      nvs_set_i32(hHandle, DF_APP_STORAGE_KEY_DEV_TYPE, config->dev_type));
  DF_RETURN_IF_ERROR(nvs_set_str(hHandle, DF_APP_STORAGE_KEY_DEV_EXT_ADDR,
                                 config->dev_ext_addr));
  DF_RETURN_IF_ERROR(
      nvs_set_str(hHandle, DF_APP_STORAGE_KEY_BROKER, config->broker));
  DF_RETURN_IF_ERROR(
      nvs_set_str(hHandle, DF_APP_STORAGE_KEY_USERNAME, config->username));
  DF_RETURN_IF_ERROR(
      nvs_set_str(hHandle, DF_APP_STORAGE_KEY_PASSWORD, config->password));
  DF_RETURN_IF_ERROR(
      nvs_set_str(hHandle, DF_APP_STORAGE_KEY_MQTT_SUB, config->mqtt_sub));
  DF_RETURN_IF_ERROR(
      nvs_set_str(hHandle, DF_APP_STORAGE_KEY_MQTT_PUB, config->mqtt_pub));
  DF_RETURN_IF_ERROR(
      nvs_set_str(hHandle, DF_APP_STORAGE_KEY_MQTT_ALERT, config->mqtt_alert));
  DF_RETURN_IF_ERROR(nvs_set_str(hHandle, DF_APP_STORAGE_KEY_FORCE_OTA_URL,
                                 config->force_ota_url));
  DF_RETURN_IF_ERROR(nvs_set_str(hHandle, DF_APP_STORAGE_KEY_BE_SHARED_KEY,
                                 config->be_shared_key));
  DF_RETURN_IF_ERROR(
      nvs_set_str(hHandle, DF_APP_STORAGE_KEY_API_URL, config->api_url));
  DF_RETURN_IF_ERROR(nvs_set_str(hHandle, DF_APP_STORAGE_KEY_API_SECRET_KEY,
                                 config->api_secret_key));
  DF_RETURN_IF_ERROR(
      nvs_set_str(hHandle, DF_APP_STORAGE_KEY_USER_ID, config->user_id));
  DF_RETURN_IF_ERROR(
      nvs_set_u8(hHandle, DF_APP_STORAGE_KEY_DEV_PROVISIONED, 1U));
  return nvs_commit(hHandle);
}

static esp_err_t
app_nvs_LoadDeviceConfigFromHandle(nvs_handle_t hHandle,
                                   app_nvs_device_config_t *config) {
  size_t zLen;
  int32_t i32DevType = 0;

  DF_RETURN_IF_ERROR(
      nvs_get_i32(hHandle, DF_APP_STORAGE_KEY_DEV_TYPE, &i32DevType));
  config->dev_type = i32DevType;

  zLen = sizeof(config->dev_ext_addr);
  DF_RETURN_IF_ERROR(nvs_get_str(hHandle, DF_APP_STORAGE_KEY_DEV_EXT_ADDR,
                                 config->dev_ext_addr, &zLen));
  zLen = sizeof(config->broker);
  DF_RETURN_IF_ERROR(
      nvs_get_str(hHandle, DF_APP_STORAGE_KEY_BROKER, config->broker, &zLen));
  zLen = sizeof(config->username);
  DF_RETURN_IF_ERROR(nvs_get_str(hHandle, DF_APP_STORAGE_KEY_USERNAME,
                                 config->username, &zLen));
  zLen = sizeof(config->password);
  DF_RETURN_IF_ERROR(nvs_get_str(hHandle, DF_APP_STORAGE_KEY_PASSWORD,
                                 config->password, &zLen));
  zLen = sizeof(config->mqtt_sub);
  DF_RETURN_IF_ERROR(nvs_get_str(hHandle, DF_APP_STORAGE_KEY_MQTT_SUB,
                                 config->mqtt_sub, &zLen));
  zLen = sizeof(config->mqtt_pub);
  DF_RETURN_IF_ERROR(nvs_get_str(hHandle, DF_APP_STORAGE_KEY_MQTT_PUB,
                                 config->mqtt_pub, &zLen));
  zLen = sizeof(config->mqtt_alert);
  DF_RETURN_IF_ERROR(nvs_get_str(hHandle, DF_APP_STORAGE_KEY_MQTT_ALERT,
                                 config->mqtt_alert, &zLen));
  zLen = sizeof(config->force_ota_url);
  DF_RETURN_IF_ERROR(nvs_get_str(hHandle, DF_APP_STORAGE_KEY_FORCE_OTA_URL,
                                 config->force_ota_url, &zLen));
  zLen = sizeof(config->be_shared_key);
  DF_RETURN_IF_ERROR(nvs_get_str(hHandle, DF_APP_STORAGE_KEY_BE_SHARED_KEY,
                                 config->be_shared_key, &zLen));
  zLen = sizeof(config->api_url);
  DF_RETURN_IF_ERROR(
      nvs_get_str(hHandle, DF_APP_STORAGE_KEY_API_URL, config->api_url, &zLen));
  zLen = sizeof(config->api_secret_key);
  DF_RETURN_IF_ERROR(nvs_get_str(hHandle, DF_APP_STORAGE_KEY_API_SECRET_KEY,
                                 config->api_secret_key, &zLen));
  zLen = sizeof(config->user_id);
  return nvs_get_str(hHandle, DF_APP_STORAGE_KEY_USER_ID, config->user_id,
                     &zLen);
}

esp_err_t app_nvs_InitNvs(void) {
  ESP_LOGI(TAG, "Bắt đầu khởi tạo bộ nhớ NVS");
  esp_err_t ret = ESP_OK;
  ret = nvs_flash_erase();
  if (ret != ESP_OK) {
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
      ESP_LOGI(TAG, "Đã đánh dấu lưu cấu hình wifi");
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
      eErr = nvs_set_str(hHandle, DF_APP_STORAGE_KEY_WIFI_SSID,
                         (const char *)&config->sta.ssid);
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
      eErr = nvs_get_str(hHandle, DF_APP_STORAGE_KEY_WIFI_SSID,
                         (char *)config->sta.ssid, &zLen);
      if (eErr == ESP_OK) {
        zLen = sizeof(config->sta.password);
        eErr = nvs_get_str(hHandle, DF_APP_STORAGE_KEY_WIFI_PASS,
                           (char *)config->sta.password, &zLen);
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
  esp_err_t eErr =
      nvs_open(DF_APP_STORAGE_NVS_NAMESPACE, NVS_READWRITE, &hHandle);
  if (eErr != ESP_OK) {
    ESP_LOGE(TAG, "Mở NVS để lưu cấu hình thiết bị thất bại: %s",
             esp_err_to_name(eErr));
    return eErr;
  }

  eErr = app_nvs_SaveDeviceConfigToHandle(hHandle, config);
  if (eErr == ESP_OK) {
    ESP_LOGI(TAG, "Đã lưu cấu hình thiết bị vào NVS (devT=%d)",
             config->dev_type);
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
  esp_err_t eErr =
      nvs_open(DF_APP_STORAGE_NVS_NAMESPACE, NVS_READONLY, &hHandle);
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

  esp_err_t eErr =
      nvs_open(DF_APP_STORAGE_NVS_NAMESPACE, NVS_READONLY, &hHandle);
  if (eErr == ESP_OK) {
    eErr = nvs_get_u8(hHandle, DF_APP_STORAGE_KEY_DEV_PROVISIONED, &u8Value);
    if ((eErr == ESP_OK) && (u8Value == 1U)) {
      bIsProvisioned = true;
    }
    nvs_close(hHandle);
  }
  return bIsProvisioned;
}

// extra config functions

void app_nvs_SetDefaultExtraConfig(app_extra_config_t *config) {
  config->buzzerEnb = 1;
  config->ledEnb = 1;
  config->ledRgbOn = 16711680;
  config->ledRgbOff = 255;
  config->led_lightness = 100;

  config->gate_1_type = 1;
  config->gate_1_control_mode = 1;
  config->gate_1_led_off = 0;
  config->gate_1_rgb_on = 862021;
  config->gate_1_rgb_off = 123456;

  config->gate_2_type = 1;
  config->gate_2_control_mode = 1;
  config->gate_2_led_off = 0;
  config->gate_2_rgb_on = 862021;
  config->gate_2_rgb_off = 123456;

  config->gate_3_type = 1;
  config->gate_3_control_mode = 1;
  config->gate_3_led_off = 0;
  config->gate_3_rgb_on = 862021;
  config->gate_3_rgb_off = 123456;

  config->nightModeEnb = 1;
  config->nightBegin = 1638928504;
  config->nightEnd = 1638928504;
  config->nightTz = 7;

  config->warningEnb = 1;
  config->warningBegin = 1638928504;
  config->warningEnd = 1638928504;

  config->switch_1_lightness = 100;
  config->switch_2_lightness = 100;
  config->switch_3_lightness = 100;

  config->anti_animal_enb = 0;
  config->anti_animal_lock_time = 5;
  config->gate_countdown = 200;
  config->sgmCycle = 60;
  config->sgmCycleGap = 6;
  config->sgmUseCycleGap = 1;

  config->resetMode = 1;
  config->wlanMode = 0;
  config->lockRFEnb = 0;
  config->lockRFBegin = 1638928504;
  config->lockRFEnd = 1638928504;
}

esp_err_t app_nvs_SaveExtraConfig(const app_extra_config_t *config) {
  DF_CHECK_NULL_PARAM(config);

  nvs_handle_t hHandle = 0U;
  esp_err_t eErr =
      nvs_open(DF_APP_STORAGE_NVS_NAMESPACE, NVS_READWRITE, &hHandle);
  if (eErr != ESP_OK) {
    ESP_LOGE(TAG, "Mở NVS để lưu Extra Config thất bại: %s",
             esp_err_to_name(eErr));
    return eErr;
  }

  /* Lưu toàn bộ struct xuống NVS dưới dạng Blob */
  eErr = nvs_set_blob(hHandle, DF_APP_STORAGE_KEY_EXTRA_CONFIG, config,
                      sizeof(app_extra_config_t));
  if (eErr == ESP_OK) {
    eErr = nvs_commit(hHandle);
    ESP_LOGI(TAG, "Đã lưu Extra Config vào NVS");
  } else {
    ESP_LOGE(TAG, "Lưu Blob Extra Config thất bại: %s", esp_err_to_name(eErr));
  }

  nvs_close(hHandle);
  return eErr;
}

esp_err_t app_nvs_LoadExtraConfig(app_extra_config_t *config) {
  DF_CHECK_NULL_PARAM(config);
  memset(config, 0,
         sizeof(app_extra_config_t)); // Xóa trắng dữ liệu trước khi đọc

  app_nvs_SetDefaultExtraConfig(
      config); // đọc dữ liệu mặc định trước (tránh trường hợp mở nvs thất bại)

  nvs_handle_t hHandle = 0U;
  esp_err_t eErr =
      nvs_open(DF_APP_STORAGE_NVS_NAMESPACE, NVS_READONLY, &hHandle);
  if (eErr != ESP_OK) {
    return eErr;
  }

  size_t required_size = sizeof(app_extra_config_t);
  eErr = nvs_get_blob(hHandle, DF_APP_STORAGE_KEY_EXTRA_CONFIG, config,
                      &required_size);

  if (eErr == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(
        TAG,
        "Extra Config chưa từng được lưu, tiến hành dùng giá trị mặc định");
  } else if (eErr != ESP_OK) {
    ESP_LOGE(TAG, "Lỗi đọc Extra Config: %s", esp_err_to_name(eErr));
  } else {
    ESP_LOGI(TAG, "Đã tải thành công Extra Config từ bộ nhớ Flash");
  }

  nvs_close(hHandle);
  return eErr;
}

esp_err_t app_nvs_SaveSchedule(const app_schedule_item_t *psNewSchedule) {
  if (psNewSchedule == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t xNvsHandle;
  /* Mở NVS Namespace (Giả sử bạn đang dùng macro DF_NVS_NAMESPACE) */
  esp_err_t eErr =
      nvs_open(DF_APP_STORAGE_NVS_NAMESPACE, NVS_READWRITE, &xNvsHandle);
  if (eErr != ESP_OK) {
    ESP_LOGE(TAG, "Không thể mở NVS để lưu lịch hẹn giờ: %s",
             esp_err_to_name(eErr));
    return eErr;
  }

  /* Khởi tạo mảng chứa tối đa 10 lịch */
  app_schedule_item_t asSchedules[DF_MAX_SCHEDULES];
  size_t zLength = sizeof(asSchedules);
  uint8_t u8ScheduleCount = 0U;

  (void)memset(asSchedules, 0, sizeof(asSchedules));

  /* 1. Đọc mảng lịch hiện tại đang có trong NVS (nếu có) */
  eErr = nvs_get_blob(xNvsHandle, "schedules", asSchedules, &zLength);
  if (eErr == ESP_OK) {
    /* Tính ra số lượng lịch đang có dựa trên kích thước mảng đọc được */
    u8ScheduleCount = (uint8_t)(zLength / sizeof(app_schedule_item_t));
  } else if (eErr != ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGE(TAG, "Lỗi đọc blob schedules: %s", esp_err_to_name(eErr));
    nvs_close(xNvsHandle);
    return eErr;
  }

  /* 2. Kiểm tra xem ID này đã tồn tại chưa để Cập nhật */
  bool bIsUpdated = false;
  for (uint8_t i = 0; i < u8ScheduleCount; i++) {
    if (asSchedules[i].u32Id == psNewSchedule->u32Id) {
      (void)memcpy(&asSchedules[i], psNewSchedule, sizeof(app_schedule_item_t));
      bIsUpdated = true;
      ESP_LOGI(TAG, "Cập nhật thành công lịch ID: %u",
               (unsigned int)psNewSchedule->u32Id);
      break;
    }
  }

  /* 3. Nếu chưa tồn tại, tiến hành Thêm mới vào cuối mảng */
  if (!bIsUpdated) {
    if (u8ScheduleCount < DF_MAX_SCHEDULES) {
      (void)memcpy(&asSchedules[u8ScheduleCount], psNewSchedule,
                   sizeof(app_schedule_item_t));
      u8ScheduleCount++;
      zLength = (size_t)u8ScheduleCount * sizeof(app_schedule_item_t);
      ESP_LOGI(TAG, "Thêm mới thành công lịch ID: %u (Tổng: %d/%d)",
               (unsigned int)psNewSchedule->u32Id, u8ScheduleCount,
               DF_MAX_SCHEDULES);
    } else {
      ESP_LOGW(TAG, "Danh sách hẹn giờ đã đầy (%d), không thể thêm mới!",
               DF_MAX_SCHEDULES);
      nvs_close(xNvsHandle);
      return ESP_ERR_NO_MEM;
    }
  }

  /* 4. Ghi toàn bộ mảng trở lại NVS và Commit */
  eErr = nvs_set_blob(xNvsHandle, "schedules", asSchedules, zLength);
  if (eErr == ESP_OK) {
    eErr = nvs_commit(xNvsHandle);
  }

  nvs_close(xNvsHandle);
  return eErr;
}

esp_err_t app_nvs_DeleteSchedule(uint32_t u32Id) {
  nvs_handle_t xNvsHandle;
  esp_err_t eErr =
      nvs_open(DF_APP_STORAGE_NVS_NAMESPACE, NVS_READWRITE, &xNvsHandle);
  if (eErr != ESP_OK) {
    ESP_LOGE(TAG, "Không thể mở NVS để xóa lịch: %s", esp_err_to_name(eErr));
    return eErr;
  }

  app_schedule_item_t asSchedules[DF_MAX_SCHEDULES];
  size_t zLength = sizeof(asSchedules);
  uint8_t u8ScheduleCount = 0U;
  bool bIsFound = false;

  (void)memset(asSchedules, 0, sizeof(asSchedules));

  /* Đọc mảng lịch hiện hành */
  eErr = nvs_get_blob(xNvsHandle, "schedules", asSchedules, &zLength);
  if (eErr == ESP_OK) {
    u8ScheduleCount = (uint8_t)(zLength / sizeof(app_schedule_item_t));

    /* Tìm và xóa phần tử */
    for (uint8_t i = 0; i < u8ScheduleCount; i++) {
      if (asSchedules[i].u32Id == u32Id) {
        bIsFound = true;
        /* Dịch các phần tử phía sau lên trước 1 ô để lấp chỗ trống */
        for (uint8_t j = i; j < u8ScheduleCount - 1; j++) {
          (void)memcpy(&asSchedules[j], &asSchedules[j + 1],
                       sizeof(app_schedule_item_t));
        }
        u8ScheduleCount--;
        break;
      }
    }

    if (bIsFound) {
      if (u8ScheduleCount == 0) {
        /* Nếu mảng rỗng, xóa hoàn toàn Key cho sạch bộ nhớ */
        eErr = nvs_erase_key(xNvsHandle, "schedules");
      } else {
        /* Ghi lại mảng đã rút gọn */
        zLength = (size_t)u8ScheduleCount * sizeof(app_schedule_item_t);
        eErr = nvs_set_blob(xNvsHandle, "schedules", asSchedules, zLength);
      }

      if (eErr == ESP_OK) {
        eErr = nvs_commit(xNvsHandle);
        ESP_LOGI(TAG, "Đã xóa thành công lịch hẹn giờ ID: %u",
                 (unsigned int)u32Id);
      }
    } else {
      ESP_LOGW(TAG, "Không tìm thấy lịch hẹn giờ ID: %u để xóa",
               (unsigned int)u32Id);
      eErr = ESP_ERR_NOT_FOUND;
    }
  } else {
    ESP_LOGW(TAG, "NVS chưa có lịch hẹn giờ nào!");
  }

  nvs_close(xNvsHandle);
  return eErr;
}

esp_err_t app_nvs_DeleteAllSchedules(void) {
  nvs_handle_t xNvsHandle;
  esp_err_t eErr =
      nvs_open(DF_APP_STORAGE_NVS_NAMESPACE, NVS_READWRITE, &xNvsHandle);
  if (eErr != ESP_OK)
    return eErr;

  /* Xóa hoàn toàn Key lưu trữ lịch khỏi bộ nhớ */
  eErr = nvs_erase_key(xNvsHandle, "schedules");
  if (eErr == ESP_OK) {
    nvs_commit(xNvsHandle);
    ESP_LOGI(TAG, "Đã xóa TẤT CẢ lịch hẹn giờ trong NVS");
  } else if (eErr == ESP_ERR_NVS_NOT_FOUND) {
    eErr = ESP_OK; /* Nếu chưa có lịch nào thì vẫn coi như xóa thành công */
  }

  nvs_close(xNvsHandle);
  return eErr;
}

esp_err_t app_nvs_GetAllSchedules(app_schedule_item_t *pasSchedules,
                                  uint8_t *pu8Count) {
  *pu8Count = 0;
  nvs_handle_t xNvsHandle;
  esp_err_t eErr =
      nvs_open(DF_APP_STORAGE_NVS_NAMESPACE, NVS_READONLY, &xNvsHandle);
  if (eErr != ESP_OK)
    return eErr;
  size_t zLength = DF_MAX_SCHEDULES * sizeof(app_schedule_item_t);
  eErr = nvs_get_blob(xNvsHandle, "schedules", pasSchedules, &zLength);
  if (eErr == ESP_OK) {
    *pu8Count = (uint8_t)(zLength / sizeof(app_schedule_item_t));
  }

  nvs_close(xNvsHandle);
  return eErr;
}

// Delete Device Configuration from NVS
esp_err_t app_nvs_ClearDeviceConfig(void) {
  nvs_handle_t hHandle = 0U;
  esp_err_t eErr =
      nvs_open(DF_APP_STORAGE_NVS_NAMESPACE, NVS_READWRITE, &hHandle);
  if (eErr == ESP_OK) {
    (void)nvs_erase_key(hHandle, DF_APP_STORAGE_KEY_DEV_TYPE);
    (void)nvs_erase_key(hHandle, DF_APP_STORAGE_KEY_DEV_EXT_ADDR);
    (void)nvs_erase_key(hHandle, DF_APP_STORAGE_KEY_BROKER);
    (void)nvs_erase_key(hHandle, DF_APP_STORAGE_KEY_USERNAME);
    (void)nvs_erase_key(hHandle, DF_APP_STORAGE_KEY_PASSWORD);
    (void)nvs_erase_key(hHandle, DF_APP_STORAGE_KEY_MQTT_SUB);
    (void)nvs_erase_key(hHandle, DF_APP_STORAGE_KEY_MQTT_PUB);
    (void)nvs_erase_key(hHandle, DF_APP_STORAGE_KEY_MQTT_ALERT);
    (void)nvs_erase_key(hHandle, DF_APP_STORAGE_KEY_FORCE_OTA_URL);
    (void)nvs_erase_key(hHandle, DF_APP_STORAGE_KEY_BE_SHARED_KEY);
    (void)nvs_erase_key(hHandle, DF_APP_STORAGE_KEY_API_URL);
    (void)nvs_erase_key(hHandle, DF_APP_STORAGE_KEY_API_SECRET_KEY);
    (void)nvs_erase_key(hHandle, DF_APP_STORAGE_KEY_USER_ID);
    (void)nvs_erase_key(hHandle, DF_APP_STORAGE_KEY_DEV_PROVISIONED);
    eErr = nvs_commit(hHandle);
    (void)nvs_close(hHandle);
  }
  if (eErr == ESP_OK) {
    ESP_LOGI(TAG, "Đã xóa cấu hình thiết bị (Device Config)");
  }
  return eErr;
}

// Delete Extra Config from NVS
esp_err_t app_nvs_ClearExtraConfig(void) {
  nvs_handle_t hHandle = 0U;
  esp_err_t eErr =
      nvs_open(DF_APP_STORAGE_NVS_NAMESPACE, NVS_READWRITE, &hHandle);
  if (eErr == ESP_OK) {
    (void)nvs_erase_key(hHandle, DF_APP_STORAGE_KEY_EXTRA_CONFIG);
    eErr = nvs_commit(hHandle);
    (void)nvs_close(hHandle);
  }
  if (eErr == ESP_OK) {
    ESP_LOGI(TAG, "Đã xóa Extra Config");
  }
  return eErr;
}