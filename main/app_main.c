#include <stdio.h>
#include <string.h>

#include "app_nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

#include "app_blufi.h"
#include "app_device_state.h"
#include "app_led_state.h"
#include "app_relay_state.h"
#include "app_logic_buzzer.h"
#include "app_logic_led.h"
#include "app_logic_mqtt.h"
#include "app_logic_relay.h"
#include "app_logic_touch.h"
#include "app_mqtt.h"
#include "app_wifi.h"
#include "app_logic_schedule.h"
#include "app_logic_extra_config.h"
#include "app_udp.h"
#include "app_ble_mesh.h"

static const char *TAG = "APP_MAIN";

/* Khai báo tĩnh (static) ở phạm vi file để tránh cấp phát trên stack */
static app_nvs_device_config_t sNvsConfig;
static app_nvs_device_config_t sDeviceConfig;

/**
 * @brief Đọc cấu hình từ NVS và khởi tạo dịch vụ MQTT Client.
 * @note Hàm nội bộ tĩnh (static) của file app_main.c.
 * @return esp_err_t ESP_OK nếu khởi tạo thành công, mã lỗi nếu thất bại.
 */
static esp_err_t app_main_StartMqttFromNvs(void)
{
    (void)memset(&sNvsConfig, 0, sizeof(sNvsConfig));
    esp_err_t eRet = app_nvs_LoadDeviceConfig(&sNvsConfig);
    if ((eRet == ESP_OK) && (sNvsConfig.broker[0] != '\0')) {
        (void)memset(&sDeviceConfig, 0, sizeof(sDeviceConfig));
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
            ESP_LOGI(TAG, "MQTT đã được khởi động thành công!");
        }
        return eRet;
    }

    ESP_LOGW(TAG, "Không tìm thấy cấu hình MQTT hợp lệ trong NVS.");
    return ESP_ERR_NOT_FOUND;
}

/**
 * @brief   Nhánh khởi động dành riêng cho chế độ CẤU HÌNH (BluFi hoặc UDP).
 * @note    Được gọi ngay sau khi đọc cờ u8ReconfigFlag != NONE từ NVS.
 *          Chỉ init tối thiểu các module cần thiết (LED, buzzer, task cảm
 *          ứng RÚT GỌN chỉ để hủy) — KHÔNG init Wi-Fi STA/MQTT/relay logic
 *          đầy đủ như chế độ NORMAL, và KHÔNG bao giờ return về app_main().
 *          app_blufi.c / app_udp.c tự gọi esp_restart() khi hoàn tất hoặc
 *          khi người dùng thoát/hủy từ App Mobile.
 * @param   u8Flag Giá trị cờ đã đọc (E_APP_RECONFIG_BLUFI hoặc E_APP_RECONFIG_UDP).
 */
static void app_main_RunConfigMode(uint8_t u8Flag)
{
    ESP_LOGW(TAG, "=== BOOT VÀO CHẾ ĐỘ CẤU HÌNH (flag=%u) ===", (unsigned)u8Flag);

    /* Chỉ init tối thiểu cần cho chế độ cấu hình */
    (void)app_logic_led_Init();
    app_led_state_Init();
    (void)app_logic_buzzer_Init();

    /* Task cảm ứng RÚT GỌN — chỉ để nút vật lý hủy cấu hình, không dùng
       chung app_logic_touch_Task đầy đủ (task đó không được Init ở nhánh này) */
    (void)app_logic_touch_InitCancelConfigMode();

    if (u8Flag == E_APP_RECONFIG_BLUFI) {
        app_led_state_SetState(E_LED_STATE_BLUFI_AUTO);
        esp_err_t eWifiRet = app_wifi_InitSta();
        if (eWifiRet != ESP_OK) {
            ESP_LOGW(TAG, "Init Wi-Fi STA cho BluFi gặp sự cố: %s", esp_err_to_name(eWifiRet));
        }
        (void)app_blufi_Init();
    } else if (u8Flag == E_APP_RECONFIG_UDP) {
        app_led_state_SetState(E_LED_STATE_CONNECT_MANUAL);
        (void)app_udp_Init();
    } else {
        ESP_LOGE(TAG, "Giá trị cờ reconfig không hợp lệ: %u", (unsigned)u8Flag);
    }

    ESP_LOGI(TAG, "=== HỆ THỐNG ĐÃ VÀO CHẾ ĐỘ CẤU HÌNH ===");
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    /* Vòng lặp riêng cho chế độ config — không có BLE Mesh/UDP/MQTT dance,
       vì mọi chuyển đổi mode khác đều đi qua esp_restart() từ nơi khác */
    while (true) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
    /* Không bao giờ chạy tới đây */
}

void app_main(void) {
  /* Kiểm tra nguyên nhân khởi động lại (đặc biệt là do Task Watchdog) */
  esp_reset_reason_t eResetReason = esp_reset_reason();
  if (eResetReason == ESP_RST_TASK_WDT) {
    ESP_LOGE(TAG, "=======================================================");
    ESP_LOGE(TAG, ">>> CẢNH BÁO: HỆ THỐNG VỪA RESET BỞI TASK WATCHDOG (TWDT)! <<<");
    ESP_LOGE(TAG, "=======================================================");
  } else {
    ESP_LOGI(TAG, "Lý do khởi động lại: %d", (int)eResetReason);
  }

  esp_task_wdt_config_t sTwdtConfig = {
        .timeout_ms = 5000,                               /* Timeout 5 giây */
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,  /* Giám sát Idle Task trên mọi Core */
        .trigger_panic = true                             /* Tự động Reset ESP32 khi bị treo */
    };

    /* Nạp cấu hình TWDT */
  ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&sTwdtConfig));

  ESP_LOGI("MAIN", "Đã khởi tạo Task Watchdog Timer (TWDT) 5s thành công.");

  ESP_LOGI(TAG, "=== BẮT ĐẦU KIỂM TRA KHỞI TẠO NVS ===");

  // 1. Khởi tạo NVS
  esp_err_t eRet = app_nvs_InitNvs();
  if (eRet != ESP_OK) {
    ESP_LOGE(TAG, "Khởi tạo NVS thất bại! Mã lỗi: %s", esp_err_to_name(eRet));
    return;
  }
  ESP_LOGI(TAG, "Khởi tạo NVS thành công!");

  /* ==================================================================
   * ĐỌC CỜ RECONFIG NGAY SAU NVS INIT, TRƯỚC MỌI THỨ KHÁC.
   * Nếu != NONE -> reset cờ về NONE NGAY LẬP TỨC (chống boot-loop nếu
   * mất điện giữa chừng lúc đang cấu hình) rồi rẽ hẳn sang nhánh riêng,
   * không bao giờ quay lại luồng NORMAL bên dưới trong cùng phiên boot.
   * ================================================================== */
  uint8_t u8ReconfigFlag = app_nvs_LoadReconfigFlag();
  if (u8ReconfigFlag != E_APP_RECONFIG_NONE) {
      (void)app_nvs_SaveReconfigFlag(E_APP_RECONFIG_NONE);
      app_main_RunConfigMode(u8ReconfigFlag);
      return; 
  }

  /* ================= TỪ ĐÂY TRỞ XUỐNG: LUỒNG NORMAL/IDLE ================= */

  if (app_nvs_LoadExtraConfig(&g_sExtraConfig) == ESP_OK) {
    if ((g_sExtraConfig.gate_1_control_mode == 3) || (g_sExtraConfig.gate_2_control_mode == 3) || (g_sExtraConfig.gate_3_control_mode == 3))
    {
      app_device_state_SetModeBit(DEVICE_MODE_LOCKED_CHILD, true);
      ESP_LOGI("APP_MAIN", "Đã khôi phục trạng thái KHÓA TRẺ EM từ NVS!");
    }
    ESP_LOGI(TAG, "Nạp Extra Config từ NVS vào RAM thành công");
    /* Kiểm tra và bật cờ DEVICE_MODE_LOCKED_RF nếu đang trong khung giờ khóa */
    (void)app_logic_extra_config_IsRFLocked();
  } else {
    ESP_LOGW(TAG, "Không tìm thấy Extra Config trong NVS (dùng mặc định)");
  }

  // 2. Khởi tạo các module nghiệp vụ tầng Application
  eRet = app_logic_relay_Init();
  if (eRet != ESP_OK) {
    ESP_LOGE(TAG, "Khởi tạo logic relay thất bại! Mã lỗi: %s", esp_err_to_name(eRet));
    return;
  }
  ESP_LOGI(TAG, "khởi tạo logic relay thành công");
  app_relay_state_Init();
  app_relay_state_SetState(E_RELAY_STATE_CLOSED);
  ESP_LOGI(TAG, "Đã set trạng thái cửa khi khởi động là đóng hoàn toàn");

  /* Task cảm ứng ĐẦY ĐỦ — chỉ chạy trong nhánh NORMAL/IDLE này.
     Giữ 3s/7s giờ chỉ ghi cờ NVS + esp_restart(), không set bitmask sống nữa. */
  eRet = app_logic_touch_Init();
  if (eRet != ESP_OK) {
    ESP_LOGE(TAG, "Khởi tạo logic cảm ứng thất bại! Mã lỗi: %s", esp_err_to_name(eRet));
    return;
  }
  ESP_LOGI(TAG, "Khởi tạo logic cảm ứng nút bấm thành công");

  eRet = app_logic_led_Init();
  if (eRet != ESP_OK) {
    ESP_LOGE(TAG, "Khởi tạo logic LED thất bại! Mã lỗi: %s", esp_err_to_name(eRet));
    return;
  }
  ESP_LOGI(TAG, "Khởi tạo logic LED thành công");
  app_led_state_Init();
  app_led_state_SetState(E_LED_STATE_NORMAL_IDLE);

  eRet = app_logic_buzzer_Init();
  if (eRet != ESP_OK) {
    ESP_LOGE(TAG, "Khởi tạo logic buzzer thất bại! Mã lỗi: %s",esp_err_to_name(eRet));
    return;
  }
  ESP_LOGI(TAG, "Khởi tạo logic buzzer thành công");

  eRet = app_logic_mqtt_Init();
  if (eRet != ESP_OK) {
    ESP_LOGE(TAG, "Khởi tạo logic MQTT thất bại! Mã lỗi: %s",esp_err_to_name(eRet));
    return;
  }
  ESP_LOGI(TAG, "Khởi tạo logic MQTT thành công");
  eRet = app_logic_schedule_Init();
  if (eRet != ESP_OK) {
    ESP_LOGE(TAG, "Khởi tạo lập lịch thất bại! Mã lỗi: %s",esp_err_to_name(eRet));
    return;
  }

  // 3. Điều phối luồng dựa trên NVS: đã provisioned Wi-Fi hay chưa
  if (!app_nvs_IsProvisionedWifiConfig()) {
      ESP_LOGI(TAG, "Chưa có Wi-Fi trong NVS -> Chế độ CHỜ (IDLE), chờ người dùng giữ nút để cấu hình");
      app_device_state_SetModeBit(DEVICE_MODE_UNCONNECTED, true);
      app_led_state_SetState(E_LED_STATE_UNCONNECTED);
  } else {
      ESP_LOGI(TAG, "Đã có sẵn cấu hình Wi-Fi -> Chế độ NORMAL");
      app_device_state_SetModeBit(DEVICE_MODE_UNCONNECTED, false);
      app_device_state_SetModeBit(DEVICE_MODE_NORMAL, true);
      app_led_state_SetState(E_LED_STATE_LOCKED);

      /* Init BLE Mesh SỚM, ngay khi heap còn sạch nhất, TRƯỚC Wi-Fi STA/TLS/MQTT
         -> tránh Malloc failed do heap phân mảnh (xem log crash trước đó) */
      ESP_LOGI(TAG, "Khởi tạo BLE Mesh sớm (heap còn sạch)...");
      (void)app_ble_mesh_Init();

      // 4. Khởi tạo Wi-Fi STA
      eRet = app_wifi_InitSta();
      if (eRet != ESP_OK) {
          ESP_LOGW(TAG, "Khởi tạo Wi-Fi STA gặp sự cố, kiểm tra trạng thái thiết bị...");
      } else {
          ESP_LOGI(TAG, "Khởi tạo wifi STA thành công");
      }

      if (app_wifi_WaitForConnect(10000U)) {
          ESP_LOGI(TAG, "Kết nối Wi-Fi thành công!");
          (void)app_main_StartMqttFromNvs();
      } else {
          ESP_LOGW(TAG, "Timeout chờ kết nối Wi-Fi, tiếp tục chạy các task nền.");
      }
  }

  ESP_LOGI(TAG, "=== HỆ THỐNG ĐÃ KHỞI ĐỘNG HOÀN TẤT ===");

  /* Đăng ký task chính app_main vào TWDT sau khi hoàn tất khởi động mạng và các module */
  ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

  /* Vòng lặp chính giờ RẤT ĐƠN GIẢN — không còn init/deinit BLE Mesh/BluFi/UDP
     động theo bitmask nữa, vì mọi chuyển mode (CONNECT_AUTO/CONNECT_MANUAL)
     giờ đi qua u8ReconfigFlag + esp_restart(), xử lý ở app_logic_touch.c và
     app_main_RunConfigMode() phía trên — không còn xảy ra tại runtime trong
     nhánh NORMAL này nữa. */
  while (true) {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(1000U));
  }
}