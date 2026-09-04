#ifndef APP_HEADER2H_H
#define APP_HEADER2H_H

#include "driver/gpio.h"
#include "esp_err.h"

#define DF_HEADER2H_GPIO_PIN       GPIO_NUM_3
#define DF_HEADER2H_ISR_FLAGS      (0U)

typedef void (*app_header2h_cb_t)(int level, void *arg);

esp_err_t app_header2h_Init(app_header2h_cb_t cb, void *arg);
esp_err_t app_header2h_Read(int *level);

#endif /* APP_HEADER2H_H */