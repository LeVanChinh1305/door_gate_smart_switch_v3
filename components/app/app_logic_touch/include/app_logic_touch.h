#ifndef APP_LOGIC_TOUCH_H
#define APP_LOGIC_TOUCH_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    E_APP_LOGIC_TOUCH_CMD_READ_STATUS = 0,
    E_APP_LOGIC_TOUCH_CMD_CLEAR_LATCHED,
    E_APP_LOGIC_TOUCH_CMD_SOFT_RESET
} e_app_logic_touch_cmd_t;

esp_err_t app_logic_touch_Init(void);
esp_err_t app_logic_touch_SendCommand(e_app_logic_touch_cmd_t eCommand);

#ifdef __cplusplus
}
#endif

#endif /* APP_LOGIC_TOUCH_H */