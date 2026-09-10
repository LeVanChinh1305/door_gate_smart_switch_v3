#include "app_logic_extra_config.h"
#include "app_nvs.h"         
#include "app_logic_led.h"   
#include "esp_log.h"
#include <stdlib.h>
#include "app_common.h" 
#include "app_logic_mqtt_publisher.h"
#include "app_mqtt.h"
#include <sys/time.h>
#include <time.h>
#include "app_device_state.h"
#include "app_nvs.h"
#include "app_relay_state.h"
#include "app_logic_buzzer.h"

static const char *TAG = "APP_LOGIC_EXTRA_CONFIG";
/* Biến Timer tĩnh quản lý lịch bật/tắt Khóa RF */
static TimerHandle_t g_hLockRFTimer = NULL;
// biến quản lý khóa tạm thời 
static TimerHandle_t g_hAntiAnimalTimer = NULL;
static char s_acExtraCfgResponseBuffer[1024];


static inline app_led_color_t unpack_rgb_to_color(uint32_t u32ColorVal) {
    app_led_color_t sColor;
    sColor.r = (uint8_t)((u32ColorVal >> 16) & 0xFF);
    sColor.g = (uint8_t)((u32ColorVal >> 8) & 0xFF);
    sColor.b = (uint8_t)(u32ColorVal & 0xFF);
    return sColor;
}

/**
 * @brief Cập nhật màu và độ sáng xuống phần cứng LED thông qua Queue của app_logic_led
 */
void app_logic_extra_config_ApplyLedConfig(void)
{
    /* 1. Nếu TẮT Đèn nền (ledEnb == 0) -> Đặt độ sáng = 0 */
    if (g_sExtraConfig.ledEnb == 0) {
        (void)app_logic_led_SetBrightness(0);
        (void)app_logic_led_SetColor(APP_LED_COLOR_OFF);
        ESP_LOGI(TAG, "Đã TẮT toàn bộ Đèn nền LED");
        return;
    }

    /* 2. Áp dụng độ sáng % (Quy đổi 0..100% sang 0..255) */
    uint8_t u8Bright255 = (uint8_t)((g_sExtraConfig.led_lightness * 255) / 100);
    (void)app_logic_led_SetBrightness(u8Bright255);

    /* 3. Đọc trạng thái Relay/Cửa hiện tại để chọn màu ON hay OFF */
    e_relay_state_t eRelayState = app_relay_state_GetState();
    uint32_t u32CurrentColorVal;
    if (eRelayState == E_RELAY_STATE_OPENING || eRelayState == E_RELAY_STATE_OPENED) {
        u32CurrentColorVal = g_sExtraConfig.ledRgbOn;
    } else {
        u32CurrentColorVal = g_sExtraConfig.ledRgbOff;
    }
    app_led_color_t sColor = unpack_rgb_to_color(u32CurrentColorVal);

    /* 4. Đẩy lệnh đổi màu toàn bộ dải LED vào Queue */
    (void)app_logic_led_SetColor(sColor);

    ESP_LOGI(TAG, "Cập nhật LED thành công: Brightness=%d%% (%d/255), RGB=0x%06X (relay=%s)",
             g_sExtraConfig.led_lightness, u8Bright255, (unsigned int)u32CurrentColorVal,
             app_relay_state_ToString(eRelayState));
}

static void handle_cmd_led_enable(const cJSON *pValue, bool *pbConfigChanged)
{
    const cJSON *item = cJSON_GetObjectItem(pValue, "ledEnb");
    if (cJSON_IsNumber(item) && (g_sExtraConfig.ledEnb != item->valueint)) {
        g_sExtraConfig.ledEnb = item->valueint;
        *pbConfigChanged = true;
        app_logic_extra_config_ApplyLedConfig();
        ESP_LOGI(TAG, "-> CẬP NHẬT BẬT/TẮT LED: %d", g_sExtraConfig.ledEnb);
    }
}

static void handle_cmd_led_lightness(const cJSON *pValue, bool *pbConfigChanged)
{
    const cJSON *item = cJSON_GetObjectItem(pValue, "led_lightness");
    if (cJSON_IsNumber(item) && (g_sExtraConfig.led_lightness != item->valueint)) {
        g_sExtraConfig.led_lightness = item->valueint;
        *pbConfigChanged = true;
        app_logic_extra_config_ApplyLedConfig();
        ESP_LOGI(TAG, "-> CẬP NHẬT ĐỘ SÁNG LED: %d%%", g_sExtraConfig.led_lightness);
    }
}

static void handle_cmd_led_rgb_on(const cJSON *pValue, bool *pbConfigChanged)
{
    const cJSON *item = cJSON_GetObjectItem(pValue, "ledRgbOn");
    if (cJSON_IsNumber(item)) {
        uint32_t u32Val = (uint32_t)item->valuedouble;
        if (g_sExtraConfig.ledRgbOn != u32Val) {
            g_sExtraConfig.ledRgbOn = u32Val;
            *pbConfigChanged = true;
            app_logic_extra_config_ApplyLedConfig();
            ESP_LOGI(TAG, "-> CẬP NHẬT MÀU LED BẬT: 0x%06X", (unsigned int)g_sExtraConfig.ledRgbOn);
        }
    }
}

static void handle_cmd_led_rgb_off(const cJSON *pValue, bool *pbConfigChanged)
{
    const cJSON *item = cJSON_GetObjectItem(pValue, "ledRgbOff");
    if (cJSON_IsNumber(item)) {
        uint32_t u32Val = (uint32_t)item->valuedouble;
        if (g_sExtraConfig.ledRgbOff != u32Val) {
            g_sExtraConfig.ledRgbOff = u32Val;
            *pbConfigChanged = true;
            app_logic_extra_config_ApplyLedConfig();
            ESP_LOGI(TAG, "-> CẬP NHẬT MÀU LED TẮT: 0x%06X", (unsigned int)g_sExtraConfig.ledRgbOff);
        }
    }
}

static void handle_cmd_reset_all(const cJSON *pValue, bool *pbConfigChanged)
{
    if (cJSON_GetObjectItem(pValue, "resetAll") != NULL) {
        ESP_LOGI(TAG, "-> LỆNH KHÔI PHỤC CẤU HÌNH MẶC ĐỊNH (resetAll)");
        app_nvs_SetDefaultExtraConfig(&g_sExtraConfig);
        app_logic_extra_config_ApplyLedConfig();
        *pbConfigChanged = true;
    }
}

static void handle_cmd_buzzer_enable(const cJSON *pValue, bool *pbConfigChanged)
{
    const cJSON *item = cJSON_GetObjectItem(pValue, "buzzerEnb");
    if (cJSON_IsNumber(item) && (g_sExtraConfig.buzzerEnb != item->valueint)) {
        g_sExtraConfig.buzzerEnb = item->valueint;
        *pbConfigChanged = true;

        if (g_sExtraConfig.buzzerEnb == 1) {
            /* Phát 1 tiếng bíp ngắn 80ms thông qua Queue để báo hiệu BẬT CÒI */
            (void)app_logic_buzzer_Beep(80);
            ESP_LOGI(TAG, "-> ĐÃ BẬT CỜI BÍP (buzzerEnb = 1)");
        } else {
            /* Tắt còi lập tức nếu còi đang kêu */
            (void)app_logic_buzzer_Off();
            ESP_LOGI(TAG, "-> ĐÃ TẮT CỜI BÍP (buzzerEnb = 0)");
        }
    }
}

/**
 * @brief Callback hết thời gian mở cửa sổ thao tác -> TỰ ĐỘNG KHÓA LẠI
 */
static void anti_animal_timer_callback(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "==> Hết thời gian cho phép thao tác! TỰ ĐỘNG KHÓA NÚT CẢM ỨNG.");
    /* Dựng lại cờ KHÓA TẠM THỜI */
    app_device_state_SetModeBit(DEVICE_MODE_LOCKED_TEMP, true);
}

/**
 * @brief Tạm thời mở khóa nút cảm ứng và đếm ngược N giây để khóa lại
 */
void app_logic_extra_config_StartAntiAnimalWindow(void)
{
    /* Nếu tính năng TẮT -> Tắt bitmask khóa và dừng Timer */
    if (g_sExtraConfig.anti_animal_enb == 0 || g_sExtraConfig.anti_animal_lock_time == 0) {
        app_device_state_SetModeBit(DEVICE_MODE_LOCKED_TEMP, false);
        if (g_hAntiAnimalTimer != NULL) {
            xTimerStop(g_hAntiAnimalTimer, 0);
        }
        return;
    }

    /* 1. TẠM THỜI MỞ KHÓA NÚT CẢM ỨNG */
    app_device_state_SetModeBit(DEVICE_MODE_LOCKED_TEMP, false);
    ESP_LOGI(TAG, "Mở khóa nút cảm ứng! Cho phép thao tác trong %lu giây...", (unsigned long)g_sExtraConfig.anti_animal_lock_time);

    /* 2. ĐẶT LỊCH KHÓA LẠI SAU N GIÂY */
    uint32_t u32TimeoutMs = g_sExtraConfig.anti_animal_lock_time * 1000U;

    if (g_hAntiAnimalTimer == NULL) {
        g_hAntiAnimalTimer = xTimerCreate("anti_anim_tmr", pdMS_TO_TICKS(u32TimeoutMs), pdFALSE, NULL, anti_animal_timer_callback);
    } else {
        xTimerChangePeriod(g_hAntiAnimalTimer, pdMS_TO_TICKS(u32TimeoutMs), 0);
    }
    xTimerStart(g_hAntiAnimalTimer, 0);
}


/**
 * @brief Callback xử lý khi Timer đếm ngược hết giờ
 */
static void lock_rf_timer_callback(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "==> Timer kích hoạt: Cập nhật trạng thái Khóa RF!");
    
    /* 1. Cập nhật Bitmask trạng thái hiện tại */
    (void)app_logic_extra_config_IsRFLocked();

    /* 2. Tiếp tục đặt lịch cho mốc thời gian tiếp theo */
    app_logic_extra_config_ScheduleNextRFLock();
}

/**
 * @brief Tính toán số giây đến mốc thời gian tiếp theo và lập lịch Timer
 */
void app_logic_extra_config_ScheduleNextRFLock(void)
{
    /* 1. Nếu tính năng TẮT -> Dừng Timer (nếu đang chạy) */
    if (g_sExtraConfig.lockRFEnb == 0) {
        if (g_hLockRFTimer != NULL) {
            xTimerStop(g_hLockRFTimer, 0);
        }
        return;
    }

    /* 2. Lấy thời gian hiện tại từ hệ thống */
    time_t tNow;
    struct tm sTimeInfo;
    time(&tNow);
    localtime_r(&tNow, &sTimeInfo);

    /* Nếu chưa có giờ SNTP -> Bỏ qua, chờ SNTP Callback gọi lại */
    if (sTimeInfo.tm_year < (2024 - 1900)) {
        return;
    }

    /* 3. Quy đổi số giây trong ngày hiện tại */
    uint32_t u32CurrentSec = (uint32_t)(sTimeInfo.tm_hour * 3600 + sTimeInfo.tm_min * 60 + sTimeInfo.tm_sec);
    uint32_t u32TzOffsetSec = (uint32_t)(g_sExtraConfig.nightTz * 3600);
    uint32_t u32BeginSec = (uint32_t)((g_sExtraConfig.lockRFBegin + u32TzOffsetSec) % 86400U);
    uint32_t u32EndSec = (uint32_t)((g_sExtraConfig.lockRFEnd + u32TzOffsetSec) % 86400U);

    uint32_t u32NextDelaySec = 0;

    /* 4. Tính toán số giây còn lại tới mốc chuyển đổi tiếp theo */
    bool bCurrentlyLocked = app_device_state_HasMode(DEVICE_MODE_LOCKED_RF);
    if (bCurrentlyLocked) {
        /* Đang KHÓA -> Tính thời gian chờ đến mốc MỞ KHÓA (lockRFEnd) */
        u32NextDelaySec = (u32EndSec > u32CurrentSec) ? (u32EndSec - u32CurrentSec) : (86400U - u32CurrentSec + u32EndSec);
    } else {
        /* Đang MỞ -> Tính thời gian chờ đến mốc BẬT KHÓA (lockRFBegin) */
        u32NextDelaySec = (u32BeginSec > u32CurrentSec) ? (u32BeginSec - u32CurrentSec) : (86400U - u32CurrentSec + u32BeginSec);
    }

    /* Bảo đảm khoảng delay tối thiểu là 1 giây */
    if (u32NextDelaySec == 0) u32NextDelaySec = 1;

    ESP_LOGI(TAG, "Lập lịch Timer Khóa RF thành công: Thức dậy sau %u giây (%u giờ %u phút)",
             (unsigned int)u32NextDelaySec, 
             (unsigned int)(u32NextDelaySec / 3600), 
             (unsigned int)((u32NextDelaySec % 3600) / 60));

    /* 5. Khởi tạo hoặc khởi động lại Timer với chu kỳ mới */
    if (g_hLockRFTimer == NULL) {
        g_hLockRFTimer = xTimerCreate("lock_rf_tmr", pdMS_TO_TICKS(u32NextDelaySec * 1000U), pdFALSE, NULL, lock_rf_timer_callback);
    } else {
        xTimerChangePeriod(g_hLockRFTimer, pdMS_TO_TICKS(u32NextDelaySec * 1000U), 0);
    }
    xTimerStart(g_hLockRFTimer, 0);
}

/* In file: app_logic_extra_config.c -> app_logic_extra_config_ProcessGet() */

void app_logic_extra_config_ProcessGet(char *pcOutBuffer, size_t zMaxLen)
{
    if (pcOutBuffer == NULL || zMaxLen == 0) {
        return;
    }

    app_nvs_device_config_t sDevConfig;
    char acMac[DF_APP_STORAGE_DEV_EXT_ADDR_SIZE] = "UNKNOWN";
    if (app_nvs_LoadDeviceConfig(&sDevConfig) == ESP_OK) {
        snprintf(acMac, sizeof(acMac), "%s", sDevConfig.dev_ext_addr);
    }

    int iOffset = 0;

    /* ĐỢT 1: Thông tin chung & Cấu hình Cổng 1 + Cổng 2 */
    iOffset = snprintf(pcOutBuffer, zMaxLen,
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
        "\"gate_2_rgb_off\":%lu,",
        acMac,
        g_sExtraConfig.buzzerEnb, g_sExtraConfig.ledEnb, g_sExtraConfig.ledRgbOn, g_sExtraConfig.ledRgbOff, g_sExtraConfig.led_lightness,
        g_sExtraConfig.gate_1_type, g_sExtraConfig.gate_1_control_mode, g_sExtraConfig.gate_1_led_off, g_sExtraConfig.gate_1_rgb_on, g_sExtraConfig.gate_1_rgb_off,
        g_sExtraConfig.gate_2_type, g_sExtraConfig.gate_2_control_mode, g_sExtraConfig.gate_2_led_off, g_sExtraConfig.gate_2_rgb_on, g_sExtraConfig.gate_2_rgb_off
    );

    /* ĐỢT 2: Cấu hình Cổng 3, Ban đêm & Cảnh báo */
    if (iOffset > 0 && (size_t)iOffset < zMaxLen) {
        int iNext = snprintf(pcOutBuffer + iOffset, zMaxLen - (size_t)iOffset,
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
            "\"switch_3_lightness\":%d,",
            g_sExtraConfig.gate_3_type, g_sExtraConfig.gate_3_control_mode, g_sExtraConfig.gate_3_led_off, g_sExtraConfig.gate_3_rgb_on, g_sExtraConfig.gate_3_rgb_off,
            g_sExtraConfig.nightModeEnb, g_sExtraConfig.nightBegin, g_sExtraConfig.nightEnd, g_sExtraConfig.nightTz,
            g_sExtraConfig.warningEnb, g_sExtraConfig.warningBegin, g_sExtraConfig.warningEnd,
            g_sExtraConfig.switch_1_lightness, g_sExtraConfig.switch_2_lightness, g_sExtraConfig.switch_3_lightness
        );
        if (iNext > 0) {
            iOffset += iNext;
        }
    }

    /* ĐỢT 3: Anti Animal, SGM & Lock RF (Kết thúc JSON) */
    if (iOffset > 0 && (size_t)iOffset < zMaxLen) {
        (void)snprintf(pcOutBuffer + iOffset, zMaxLen - (size_t)iOffset,
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
            g_sExtraConfig.anti_animal_enb, g_sExtraConfig.anti_animal_lock_time, g_sExtraConfig.gate_countdown,
            g_sExtraConfig.sgmCycle, g_sExtraConfig.sgmCycleGap, g_sExtraConfig.sgmUseCycleGap,
            g_sExtraConfig.resetMode, g_sExtraConfig.wlanMode, g_sExtraConfig.lockRFEnb, g_sExtraConfig.lockRFBegin, g_sExtraConfig.lockRFEnd
        );
    }
}

void app_logic_extra_config_ProcessSet(const cJSON *pValue)
{
    if (pValue == NULL) {
        return;
    }
    
    bool bConfigChanged = false;

    /* Cập nhật toàn bộ các trường dữ liệu bằng Macro */
    UPDATE_CFG_INT(pValue, "buzzerEnb", g_sExtraConfig.buzzerEnb);
    /* LƯU Ý: Không dùng UPDATE_CFG_* cho ledEnb, ledRgbOn, ledRgbOff, led_lightness ở đây.
     * Các trường này được xử lý bởi handle_cmd_led_* bên dưới, vừa cập nhật g_sExtraConfig
     * vừa gọi ApplyLedConfig(). Nếu dùng UPDATE_CFG_* trước, giá trị đã bằng nhau rồi
     * nên điều kiện kiểm tra trong handle_cmd_led_* luôn false và ApplyLedConfig không chạy. */

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

    handle_cmd_reset_all(pValue, &bConfigChanged);

    if (cJSON_GetObjectItem(pValue, "ledEnb") != NULL) {
        handle_cmd_led_enable(pValue, &bConfigChanged);
    }
    if (cJSON_GetObjectItem(pValue, "led_lightness") != NULL) {
        handle_cmd_led_lightness(pValue, &bConfigChanged);
    }
    if (cJSON_GetObjectItem(pValue, "ledRgbOn") != NULL) {
        handle_cmd_led_rgb_on(pValue, &bConfigChanged);
    }
    if (cJSON_GetObjectItem(pValue, "ledRgbOff") != NULL) {
        handle_cmd_led_rgb_off(pValue, &bConfigChanged);
    }

    if((cJSON_GetObjectItem(pValue, "lockRFEnb") != NULL)||(cJSON_GetObjectItem(pValue, "lockRFBegin") != NULL)||(cJSON_GetObjectItem(pValue, "lockRFEnd") != NULL)){
        /* 1. Kiểm tra và áp dụng Bitmask ngay lập tức */
        (void)app_logic_extra_config_IsRFLocked();

        /* 2. Tính toán lại thời gian chờ cho Timer theo cấu hình mới */
        app_logic_extra_config_ScheduleNextRFLock();
    }

    if (g_sExtraConfig.anti_animal_enb == 1) {
        /* Khóa ngay lập tức nếu vừa bật tính năng */
        app_device_state_SetModeBit(DEVICE_MODE_LOCKED_TEMP, true);
    }else {
        app_device_state_SetModeBit(DEVICE_MODE_LOCKED_TEMP, false);
            if (g_hAntiAnimalTimer != NULL) {
            xTimerStop(g_hAntiAnimalTimer, 0);
        }
    }

    // Kiểm tra nếu có bất kỳ kênh nào set control_mode = 3 (DISABLE TOUCH)
    if ((cJSON_GetObjectItem(pValue, "gate_1_control_mode") != NULL) ||
        (cJSON_GetObjectItem(pValue, "gate_2_control_mode") != NULL) ||
        (cJSON_GetObjectItem(pValue, "gate_3_control_mode") != NULL))
    {
        if ((g_sExtraConfig.gate_1_control_mode == 3) || 
            (g_sExtraConfig.gate_2_control_mode == 3) || 
            (g_sExtraConfig.gate_3_control_mode == 3)) 
        {
            app_device_state_SetModeBit(DEVICE_MODE_LOCKED_CHILD, true);
            ESP_LOGI(TAG, ">>> ĐÃ BẬT KHÓA TRẺ EM: Vô hiệu hóa nút bấm cảm ứng vật lý (DISABLE TOUCH)!");
        } else {
            app_device_state_SetModeBit(DEVICE_MODE_LOCKED_CHILD, false);
            ESP_LOGI(TAG, ">>> ĐÃ TẮT KHÓA TRẺ EM: Nút bấm cảm ứng hoạt động bình thường.");
        }
    }

    if (cJSON_GetObjectItem(pValue, "buzzerEnb") != NULL) {
        handle_cmd_buzzer_enable(pValue, &bConfigChanged);
    }

    /* Lưu vào NVS nếu có bất kỳ biến nào bị thay đổi so với cấu hình hiện tại */
    esp_err_t eErr = ESP_OK;
    if (bConfigChanged) {
        eErr = app_nvs_SaveExtraConfig(&g_sExtraConfig);
        if (eErr == ESP_OK) {
            ESP_LOGI(TAG, "Đã lưu bản cập nhật ExtraConfig xuống NVS");
        } else {
            ESP_LOGE(TAG, "Lỗi khi lưu ExtraConfig xuống NVS");
        }
    } else {
        ESP_LOGI(TAG, "Cấu hình không có thay đổi, bỏ qua việc lưu NVS");
    }
    (void)memset(s_acExtraCfgResponseBuffer, 0, sizeof(s_acExtraCfgResponseBuffer));

    app_logic_extra_config_ProcessGet(s_acExtraCfgResponseBuffer, sizeof(s_acExtraCfgResponseBuffer));

    /* Bắn bản tin hoàn chỉnh 600+ bytes lên Response Topic */
    (void)app_logic_mqtt_publisher_SendResponse(s_acExtraCfgResponseBuffer);
}


bool app_logic_extra_config_IsRFLocked(void)
{
    /* 1. Nếu tính năng TẮT -> Tắt bit DEVICE_MODE_LOCKED_RF */
    if (g_sExtraConfig.lockRFEnb == 0) {
        app_device_state_SetModeBit(DEVICE_MODE_LOCKED_RF, false);
        return false;
    }

    /* 2. Lấy thời gian hiện tại từ hệ thống SNTP */
    time_t tNow;
    struct tm sTimeInfo;
    time(&tNow);
    
    /* Chuyển sang giờ địa phương (local time dựa theo TZ cấu hình) */
    localtime_r(&tNow, &sTimeInfo);

    /* Kiểm tra xem hệ thống đã đồng bộ thời gian thực chưa */
    if (sTimeInfo.tm_year < (2024 - 1900)) {
        return false;
    }

    /* 3. Tính tổng số giây trong ngày hiện tại (0 -> 86399 giây) */
    uint32_t u32CurrentSecOfDay = (uint32_t)(sTimeInfo.tm_hour * 3600 + sTimeInfo.tm_min * 60 + sTimeInfo.tm_sec);

    /* 4. Trích xuất mốc giây bắt đầu & kết thúc trong ngày từ ExtraConfig (Cộng múi giờ nếu giá trị là Epoch Timestamp) */
    uint32_t u32TzOffsetSec = (uint32_t)(g_sExtraConfig.nightTz * 3600); /* Múi giờ UTC+7 */
    uint32_t u32BeginSec = (uint32_t)((g_sExtraConfig.lockRFBegin + u32TzOffsetSec) % 86400U);
    uint32_t u32EndSec = (uint32_t)((g_sExtraConfig.lockRFEnd + u32TzOffsetSec) % 86400U);

    bool bIsLocked = false;
    if (u32BeginSec <= u32EndSec) {
        /* Khung giờ trong cùng 1 ngày (Ví dụ: 08:00 -> 17:00) */
        if (u32CurrentSecOfDay >= u32BeginSec && u32CurrentSecOfDay <= u32EndSec) {
            bIsLocked = true;
        }
    } else {
        /* Khung giờ qua đêm (Ví dụ: 22:00 -> 05:00 sáng hôm sau) */
        if (u32CurrentSecOfDay >= u32BeginSec || u32CurrentSecOfDay <= u32EndSec) {
            bIsLocked = true;
        }
    }

    /* 5. Cập nhật trực tiếp bitmask trạng thái thiết bị */
    app_device_state_SetModeBit(DEVICE_MODE_LOCKED_RF, bIsLocked);

    return bIsLocked;
}

/**
 * @brief Kiểm tra xem thời gian thực hiện tại có thuộc khung giờ Cảnh báo ban đêm hay không
 */
bool app_logic_extra_config_IsWarningNightActive(void)
{
    /* 1. Nếu tính năng TẮT -> Trả về false ngay lập tức */
    if (g_sExtraConfig.warningEnb == 0) {
        return false;
    }

    /* 2. Lấy thời gian hệ thống hiện tại */
    time_t tNow;
    struct tm sTimeInfo;
    time(&tNow);
    localtime_r(&tNow, &sTimeInfo);

    /* Bỏ qua nếu chưa đồng bộ SNTP chuẩn (>2024) */
    if (sTimeInfo.tm_year < (2024 - 1900)) {
        return false;
    }

    /* 3. Quy đổi thời gian hiện tại ra số giây trong ngày (0 -> 86399 giây) */
    uint32_t u32CurrentSecOfDay = (uint32_t)(sTimeInfo.tm_hour * 3600 + sTimeInfo.tm_min * 60 + sTimeInfo.tm_sec);

    /* 4. Quy đổi mốc warningBegin và warningEnd sang giây trong ngày (Cộng múi giờ UTC+7) */
    uint32_t u32TzOffsetSec = (uint32_t)(g_sExtraConfig.nightTz * 3600);
    uint32_t u32BeginSec = (uint32_t)((g_sExtraConfig.warningBegin + u32TzOffsetSec) % 86400U);
    uint32_t u32EndSec = (uint32_t)((g_sExtraConfig.warningEnd + u32TzOffsetSec) % 86400U);

    bool bIsWarningActive = false;

    /* 5. So sánh khung giờ (Bao phủ cả khung giờ trong ngày lẫn vắt qua đêm như 18:33 -> 05:50) */
    if (u32BeginSec <= u32EndSec) {
        /* Khung giờ cùng 1 ngày */
        if (u32CurrentSecOfDay >= u32BeginSec && u32CurrentSecOfDay <= u32EndSec) {
            bIsWarningActive = true;
        }
    } else {
        /* Khung giờ vắt qua đêm */
        if (u32CurrentSecOfDay >= u32BeginSec || u32CurrentSecOfDay <= u32EndSec) {
            bIsWarningActive = true;
        }
    }

    if (bIsWarningActive) {
        ESP_LOGI(TAG, "CẢNH BÁO BAN ĐÊM ACTIVE (%02d:%02d:%02d)",  sTimeInfo.tm_hour, sTimeInfo.tm_min, sTimeInfo.tm_sec);
    }

    return bIsWarningActive;
}

/**
 * @brief Kiểm tra cấu hình buzzerEnb và phát tiếng bíp qua Queue an toàn
 */
void app_logic_extra_config_TriggerBuzzer(uint32_t u32DurationMs)
{
    /* Nếu người dùng TẮT còi trên App (buzzerEnb == 0) -> Bỏ qua */
    if (g_sExtraConfig.buzzerEnb == 0) {
        return;
    }

    /* Đẩy lệnh bíp vào Queue xử lý bất đồng bộ, không block task hiện tại */
    (void)app_logic_buzzer_Beep(u32DurationMs);
}

void app_logic_extra_config_TriggerBuzzerRepeat(uint32_t u32DurationMs, uint32_t u32DelayMs, uint8_t u8RepeatCount)
{
    /* Nếu người dùng TẮT còi trên App (buzzerEnb == 0) -> Bỏ qua */
    if (g_sExtraConfig.buzzerEnb == 0) {
        return;
    }

    /* Đẩy lệnh bíp lặp lại vào Queue xử lý bất đồng bộ */
    (void)app_logic_buzzer_BeepRepeat(u32DurationMs, u32DelayMs, u8RepeatCount);
}