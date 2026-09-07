#include "app_logic_extra_config.h"
#include "app_nvs.h"         
#include "app_logic_led.h"   
#include "esp_log.h"
#include <stdlib.h>
#include "app_common.h" 

static const char *TAG = "APP_LOGIC_EXTRA_CFG";

void app_logic_extra_config_ProcessGet(char *pcOutBuffer, size_t zMaxLen)
{
    if (pcOutBuffer == NULL || zMaxLen == 0) {
        return;
    }

    /* Lấy địa chỉ MAC để điền vào JSON */
    app_nvs_device_config_t sDevConfig;
    char acMac[DF_APP_STORAGE_DEV_EXT_ADDR_SIZE] = "UNKNOWN";
    if (app_nvs_LoadDeviceConfig(&sDevConfig) == ESP_OK) {
        snprintf(acMac, sizeof(acMac), "%s", sDevConfig.dev_ext_addr);
    }

    /* Định dạng trực tiếp dữ liệu cấu hình vào mảng bằng snprintf */
    snprintf(pcOutBuffer, zMaxLen,
        "{"
        "\"name\":\"CmdSetExtraConfig\","
        "\"devT\":3097,"
        "\"devExtAddr\":\"%s\","
        "\"buzzerEnb\":%d,"
        "\"ledEnb\":%d,"
        "\"ledRgbOn\":%lu,"
        "\"ledRgbOff\":%lu,"
        "\"led_lightness\":%d,"
        "\"gate_1_type\":%d,"
        "\"gate_1_control_mode\":%d,"
        "\"gate_1_led_off\":%d,"
        "\"gate_1_rgb_on\":%lu,"
        "\"gate_1_rgb_off\":%lu,"
        "\"gate_2_type\":%d,"
        "\"gate_2_control_mode\":%d,"
        "\"gate_2_led_off\":%d,"
        "\"gate_2_rgb_on\":%lu,"
        "\"gate_2_rgb_off\":%lu,"
        "\"gate_3_type\":%d,"
        "\"gate_3_control_mode\":%d,"
        "\"gate_3_led_off\":%d,"
        "\"gate_3_rgb_on\":%lu,"
        "\"gate_3_rgb_off\":%lu,"
        "\"nightModeEnb\":%d,"
        "\"nightBegin\":%lu,"
        "\"nightEnd\":%lu,"
        "\"nightTz\":%d,"
        "\"warningEnb\":%d,"
        "\"warningBegin\":%lu,"
        "\"warningEnd\":%lu,"
        "\"switch_1_lightness\":%d,"
        "\"switch_2_lightness\":%d,"
        "\"switch_3_lightness\":%d,"
        "\"anti_animal_enb\":%d,"
        "\"anti_animal_lock_time\":%lu,"
        "\"gate_countdown\":%lu,"
        "\"sgmCycle\":%lu,"
        "\"sgmCycleGap\":%lu,"
        "\"sgmUseCycleGap\":%d,"
        "\"resetMode\":%d,"
        "\"wlanMode\":%d,"
        "\"lockRFEnb\":%d,"
        "\"lockRFBegin\":%lu,"
        "\"lockRFEnd\":%lu"
        "}",
        acMac,
        g_sExtraConfig.buzzerEnb, g_sExtraConfig.ledEnb, g_sExtraConfig.ledRgbOn, g_sExtraConfig.ledRgbOff, g_sExtraConfig.led_lightness,
        g_sExtraConfig.gate_1_type, g_sExtraConfig.gate_1_control_mode, g_sExtraConfig.gate_1_led_off, g_sExtraConfig.gate_1_rgb_on, g_sExtraConfig.gate_1_rgb_off,
        g_sExtraConfig.gate_2_type, g_sExtraConfig.gate_2_control_mode, g_sExtraConfig.gate_2_led_off, g_sExtraConfig.gate_2_rgb_on, g_sExtraConfig.gate_2_rgb_off,
        g_sExtraConfig.gate_3_type, g_sExtraConfig.gate_3_control_mode, g_sExtraConfig.gate_3_led_off, g_sExtraConfig.gate_3_rgb_on, g_sExtraConfig.gate_3_rgb_off,
        g_sExtraConfig.nightModeEnb, g_sExtraConfig.nightBegin, g_sExtraConfig.nightEnd, g_sExtraConfig.nightTz,
        g_sExtraConfig.warningEnb, g_sExtraConfig.warningBegin, g_sExtraConfig.warningEnd,
        g_sExtraConfig.switch_1_lightness, g_sExtraConfig.switch_2_lightness, g_sExtraConfig.switch_3_lightness,
        g_sExtraConfig.anti_animal_enb, g_sExtraConfig.anti_animal_lock_time, g_sExtraConfig.gate_countdown,
        g_sExtraConfig.sgmCycle, g_sExtraConfig.sgmCycleGap, g_sExtraConfig.sgmUseCycleGap,
        g_sExtraConfig.resetMode, g_sExtraConfig.wlanMode, g_sExtraConfig.lockRFEnb, g_sExtraConfig.lockRFBegin, g_sExtraConfig.lockRFEnd
    );
}
void app_logic_extra_config_ProcessSet(const cJSON *pValue)
{
    if (pValue == NULL) {
        return;
    }
    
    bool bConfigChanged = false;

    /* Cập nhật toàn bộ các trường dữ liệu bằng Macro */
    UPDATE_CFG_INT(pValue, "buzzerEnb", g_sExtraConfig.buzzerEnb);
    UPDATE_CFG_INT(pValue, "ledEnb", g_sExtraConfig.ledEnb);
    UPDATE_CFG_UINT32(pValue, "ledRgbOn", g_sExtraConfig.ledRgbOn);
    UPDATE_CFG_UINT32(pValue, "ledRgbOff", g_sExtraConfig.ledRgbOff);
    UPDATE_CFG_INT(pValue, "led_lightness", g_sExtraConfig.led_lightness);

    UPDATE_CFG_INT(pValue, "gate_1_type", g_sExtraConfig.gate_1_type);
    UPDATE_CFG_INT(pValue, "gate_1_control_mode", g_sExtraConfig.gate_1_control_mode);
    UPDATE_CFG_INT(pValue, "gate_1_led_off", g_sExtraConfig.gate_1_led_off);
    UPDATE_CFG_UINT32(pValue, "gate_1_rgb_on", g_sExtraConfig.gate_1_rgb_on);
    UPDATE_CFG_UINT32(pValue, "gate_1_rgb_off", g_sExtraConfig.gate_1_rgb_off);

    UPDATE_CFG_INT(pValue, "gate_2_type", g_sExtraConfig.gate_2_type);
    UPDATE_CFG_INT(pValue, "gate_2_control_mode", g_sExtraConfig.gate_2_control_mode);
    UPDATE_CFG_INT(pValue, "gate_2_led_off", g_sExtraConfig.gate_2_led_off);
    UPDATE_CFG_UINT32(pValue, "gate_2_rgb_on", g_sExtraConfig.gate_2_rgb_on);
    UPDATE_CFG_UINT32(pValue, "gate_2_rgb_off", g_sExtraConfig.gate_2_rgb_off);

    UPDATE_CFG_INT(pValue, "gate_3_type", g_sExtraConfig.gate_3_type);
    UPDATE_CFG_INT(pValue, "gate_3_control_mode", g_sExtraConfig.gate_3_control_mode);
    UPDATE_CFG_INT(pValue, "gate_3_led_off", g_sExtraConfig.gate_3_led_off);
    UPDATE_CFG_UINT32(pValue, "gate_3_rgb_on", g_sExtraConfig.gate_3_rgb_on);
    UPDATE_CFG_UINT32(pValue, "gate_3_rgb_off", g_sExtraConfig.gate_3_rgb_off);

    UPDATE_CFG_INT(pValue, "nightModeEnb", g_sExtraConfig.nightModeEnb);
    UPDATE_CFG_UINT32(pValue, "nightBegin", g_sExtraConfig.nightBegin);
    UPDATE_CFG_UINT32(pValue, "nightEnd", g_sExtraConfig.nightEnd);
    UPDATE_CFG_INT(pValue, "nightTz", g_sExtraConfig.nightTz);

    UPDATE_CFG_INT(pValue, "warningEnb", g_sExtraConfig.warningEnb);
    UPDATE_CFG_UINT32(pValue, "warningBegin", g_sExtraConfig.warningBegin);
    UPDATE_CFG_UINT32(pValue, "warningEnd", g_sExtraConfig.warningEnd);

    UPDATE_CFG_INT(pValue, "switch_1_lightness", g_sExtraConfig.switch_1_lightness);
    UPDATE_CFG_INT(pValue, "switch_2_lightness", g_sExtraConfig.switch_2_lightness);
    UPDATE_CFG_INT(pValue, "switch_3_lightness", g_sExtraConfig.switch_3_lightness);

    UPDATE_CFG_INT(pValue, "anti_animal_enb", g_sExtraConfig.anti_animal_enb);
    UPDATE_CFG_UINT32(pValue, "anti_animal_lock_time", g_sExtraConfig.anti_animal_lock_time);
    UPDATE_CFG_UINT32(pValue, "gate_countdown", g_sExtraConfig.gate_countdown);
    UPDATE_CFG_UINT32(pValue, "sgmCycle", g_sExtraConfig.sgmCycle);
    UPDATE_CFG_UINT32(pValue, "sgmCycleGap", g_sExtraConfig.sgmCycleGap);
    UPDATE_CFG_INT(pValue, "sgmUseCycleGap", g_sExtraConfig.sgmUseCycleGap);

    UPDATE_CFG_INT(pValue, "resetMode", g_sExtraConfig.resetMode);
    UPDATE_CFG_INT(pValue, "wlanMode", g_sExtraConfig.wlanMode);
    UPDATE_CFG_INT(pValue, "lockRFEnb", g_sExtraConfig.lockRFEnb);
    UPDATE_CFG_UINT32(pValue, "lockRFBegin", g_sExtraConfig.lockRFBegin);
    UPDATE_CFG_UINT32(pValue, "lockRFEnd", g_sExtraConfig.lockRFEnd);

    /* Lưu vào NVS nếu có bất kỳ biến nào bị thay đổi so với cấu hình hiện tại */
    if (bConfigChanged) {
        if (app_nvs_SaveExtraConfig(&g_sExtraConfig) == ESP_OK) {
            ESP_LOGI(TAG, "Đã lưu bản cập nhật ExtraConfig xuống NVS");
        } else {
            ESP_LOGE(TAG, "Lỗi khi lưu ExtraConfig xuống NVS");
        }
    } else {
        ESP_LOGI(TAG, "Cấu hình không có thay đổi, bỏ qua việc lưu NVS");
    }
}