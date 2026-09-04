#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_blufi_Init(void);
esp_err_t app_blufi_Deinit(void);
bool app_blufi_IsConnected(void);
void app_blufi_ReportWifiStatus(bool is_connected);

#ifdef __cplusplus
}
#endif