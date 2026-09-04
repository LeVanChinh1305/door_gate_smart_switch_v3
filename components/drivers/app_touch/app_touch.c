#include "app_touch.h"
#include "app_common.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DF_TOUCH_REGISTER_WRITE_SIZE       (2U)
#define DF_TOUCH_REGISTER_SIZE             (1U)
#define DF_TOUCH_DEVICE_ID_SIZE            (2U)
#define DF_TOUCH_RETRY_COUNT               (8U)
#define DF_TOUCH_RETRY_DELAY_MS            (2U)
#define DF_TOUCH_BOOT_DELAY_MS             (50U)
#define DF_TOUCH_RESET_DELAY_MS            (100U)
#define DF_TOUCH_GLITCH_IGNORE_COUNT       (7U)
#define DF_TOUCH_COMMAND_IDLE_RETRY_COUNT  (20U)
#define DF_TOUCH_COMMAND_RETRY_COUNT       (50U)
#define DF_TOUCH_COMMAND_RETRY_DELAY_MS    (5U)

static const char *TAG = "APP_TOUCH";

static i2c_master_bus_handle_t g_bus_handle = NULL;
static i2c_master_dev_handle_t g_dev_handle = NULL;
static bool g_bIsReady = false;

static void app_touch_Cleanup(void)
{
    if (g_dev_handle != NULL) {
        i2c_master_bus_rm_device(g_dev_handle);
        g_dev_handle = NULL;
    }
    if (g_bus_handle != NULL) {
        i2c_del_master_bus(g_bus_handle);
        g_bus_handle = NULL;
    }
}

/* ==================== Low-level I2C ==================== */

static esp_err_t app_touch_I2cWrite(const uint8_t *data, size_t len)
{
    DF_CHECK_NULL_PARAM(data);
    if (len == 0U) return ESP_ERR_INVALID_ARG;
    if (!g_bIsReady || g_dev_handle == NULL) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit(g_dev_handle, data, len, DF_TOUCH_TIMEOUT_MS);
}

static esp_err_t app_touch_I2cWriteRead(const uint8_t *write_buf, size_t write_len,
                                        uint8_t *read_buf, size_t read_len)
{
    DF_CHECK_NULL_PARAM(write_buf);
    DF_CHECK_NULL_PARAM(read_buf);
    if (write_len == 0U || read_len == 0U) return ESP_ERR_INVALID_ARG;
    if (!g_bIsReady || g_dev_handle == NULL) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(g_dev_handle,
                                       write_buf, write_len,
                                       read_buf, read_len,
                                       DF_TOUCH_TIMEOUT_MS);
}

/* ==================== Public Low-level ==================== */

esp_err_t app_touch_WriteReg(uint8_t reg, uint8_t value)
{
    uint8_t buf[DF_TOUCH_REGISTER_WRITE_SIZE] = {reg, value};
    esp_err_t ret = ESP_FAIL;

    for (uint32_t retry_index = 0U; retry_index < DF_TOUCH_RETRY_COUNT; retry_index++) {
        ret = app_touch_I2cWrite(buf, DF_TOUCH_REGISTER_WRITE_SIZE);
        if (ret == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(DF_TOUCH_RETRY_DELAY_MS));
    }
    return ret;
}

esp_err_t app_touch_ReadReg(uint8_t reg, uint8_t *value)
{
    DF_CHECK_NULL_PARAM(value);
    return app_touch_ReadRegs(reg, value, DF_TOUCH_REGISTER_SIZE);
}

esp_err_t app_touch_ReadRegs(uint8_t reg, uint8_t *buf, size_t len)
{
    DF_CHECK_NULL_PARAM(buf);
    if (len == 0U) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = ESP_FAIL;

    for (uint32_t retry_index = 0U; retry_index < DF_TOUCH_RETRY_COUNT; retry_index++) {
        ret = app_touch_I2cWriteRead(&reg, DF_TOUCH_REGISTER_SIZE, buf, len);
        if (ret == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(DF_TOUCH_RETRY_DELAY_MS));
    }
    return ret;
}

/* ==================== Init & Check ==================== */

esp_err_t app_touch_Init(void)
{
    if (g_bIsReady) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port             = DF_TOUCH_I2C_PORT,
        .sda_io_num           = DF_TOUCH_PIN_SDA,
        .scl_io_num           = DF_TOUCH_PIN_SCL,
        .clk_source           = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt    = DF_TOUCH_GLITCH_IGNORE_COUNT,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &g_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(ret));
        return ret;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = DF_TOUCH_I2C_ADDR,
        .scl_speed_hz    = DF_TOUCH_I2C_FREQ_HZ,
    };

    ret = i2c_master_bus_add_device(g_bus_handle, &dev_cfg, &g_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(ret));
        app_touch_Cleanup();
        return ret;
    }

    g_bIsReady = true;

    // Đợi chip boot
    vTaskDelay(pdMS_TO_TICKS(DF_TOUCH_BOOT_DELAY_MS));

    ret = app_touch_CheckDevice();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CY8CMBR3108 not found");
        g_bIsReady = false;
        app_touch_Cleanup();
        return ret;
    }

    ESP_LOGI(TAG, "CY8CMBR3108 init OK");
    return ESP_OK;
}

esp_err_t app_touch_CheckDevice(void)
{
    uint8_t family = 0;
    uint16_t device_id = 0;

    if (app_touch_GetFamilyId(&family) != ESP_OK) return ESP_FAIL;
    if (app_touch_GetDeviceId(&device_id) != ESP_OK) return ESP_FAIL;

    if (family != DF_TOUCH_FAMILY_ID_DEFAULT ||
        device_id != DF_TOUCH_DEVICE_ID_DEFAULT) {
        ESP_LOGW(TAG, "ID mismatch: FAMILY=0x%02X, DEVICE=0x%04X", family, device_id);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG, "Device OK - FAMILY=0x%02X, DEVICE=0x%04X", family, device_id);
    return ESP_OK;
}

esp_err_t app_touch_GetFamilyId(uint8_t *family_id)
{
    DF_CHECK_NULL_PARAM(family_id);
    return app_touch_ReadReg(DF_TOUCH_REG_FAMILY_ID, family_id);
}

esp_err_t app_touch_GetDeviceId(uint16_t *device_id)
{
    DF_CHECK_NULL_PARAM(device_id);
    uint8_t buf[DF_TOUCH_DEVICE_ID_SIZE] = {0};
    esp_err_t ret = app_touch_ReadRegs(DF_TOUCH_REG_DEVICE_ID, buf, DF_TOUCH_DEVICE_ID_SIZE);
    if (ret != ESP_OK) return ret;

    *device_id = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);   // Little Endian
    return ESP_OK;
}

/* ==================== Button Status ==================== */

esp_err_t app_touch_ReadButtonStatus(uint8_t *status)
{
    DF_CHECK_NULL_PARAM(status);
    return app_touch_ReadReg(DF_TOUCH_REG_BUTTON_STAT, status);
}

esp_err_t app_touch_ReadLatchedButtonStatus(uint8_t *status)
{
    DF_CHECK_NULL_PARAM(status);
    return app_touch_ReadReg(DF_TOUCH_REG_LATCHED_BUTTON_STAT, status);
}

/* ==================== Command ==================== */

esp_err_t app_touch_SendCmd(uint8_t cmd)
{
    uint8_t current = DF_TOUCH_CMD_SW_RESET;

    // Đợi lệnh trước đó xong
    for (uint32_t retry_index = 0U; retry_index < DF_TOUCH_COMMAND_IDLE_RETRY_COUNT; retry_index++) {
        if (app_touch_ReadReg(DF_TOUCH_REG_CTRL_CMD, &current) == ESP_OK &&
            current == DF_TOUCH_CMD_NONE) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(DF_TOUCH_COMMAND_RETRY_DELAY_MS));
    }

    esp_err_t ret = app_touch_WriteReg(DF_TOUCH_REG_CTRL_CMD, cmd);
    if (ret != ESP_OK) return ret;

    // Đợi lệnh hiện tại xong
    for (uint32_t retry_index = 0U; retry_index < DF_TOUCH_COMMAND_RETRY_COUNT; retry_index++) {
        if (app_touch_ReadReg(DF_TOUCH_REG_CTRL_CMD, &current) == ESP_OK &&
            current == DF_TOUCH_CMD_NONE) {

            uint8_t err = 0;
            app_touch_ReadReg(DF_TOUCH_REG_CTRL_CMD_ERR, &err);
            if (err != DF_TOUCH_CMD_ERR_SUCCESS) {
                ESP_LOGW(TAG, "CMD 0x%02X error: 0x%02X", cmd, err);
                return ESP_FAIL;
            }
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(DF_TOUCH_COMMAND_RETRY_DELAY_MS));
    }

    ESP_LOGW(TAG, "CMD 0x%02X timeout", cmd);
    return ESP_ERR_TIMEOUT;
}

esp_err_t app_touch_ClearLatched(void)
{
    return app_touch_SendCmd(DF_TOUCH_CMD_CLEAR_LATCHED);
}

esp_err_t app_touch_SoftReset(void)
{
    esp_err_t ret = app_touch_WriteReg(DF_TOUCH_REG_CTRL_CMD, DF_TOUCH_CMD_SW_RESET);
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(DF_TOUCH_RESET_DELAY_MS));     // Đợi chip boot lại
    return ESP_OK;
}