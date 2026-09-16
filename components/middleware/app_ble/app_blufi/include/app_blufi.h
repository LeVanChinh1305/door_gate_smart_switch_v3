#pragma once

#include "esp_err.h"
#include <stdbool.h>


#ifdef __cplusplus
extern "C" {
#endif

#define DF_BLUFI_STA_SSID_SIZE 32U
#define DF_BLUFI_STA_PASSWORD_SIZE 64U
#define DF_BLUFI_DEVICE_ADDRESS_SIZE 18U
#define DF_BLUFI_DEVICE_TYPE_DEFAULT 3097
#define DF_BLUFI_CUSTOM_DATA_BUFFER_SIZE 1024U

esp_err_t app_blufi_Init(void);
esp_err_t app_blufi_Deinit(void);
bool app_blufi_IsConnected(void);
void app_blufi_ReportWifiStatus(bool is_connected);

#ifdef __cplusplus
}
#endif