#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(10000));

    ESP_LOGI(TAG, "Bắt đầu khởi động vùng nhớ");
}