#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DF_APP_STORAGE_KEY_EXTRA_CONFIG "extra_cfg"

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
  uint32_t travel_time_ms;
} app_nvs_device_config_t;

// Lưu device config vào NVS
esp_err_t app_nvs_SaveDeviceConfig(const app_nvs_device_config_t *config);

// Đọc device config từ NVS
esp_err_t app_nvs_LoadDeviceConfig(app_nvs_device_config_t *config);

// Kiểm tra đã có device config trong NVS chưa
bool app_nvs_IsProvisionedDeviceConfig(void);


// Cấu trúc lưu trữ Extra Config
typedef struct {
  uint8_t buzzerEnb;
  uint8_t ledEnb;
  uint32_t ledRgbOn;
  uint32_t ledRgbOff;
  uint8_t led_lightness;
  
  uint8_t gate_1_type;
  uint8_t gate_1_control_mode;
  uint8_t gate_1_led_off;
  uint32_t gate_1_rgb_on;
  uint32_t gate_1_rgb_off;
  
  uint8_t gate_2_type;
  uint8_t gate_2_control_mode;
  uint8_t gate_2_led_off;
  uint32_t gate_2_rgb_on;
  uint32_t gate_2_rgb_off;

  uint8_t gate_3_type;
  uint8_t gate_3_control_mode;
  uint8_t gate_3_led_off;
  uint32_t gate_3_rgb_on;
  uint32_t gate_3_rgb_off;

  uint8_t nightModeEnb;
  uint32_t nightBegin;
  uint32_t nightEnd;
  int8_t nightTz;
  
  uint8_t warningEnb;
  uint32_t warningBegin;
  uint32_t warningEnd;

  uint8_t switch_1_lightness;
  uint8_t switch_2_lightness;
  uint8_t switch_3_lightness;

  uint8_t anti_animal_enb;
  uint32_t anti_animal_lock_time;
  uint32_t gate_countdown;
  uint32_t sgmCycle;
  uint32_t sgmCycleGap;
  uint8_t sgmUseCycleGap;
  uint8_t resetMode;
  uint8_t wlanMode;
  uint8_t lockRFEnb;
  uint32_t lockRFBegin;
  uint32_t lockRFEnd;
} app_extra_config_t;

extern app_extra_config_t g_sExtraConfig;

esp_err_t app_nvs_SaveExtraConfig(const app_extra_config_t *config);
esp_err_t app_nvs_LoadExtraConfig(app_extra_config_t *config);



#define DF_MAX_SCHEDULES 10U // Hỗ trợ tối đa 10 lịch hẹn giờ
/**
 * @brief Cấu trúc lưu trữ 1 lịch hẹn giờ tối ưu cho NVS Flash
 */
typedef struct {
    uint32_t u32Id;               // ID của lịch (VD: 4640)
    uint8_t  u8Activate;          // Trạng thái (1 = Bật, 0 = Tắt)
    uint8_t  u8LoopDays;          // Bitmask lặp lại (127 = Cả tuần, 0 = Chạy 1 lần)
    uint8_t  u8Hour;              // Giờ thực thi (0-23)
    uint8_t  u8Minute;            // Phút thực thi (0-59)
    char     acParam[16];         // Lệnh điều khiển (VD: "gate_open")
    int32_t  i32Value;            // Giá trị lệnh (VD: 1, 0, hoặc %)
} app_schedule_item_t;

/**
 * @brief   Lưu hoặc cập nhật một lịch hẹn giờ vào NVS
 * @param   psNewSchedule Con trỏ trỏ tới cấu trúc lịch mới cần lưu
 * @return  esp_err_t ESP_OK nếu thành công
 */
esp_err_t app_nvs_SaveSchedule(const app_schedule_item_t *psNewSchedule);


/**
 * @brief   Xóa một lịch hẹn giờ khỏi NVS dựa vào ID
 * @param   u32Id ID của lịch cần xóa
 * @return  esp_err_t ESP_OK nếu tìm thấy và xóa thành công, ESP_ERR_NOT_FOUND nếu không có
 */
esp_err_t app_nvs_DeleteSchedule(uint32_t u32Id);
esp_err_t app_nvs_DeleteAllSchedules(void);

/**
 * @brief đọc danh sách lịch
 * 
 */
esp_err_t app_nvs_GetAllSchedules(app_schedule_item_t *pasSchedules, uint8_t *pu8Count);

#ifdef __cplusplus
}
#endif