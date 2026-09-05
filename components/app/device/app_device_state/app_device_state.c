/**
 * @file    device.c
 * @brief   Triển khai quản lý trạng thái thiết bị (device_mode_t, control_mode_t, v.v.)
 */

#include "app_device_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "DEVICE_STATE";

// Khởi tạo các trạng thái mặc định ban đầu theo yêu cầu hệ thống
static device_mode_t g_eCurrentDoorMode = DEVICE_MODE_UNCONNECTED;     
static control_mode_t g_eCurrentControlMode = CONTROL_MODE_ROLLING_NORMAL; 
static sensor_type_t g_eCurrentSensorType = SENSOR_TYPE_NONE;         
static sensor_wire_type_t g_eCurrentSensorWireType = SENSOR_WIRE_TYPE_NONE; 

// Mutex bảo vệ biến trạng thái tránh xung đột dữ liệu giữa các Task
static SemaphoreHandle_t g_hStateMutex = NULL;

static void device_EnsureMutex(void)
{
    if (g_hStateMutex == NULL) {
        g_hStateMutex = xSemaphoreCreateMutex();
    }
}

// --- Quản lý Device Mode ---
device_mode_t get_current_door_mode(void)
{
    device_mode_t eMode = DEVICE_MODE_UNCONNECTED; 
    device_EnsureMutex();
    if (xSemaphoreTake(g_hStateMutex, pdMS_TO_TICKS(100)) == pdPASS) {
        eMode = g_eCurrentDoorMode;
        xSemaphoreGive(g_hStateMutex);
    }
    return eMode;
}

void set_current_door_mode(device_mode_t mode) 
{
    device_EnsureMutex();
    if (xSemaphoreTake(g_hStateMutex, pdMS_TO_TICKS(100)) == pdPASS) {
        if (g_eCurrentDoorMode != mode) {
            ESP_LOGI(TAG, "Chuyển đổi Door Mode: %d -> %d", (int)g_eCurrentDoorMode, (int)mode);
            g_eCurrentDoorMode = mode;
        }
        xSemaphoreGive(g_hStateMutex);
    }
}

// --- Quản lý Control Mode ---
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

// --- Quản lý Sensor Type ---
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

// --- Quản lý Sensor Wire Type ---
sensor_wire_type_t get_current_sensor_wire_type(void) 
{
    sensor_wire_type_t eWireType = SENSOR_WIRE_TYPE_NONE; 
    device_EnsureMutex();
    if (xSemaphoreTake(g_hStateMutex, pdMS_TO_TICKS(100)) == pdPASS) {
        eWireType = g_eCurrentSensorWireType;
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