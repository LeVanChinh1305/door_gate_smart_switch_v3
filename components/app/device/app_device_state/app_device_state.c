#include "app_device_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "APP_DEVICE_STATE";

static uint32_t g_u32CurrentDoorModeMask = (uint32_t)DEVICE_MODE_UNCONNECTED;     
static control_mode_t g_eCurrentControlMode = CONTROL_MODE_ROLLING_NORMAL; 
static sensor_type_t g_eCurrentSensorType = SENSOR_TYPE_NONE;         
static sensor_wire_type_t g_eCurrentSensorWireType = SENSOR_WIRE_TYPE_NONE; 

static SemaphoreHandle_t g_hStateMutex = NULL;

static void device_EnsureMutex(void)
{
    if (g_hStateMutex == NULL) {
        g_hStateMutex = xSemaphoreCreateMutex();
    }
}

uint32_t get_current_door_mode_mask(void)
{
    uint32_t u32Mask = 0U;
    device_EnsureMutex();
    if (xSemaphoreTake(g_hStateMutex, pdMS_TO_TICKS(100)) == pdPASS) {
        u32Mask = g_u32CurrentDoorModeMask;
        xSemaphoreGive(g_hStateMutex);
    }
    return u32Mask;
}

bool app_device_state_HasMode(device_mode_t eMode)
{
    bool bHas = false;
    device_EnsureMutex();
    if (xSemaphoreTake(g_hStateMutex, pdMS_TO_TICKS(100)) == pdPASS) {
        bHas = ((g_u32CurrentDoorModeMask & (uint32_t)eMode) != 0U);
        xSemaphoreGive(g_hStateMutex);
    }
    return bHas;
}

void app_device_state_SetModeBit(device_mode_t eMode, bool bEnable)
{
    device_EnsureMutex();
    if (xSemaphoreTake(g_hStateMutex, pdMS_TO_TICKS(100)) == pdPASS) {
        uint32_t u32OldMask = g_u32CurrentDoorModeMask;
        
        if (bEnable) {
            g_u32CurrentDoorModeMask |= (uint32_t)eMode;
        } else {
            g_u32CurrentDoorModeMask &= ~((uint32_t)eMode);
        }

        if (u32OldMask != g_u32CurrentDoorModeMask) {
            ESP_LOGI(TAG, "Cập nhật Bitmask Mode: 0x%03X -> 0x%03X", 
                     (unsigned int)u32OldMask, (unsigned int)g_u32CurrentDoorModeMask);
        }
        xSemaphoreGive(g_hStateMutex);
    }
}



control_mode_t get_current_control_mode(void) 
{
    control_mode_t eMode = CONTROL_MODE_ROLLING_NORMAL; 
    device_EnsureMutex();
    if (xSemaphoreTake(g_hStateMutex, pdMS_TO_TICKS(100)) == pdPASS) {
        eMode = g_eCurrentControlMode;
        xSemaphoreGive(g_hStateMutex);
    }
    return eMode;
}

void set_current_control_mode(control_mode_t mode) 
{
    device_EnsureMutex();
    if (xSemaphoreTake(g_hStateMutex, pdMS_TO_TICKS(100)) == pdPASS) {
        g_eCurrentControlMode = mode;
        xSemaphoreGive(g_hStateMutex);
    }
}

sensor_type_t get_current_sensor_type(void) 
{
    sensor_type_t eType = SENSOR_TYPE_NONE; 
    device_EnsureMutex();
    if (xSemaphoreTake(g_hStateMutex, pdMS_TO_TICKS(100)) == pdPASS) {
        eType = g_eCurrentSensorType;
        xSemaphoreGive(g_hStateMutex);
    }
    return eType;
}

void set_current_sensor_type(sensor_type_t type) 
{
    device_EnsureMutex();
    if (xSemaphoreTake(g_hStateMutex, pdMS_TO_TICKS(100)) == pdPASS) {
        g_eCurrentSensorType = type;
        xSemaphoreGive(g_hStateMutex);
    }
}

sensor_wire_type_t get_current_sensor_wire_type(void) 
{
    sensor_wire_type_t eWireType = SENSOR_WIRE_TYPE_NONE; 
    device_EnsureMutex();
    if (xSemaphoreTake(g_hStateMutex, pdMS_TO_TICKS(100)) == pdPASS) {
        eWireType = g_eCurrentSensorWireType;
        xSemaphoreGive(g_hStateMutex);
    }
    return eWireType;
}

void set_current_sensor_wire_type(sensor_wire_type_t wire_type) 
{
    device_EnsureMutex();
    if (xSemaphoreTake(g_hStateMutex, pdMS_TO_TICKS(100)) == pdPASS) {
        g_eCurrentSensorWireType = wire_type;
        xSemaphoreGive(g_hStateMutex);
    }
}