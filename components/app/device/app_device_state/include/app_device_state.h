#pragma once

typedef enum{
    DEVICE_MODE_UNCONNECTED,
    DEVICE_MODE_CONNECT_AUTO,
    DEVICE_MODE_CONNECT_MANUAL,
    DEVICE_MODE_NORMAL
} device_mode_t; 
device_mode_t get_current_door_mode(void);
void set_current_door_mode(device_mode_t mode);





typedef enum{
    CONTROL_MODE_ROLLING_VENT,
    CONTROL_MODE_ROLLING_NORMAL,
    CONTROL_MODE_GATE_1LEAF,
    CONTROL_MODE_GATE_2LEAF,
    CONTROL_MODE_GATE_SLIDING, 
    CONTROL_MODE_GATE_CURTAIN  
} control_mode_t;
control_mode_t get_current_control_mode(void);
void set_current_control_mode(control_mode_t mode);



typedef enum{
    SENSOR_TYPE_NONE, 
    SENSOR_TYPE_WIRE, 
    SENSOR_TYPE_BLE
} sensor_type_t; 
sensor_type_t get_current_sensor_type(void);
void set_current_sensor_type(sensor_type_t type); 



typedef enum{
    SENSOR_WIRE_TYPE_NONE, 
    SENSOR_WIRE_TYPE_DOOR_STATE,
    SENSOR_WIRE_TYPE_ANTI_STUCK
} sensor_wire_type_t; 
sensor_wire_type_t get_current_sensor_wire_type(void);
void set_current_sensor_wire_type(sensor_wire_type_t wire_type);

