#include <stdio.h>
#include <string.h>

#include "app_nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_blufi.h"
#include "app_device_state.h"
#include "app_led_state.h"
#include "app_logic_buzzer.h"
#include "app_logic_led.h"
#include "app_logic_mqtt.h"
#include "app_logic_relay.h"
#include "app_logic_touch.h"
#include "app_mqtt.h"
#include "app_wifi.h"

static const char *TAG = "APP_MAIN";

/* Khai báo tĩnh (static) ở phạm vi file để tránh cấp phát trên stack */
static app_nvs_device_config_t sNvsConfig;
static app_nvs_device_config_t sDeviceConfig;

void app_main(void) {
  vTaskDelay(pdMS_TO_TICKS(10000));

  ESP_LOGI(TAG, "=== BẮT ĐẦU KIỂM TRA KHỞI TẠO NVS ===");

  // 1. Khởi tạo NVS
  esp_err_t eRet = app_nvs_InitNvs();
  if (eRet != ESP_OK) {
    ESP_LOGE(TAG, "Khởi tạo NVS thất bại! Mã lỗi: %s", esp_err_to_name(eRet));
    return;
  }
  ESP_LOGI(TAG, "Khởi tạo NVS thành công!");

  // 2. Khởi tạo các module nghiệp vụ tầng Application
  eRet = app_logic_relay_Init();
  if (eRet != ESP_OK) {
    ESP_LOGE(TAG, "Khởi tạo logic relay thất bại! Mã lỗi: %s",
             esp_err_to_name(eRet));
    return;
  }

  eRet = app_logic_touch_Init();
  if (eRet != ESP_OK) {
    ESP_LOGE(TAG, "Khởi tạo logic cảm ứng thất bại! Mã lỗi: %s",
             esp_err_to_name(eRet));
    return;
  }

  eRet = app_logic_led_Init();
  if (eRet != ESP_OK) {
    ESP_LOGE(TAG, "Khởi tạo logic LED thất bại! Mã lỗi: %s",
             esp_err_to_name(eRet));
    return;
  }
  app_led_state_Init();
  app_led_state_SetState(E_LED_STATE_NORMAL_IDLE);

  eRet = app_logic_buzzer_Init();
  if (eRet != ESP_OK) {
    ESP_LOGE(TAG, "Khởi tạo logic buzzer thất bại! Mã lỗi: %s",
             esp_err_to_name(eRet));
    return;
  }

  eRet = app_logic_mqtt_Init();
  if (eRet != ESP_OK) {
    ESP_LOGE(TAG, "Khởi tạo logic MQTT thất bại! Mã lỗi: %s",
             esp_err_to_name(eRet));
    return;
  }

  // 4. Khởi tạo Wi-Fi STA
  eRet = app_wifi_InitSta();
  if (eRet != ESP_OK) {
    ESP_LOGW(TAG,
             "Khởi tạo Wi-Fi STA gặp sự cố, kiểm tra trạng thái thiết bị...");
  }

  // 5. Điều phối luồng khởi động dựa trên trạng thái thiết bị
  device_mode_t eCurrentMode = get_current_door_mode();

  switch (eCurrentMode) {
  case DEVICE_MODE_UNCONNECTED:
    ESP_LOGI(
        TAG,"Thiết bị đang ở chế độ UNCONNECTED, kiểm tra NVS cấu hình Wi-Fi...");
    if (!app_nvs_IsProvisionedWifiConfig()) {
      ESP_LOGI(TAG,"Chưa có Wi-Fi trong NVS, chuyển sang chế độ tự động BluFi...");
      set_current_door_mode(DEVICE_MODE_CONNECT_AUTO);
      app_led_state_SetState(E_LED_STATE_BLUFI_AUTO);
      (void)app_blufi_Init();
    } else {
      ESP_LOGI(TAG,"Đã có sẵn cấu hình Wi-Fi, chuyển sang trạng thái Normal...");
      set_current_door_mode(DEVICE_MODE_LOCKED);
      app_led_state_SetState(E_LED_STATE_LOCKED);
    }
    break;

  case DEVICE_MODE_CONNECT_AUTO:
    ESP_LOGI(TAG, "Thiết bị đang ở chế độ kết nối tự động (BluFi)...");
    (void)app_blufi_Init();
    break;

  case DEVICE_MODE_CONNECT_MANUAL:
    ESP_LOGI(TAG, "Thiết bị đang ở chế độ kết nối thủ công (UDP)...");
    // Triển khai logic lắng nghe cấu hình qua cổng UDP tại đây
    break;

  case DEVICE_MODE_NORMAL:
    ESP_LOGI(TAG, "Thiết bị đang ở chế độ khóa tạm thời, chờ kết nối mạng...");
    app_led_state_SetState(E_LED_STATE_LOCKED);
    set_current_door_mode(DEVICE_MODE_LOCKED);
    if (app_wifi_WaitForConnect(10000U)) {
      ESP_LOGI(TAG, "Kết nối Wi-Fi thành công!");

      // 6. Khởi động MQTT sau khi có Wi-Fi
      (void)memset(&sNvsConfig, 0, sizeof(sNvsConfig));
      eRet = app_nvs_LoadDeviceConfig(&sNvsConfig);
      if (eRet == ESP_OK && sNvsConfig.broker[0] != '\0') {
        (void)memset(&sDeviceConfig, 0, sizeof(sDeviceConfig));
        // Ánh xạ từ NVS config sang app_nvs_device_config_t
        (void)snprintf(sDeviceConfig.broker, sizeof(sDeviceConfig.broker), "%s", sNvsConfig.broker);
        (void)snprintf(sDeviceConfig.username, sizeof(sDeviceConfig.username), "%s", sNvsConfig.username);
        (void)snprintf(sDeviceConfig.password, sizeof(sDeviceConfig.password), "%s", sNvsConfig.password);
        (void)snprintf(sDeviceConfig.mqtt_sub, sizeof(sDeviceConfig.mqtt_sub), "%s", sNvsConfig.mqtt_sub);
        (void)snprintf(sDeviceConfig.mqtt_pub, sizeof(sDeviceConfig.mqtt_pub), "%s", sNvsConfig.mqtt_pub);
        (void)snprintf(sDeviceConfig.mqtt_alert, sizeof(sDeviceConfig.mqtt_alert), "%s", sNvsConfig.mqtt_alert);
        (void)snprintf(sDeviceConfig.api_secret_key, sizeof(sDeviceConfig.api_secret_key), "%s", sNvsConfig.api_secret_key);
        (void)snprintf(sDeviceConfig.dev_ext_addr, sizeof(sDeviceConfig.dev_ext_addr), "%s", sNvsConfig.dev_ext_addr);
        sDeviceConfig.dev_type = sNvsConfig.dev_type;

        eRet = app_mqtt_StartInit(&sDeviceConfig);
        if (eRet != ESP_OK) {
          ESP_LOGE(TAG, "Khởi động MQTT thất bại: %s", esp_err_to_name(eRet));
        } else {
          ESP_LOGI(TAG, "MQTT đã khởi động thành công!");
        }
      } else {
        ESP_LOGW(TAG,"Không tìm thấy cấu hình MQTT trong NVS, bỏ qua khởi động MQTT.");
      }
    } else {
      ESP_LOGW(TAG, "Timeout chờ kết nối Wi-Fi, tiếp tục chạy các task nền.");
    }
    break;

  default:
    ESP_LOGW(TAG, "Trạng thái không xác định, đưa về UNCONNECTED");
    set_current_door_mode(DEVICE_MODE_UNCONNECTED);
    app_led_state_SetState(E_LED_STATE_NORMAL_IDLE);
    break;
  }

  ESP_LOGI(TAG, "=== HỆ THỐNG ĐÃ KHỞI ĐỘNG HOÀN TẤT ===");

  // Vòng lặp chính của app_main (giữ task chính hoạt động)
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(10000U));
  }
}