#ifndef APP_LOGIC_LED_H
#define APP_LOGIC_LED_H

#include "app_led.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    app_led_color_t sColor;
    uint8_t u8Brightness;
} app_logic_led_command_t;

esp_err_t app_logic_led_Init(void);
esp_err_t app_logic_led_SetColor(app_led_color_t sColor);
esp_err_t app_logic_led_SetBrightness(uint8_t u8Brightness);
esp_err_t app_logic_led_Show(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_LOGIC_LED_H */