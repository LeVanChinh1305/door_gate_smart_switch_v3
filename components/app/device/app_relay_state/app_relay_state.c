#include "app_relay_state.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "APP_RELAY_STATE";

#define DF_MAX_RELAY_STATE_CALLBACKS (4U)

static e_relay_state_t g_eCurrentRelayState = E_RELAY_STATE_STOPPED;
static SemaphoreHandle_t g_hStateMutex = NULL;
static bool g_bIsReady = false;

static relay_state_changed_cb_t g_fnCallbacks[DF_MAX_RELAY_STATE_CALLBACKS] = {0};
static uint8_t g_u8CallbackCount = 0;

static void app_relay_state_EnsureMutex(void)
{
    if (g_hStateMutex == NULL) {
        g_hStateMutex = xSemaphoreCreateMutex();
    }
}

esp_err_t app_relay_state_Init(void)
{
    if (g_bIsReady) {
        return ESP_OK;
    }

    app_relay_state_EnsureMutex();
    if (g_hStateMutex == NULL) {
        ESP_LOGE(TAG, "Khởi tạo Mutex quản lý relay state thất bại");
        return ESP_ERR_NO_MEM;
    }

    g_eCurrentRelayState = E_RELAY_STATE_CLOSED;
    g_u8CallbackCount = 0;
    g_bIsReady = true;

    ESP_LOGI(TAG, "Khởi tạo module app_relay_state thành công. Trạng thái ban đầu: CLOSED");
    return ESP_OK;
}

const char *app_relay_state_ToString(e_relay_state_t eState)
{
    switch (eState) {
        case E_RELAY_STATE_STOPPED:       return "STOPPED";
        case E_RELAY_STATE_OPENING:       return "OPENING";
        case E_RELAY_STATE_CLOSING:       return "CLOSING";
        case E_RELAY_STATE_OPENED:        return "OPENED";
        case E_RELAY_STATE_CLOSED:        return "CLOSED";
        case E_RELAY_STATE_OBSTACLE:      return "OBSTACLE";
        case E_RELAY_STATE_SAFETY_LOCKED: return "SAFETY_LOCKED";
        default:                          return "UNKNOWN";
    }
}

esp_err_t app_relay_state_SetState(e_relay_state_t eNewState)
{
    if (eNewState >= E_RELAY_STATE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    app_relay_state_EnsureMutex();
    if (g_hStateMutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    e_relay_state_t eOldState = E_RELAY_STATE_STOPPED;
    bool bStateChanged = false;

    if (xSemaphoreTake(g_hStateMutex, pdMS_TO_TICKS(100)) == pdPASS) {
        if (g_eCurrentRelayState != eNewState) {
            eOldState = g_eCurrentRelayState;
            g_eCurrentRelayState = eNewState;
            bStateChanged = true;
            ESP_LOGI(TAG, "Trạng thái Relay chuyển đổi: %s -> %s",
                     app_relay_state_ToString(eOldState),
                     app_relay_state_ToString(eNewState));
        }
        xSemaphoreGive(g_hStateMutex);
    } else {
        ESP_LOGE(TAG, "Không thể lấy Mutex để cập nhật Relay State (timeout)");
        return ESP_ERR_TIMEOUT;
    }

    // Nếu trạng thái thay đổi, kích hoạt các callback đã đăng ký
    if (bStateChanged) {
        for (uint8_t i = 0; i < g_u8CallbackCount; i++) {
            if (g_fnCallbacks[i] != NULL) {
                g_fnCallbacks[i](eNewState, eOldState);
            }
        }
    }

    return ESP_OK;
}

e_relay_state_t app_relay_state_GetState(void)
{
    e_relay_state_t eState = E_RELAY_STATE_STOPPED;

    app_relay_state_EnsureMutex();
    if (g_hStateMutex == NULL) {
        return g_eCurrentRelayState;
    }

    if (xSemaphoreTake(g_hStateMutex, pdMS_TO_TICKS(100)) == pdPASS) {
        eState = g_eCurrentRelayState;
        xSemaphoreGive(g_hStateMutex);
    }
    return eState;
}

esp_err_t app_relay_state_RegisterCallback(relay_state_changed_cb_t fnCallback)
{
    if (fnCallback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    app_relay_state_EnsureMutex();
    if (g_hStateMutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_OK;
    if (xSemaphoreTake(g_hStateMutex, pdMS_TO_TICKS(100)) == pdPASS) {
        // Kiểm tra xem đã đăng ký chưa
        for (uint8_t i = 0; i < g_u8CallbackCount; i++) {
            if (g_fnCallbacks[i] == fnCallback) {
                xSemaphoreGive(g_hStateMutex);
                return ESP_OK;
            }
        }

        if (g_u8CallbackCount < DF_MAX_RELAY_STATE_CALLBACKS) {
            g_fnCallbacks[g_u8CallbackCount++] = fnCallback;
            ESP_LOGI(TAG, "Đã đăng ký callback thay đổi trạng thái relay (index=%d)", g_u8CallbackCount - 1);
        } else {
            ESP_LOGW(TAG, "Danh sách callback đầy (%d)", DF_MAX_RELAY_STATE_CALLBACKS);
            ret = ESP_ERR_NO_MEM;
        }
        xSemaphoreGive(g_hStateMutex);
    } else {
        ret = ESP_ERR_TIMEOUT;
    }

    return ret;
}
