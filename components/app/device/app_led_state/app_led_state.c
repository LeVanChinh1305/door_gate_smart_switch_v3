/**
 * @file    app_led_state.c
 * @brief   Triển khai bộ điều phối hiệu ứng LED dựa trên trạng thái thiết bị
 */

#include "app_led_state.h"
#include "app_common.h"
#include "app_logic_led.h"
#include "app_nvs.h"
#include "app_relay_state.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

static const char *TAG = "APP_LED_STATE";

/* Helper: giải mã giá trị RGB packed 0xRRGGBB thành struct app_led_color_t */
static inline app_led_color_t s_unpack_rgb(uint32_t u32Val) {
  app_led_color_t c;
  c.r = (uint8_t)((u32Val >> 16) & 0xFF);
  c.g = (uint8_t)((u32Val >> 8) & 0xFF);
  c.b = (uint8_t)(u32Val & 0xFF);
  return c;
}

/* Helper: áp dụng brightness + màu ON/OFF tùy relay state, không phụ thuộc
 * app_logic_extra_config */
static void s_apply_led_from_extra_config(void) {
  if (g_sExtraConfig.ledEnb == 0) {
    (void)app_logic_led_SetBrightness(0);
    (void)app_logic_led_SetColor(APP_LED_COLOR_OFF);
    return;
  }
  uint8_t u8Bright = (uint8_t)((g_sExtraConfig.led_lightness * 255) / 100);
  (void)app_logic_led_SetBrightness(u8Bright);

  e_relay_state_t eState = app_relay_state_GetState();
  uint32_t u32Color =
      (eState == E_RELAY_STATE_OPENING || eState == E_RELAY_STATE_OPENED)
          ? g_sExtraConfig.ledRgbOn
          : g_sExtraConfig.ledRgbOff;
  (void)app_logic_led_SetColor(s_unpack_rgb(u32Color));
}

static e_led_device_state_t g_eCurrentState = E_LED_STATE_MAX;
static TaskHandle_t g_hLedStateTask = NULL;
static bool g_bTaskRunning = false;

/**
 * @brief   Task nền chuyên trách thực thi các hiệu ứng chớp nháy hoặc đổi màu
 * theo State.
 */
static void app_led_state_Task(void *pArg) {
  (void)pArg;
  bool bToggle = false;

  esp_task_wdt_add(NULL);

  while (g_bTaskRunning) {
    esp_task_wdt_reset();

    switch (g_eCurrentState) {
    case E_LED_STATE_UNCONNECTED:
      if (bToggle) {
        (void)app_logic_led_SetColor(APP_LED_COLOR_WHITE);
      } else {
        (void)app_logic_led_SetColor(APP_LED_COLOR_OFF);
      }
      vTaskDelay(pdMS_TO_TICKS(1000U));
      break;
    case E_LED_STATE_NORMAL_IDLE:
      // chế độ chờ lệnh kết nối
      if (bToggle) {
        (void)app_logic_led_SetColor(APP_LED_COLOR_WHITE);
      } else {
        (void)app_logic_led_SetColor(APP_LED_COLOR_OFF);
      }
      vTaskDelay(pdMS_TO_TICKS(1000U));
      break;
    case E_LED_STATE_BLUFI_AUTO:
      /* Nhấp nháy xanh dương (BluFi - Giữ 3s) */
      if (bToggle) {
        (void)app_logic_led_SetColor(APP_LED_COLOR_BLUE);
      } else {
        (void)app_logic_led_SetColor(APP_LED_COLOR_OFF);
      }
      vTaskDelay(pdMS_TO_TICKS(500U));
      break;

    case E_LED_STATE_CONNECT_MANUAL:
      /* Nhấp nháy đỏ (Thủ công - Giữ 7s) */
      if (bToggle) {
        (void)app_logic_led_SetColor(APP_LED_COLOR_CYAN);
      } else {
        (void)app_logic_led_SetColor(APP_LED_COLOR_OFF);
      }
      vTaskDelay(pdMS_TO_TICKS(500U));
      break;

    case E_LED_STATE_LOCKED:
      /* Trạng thái khóa: Sáng mờ hoặc màu vàng tĩnh */
      (void)app_logic_led_SetColor(APP_LED_COLOR_YELLOW);
      vTaskDelay(pdMS_TO_TICKS(1000U));
      break;

    case E_LED_STATE_WARNING:
      // Cảnh báo: Nhấp nháy đỏ liên tục tốc độ cao 
      if (bToggle) {
        (void)app_logic_led_SetColor(APP_LED_COLOR_RED);
      } else {
        (void)app_logic_led_SetColor(APP_LED_COLOR_OFF);
      }
      vTaskDelay(pdMS_TO_TICKS(200U));
      break;
    case E_LED_STATE_GATE_UP: {
      // Relay OPEN đang bật: LED OPEN (pixel 2) = màu bật, 2 LED còn lại = màu tắt 
      app_led_color_t sColorOn = s_unpack_rgb(g_sExtraConfig.ledRgbOn);
      app_led_color_t sColorOff = s_unpack_rgb(g_sExtraConfig.ledRgbOff);
      (void)app_logic_led_SetPixelColor(0, sColorOff); // CLOSE relay: tắt
      (void)app_logic_led_SetPixelColor(1, sColorOff); // STOP  relay: tắt
      (void)app_logic_led_SetPixelColor(2, sColorOn);  // OPEN  relay: bật
      (void)app_logic_led_Show();
      vTaskDelay(pdMS_TO_TICKS(500U));
      break;
    }

    case E_LED_STATE_GATE_DOWN: {
      // Relay CLOSE đang bật: LED CLOSE (pixel 0) = màu bật, 2 LED còn lại = màu tắt 
      app_led_color_t sColorOn = s_unpack_rgb(g_sExtraConfig.ledRgbOn);
      app_led_color_t sColorOff = s_unpack_rgb(g_sExtraConfig.ledRgbOff);
      (void)app_logic_led_SetPixelColor(0, sColorOn);  // CLOSE relay: bật
      (void)app_logic_led_SetPixelColor(1, sColorOff); // STOP  relay: tắt
      (void)app_logic_led_SetPixelColor(2, sColorOff); // OPEN  relay: tắt
      (void)app_logic_led_Show();
      vTaskDelay(pdMS_TO_TICKS(500U));
      break;
    }

    case E_LED_STATE_GATE_STOP: {
      // Tất cả relay đều tắt sau khi dừng: cả 3 LED hiện màu tắt 
      app_led_color_t sColorOff = s_unpack_rgb(g_sExtraConfig.ledRgbOff);
      app_led_color_t sColorOn = s_unpack_rgb(g_sExtraConfig.ledRgbOn);
      (void)app_logic_led_SetPixelColor(0, sColorOff); // CLOSE relay: tắt
      (void)app_logic_led_SetPixelColor(1, sColorOn);  // STOP  relay: bật
      (void)app_logic_led_SetPixelColor(2, sColorOff); // OPEN  relay: tắt
      (void)app_logic_led_Show();
      vTaskDelay(pdMS_TO_TICKS(500U));
      break;
    }
    case E_LED_STATE_NORMAL:
      // Chế độ bình thường: Áp dụng toàn bộ cấu hình LED (brightness + màu ON/OFF theo relay state) 
      s_apply_led_from_extra_config();
      vTaskDelay(pdMS_TO_TICKS(2000U));
      break;
    default:
      // Trạng thái bình thường: sáng nhẹ hoặc tắt tùy thiết kế 
      (void)app_logic_led_SetColor(APP_LED_COLOR_OFF);
      vTaskDelay(pdMS_TO_TICKS(2000U));
      break;
    }
    bToggle = !bToggle;
  }
  esp_task_wdt_delete(NULL);
  vTaskDelete(NULL);
}

esp_err_t app_led_state_Init(void) {
  if (g_bTaskRunning) {
    return ESP_OK;
  }

  g_eCurrentState = E_LED_STATE_NORMAL_IDLE;
  g_bTaskRunning = true;

  BaseType_t xRet =
      xTaskCreate(app_led_state_Task, "led_state_task", DF_TASK_STACK_MIN, NULL,
                  DF_TASK_PRIO_LOW, &g_hLedStateTask);
  if (xRet != pdPASS) {
    g_bTaskRunning = false;
    ESP_LOGE(TAG, "Tạo task quản lý trạng thái LED thất bại");
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "Khởi tạo module led_device_state thành công");
  return ESP_OK;
}

esp_err_t app_led_state_SetState(e_led_device_state_t eState) {
  if (eState >= E_LED_STATE_MAX) {
    return ESP_ERR_INVALID_ARG;
  }

  if (g_eCurrentState != eState) {
    g_eCurrentState = eState;
    ESP_LOGI(TAG, "Chuyển trạng thái LED sang mode: %d", (int)eState);
  }
  return ESP_OK;
}

e_led_device_state_t app_led_state_GetState(void) { 
  return g_eCurrentState; 
}