# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
	${CMAKE_CURRENT_LIST_DIR}/components/drivers/app_buzzer
	${CMAKE_CURRENT_LIST_DIR}/components/drivers/app_header2h
	${CMAKE_CURRENT_LIST_DIR}/components/drivers/app_led
	${CMAKE_CURRENT_LIST_DIR}/components/drivers/app_relay
	${CMAKE_CURRENT_LIST_DIR}/components/drivers/app_touch
	${CMAKE_CURRENT_LIST_DIR}/components/drivers/app_zero_cross
	${CMAKE_CURRENT_LIST_DIR}/components/app/logic/app_logic_relay
	${CMAKE_CURRENT_LIST_DIR}/components/app/logic/app_logic_touch
	${CMAKE_CURRENT_LIST_DIR}/components/app/logic/app_logic_led
	${CMAKE_CURRENT_LIST_DIR}/components/app/logic/app_logic_buzzer
	${CMAKE_CURRENT_LIST_DIR}/components/app/logic/app_logic_mqtt
	${CMAKE_CURRENT_LIST_DIR}/components/app/logic/app_logic_extra_config
	${CMAKE_CURRENT_LIST_DIR}/components/app/logic/app_logic_mqtt_publisher
	${CMAKE_CURRENT_LIST_DIR}/components/middleware/app_nvs
	${CMAKE_CURRENT_LIST_DIR}/components/middleware/app_wifi
	${CMAKE_CURRENT_LIST_DIR}/components/middleware/app_blufi
	${CMAKE_CURRENT_LIST_DIR}/components/middleware/app_mqtt
	${CMAKE_CURRENT_LIST_DIR}/components/app/mqtt_vconnex_decrypt 
	${CMAKE_CURRENT_LIST_DIR}/components/common
	${CMAKE_CURRENT_LIST_DIR}/components/app/device/app_device_state
	${CMAKE_CURRENT_LIST_DIR}/components/app/device/app_led_state
	${CMAKE_CURRENT_LIST_DIR}/components/app/device/app_relay_state
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(door_gate_smart_switch_v3)
