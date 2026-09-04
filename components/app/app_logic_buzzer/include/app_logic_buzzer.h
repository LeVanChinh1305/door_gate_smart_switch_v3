#ifndef APP_LOGIC_BUZZER_H
#define APP_LOGIC_BUZZER_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_logic_buzzer_Init(void);
esp_err_t app_logic_buzzer_On(void);
esp_err_t app_logic_buzzer_Off(void);
esp_err_t app_logic_buzzer_Beep(uint32_t u32DurationMs);

#ifdef __cplusplus
}
#endif

#endif /* APP_LOGIC_BUZZER_H */