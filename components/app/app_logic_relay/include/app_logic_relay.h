#ifndef APP_LOGIC_RELAY_H
#define APP_LOGIC_RELAY_H

#include "app_relay.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_logic_relay_Init(void);
esp_err_t app_logic_relay_SendCommand(e_app_relay_cmd_t eCommand);

#ifdef __cplusplus
}
#endif

#endif /* APP_LOGIC_RELAY_H */