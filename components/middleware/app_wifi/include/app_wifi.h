#pragma once 

#include "esp_err.h"
#include "stdbool.h"

#define WIFI_CONNECTED_BIT          BIT0
#define WIFI_FAIL_BIT               BIT1
#define MAXIMUM_RETRY_CONNECT_WIFI  100


#ifdef __cplusplus
extern "C"{
#endif

esp_err_t app_wifi_init_sta(void);
bool app_wifi_is_connected(void); 
bool app_wifi_wait_for_connect(uint32_t timeout_ms);


#ifdef __cplusplus
}
#endif 


