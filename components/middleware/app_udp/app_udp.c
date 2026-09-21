/**
 * @file app_udp.c
 * @brief Triển khai dịch vụ UDP cấu hình thiết bị thủ công (Manual Provisioning).
 */

#include "app_udp.h"
#include "app_nvs.h"
#include "app_wifi.h"
#include "app_device_state.h"
#include "esp_system.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "app_led_state.h"

/* ====================================================================
 * Hằng số cấu hình nội bộ (Internal Macros DF_...)
 * ==================================================================== */
#define DF_UDP_DEFAULT_ADD_DEVICE_KEY       "12345678"
#define DF_UDP_RESTART_DEVICE_PORT          DF_UDP_SERVER_PORT
#define DF_UDP_DEFAULT_DEV_TYPE             (3097)
#define DF_UDP_SOFTWARE_VERSION             "1.1.1"

#define DF_UDP_AP_SSID                      "VCONNEX-SETUP"
#define DF_UDP_AP_PASSWORD                  "12345678"
#define DF_UDP_AP_CHANNEL                   (1U)
#define DF_UDP_AP_MAX_CONN                  (4U)

#define DF_UDP_RCVTIMEO_SEC                 (3)
#define DF_UDP_RCVTIMEO_USEC                (0)

#define DF_UDP_TASK_STACK_SIZE              (8192U)
#define DF_UDP_TASK_PRIORITY                (5U)
#define DF_UDP_TASK_NAME                    "app_udp_task"

#define DF_UDP_MAC_ADDR_LEN                 (6U)
#define DF_UDP_MAC_STR_LEN                  (18U)
#define DF_UDP_WAIT_STEP_DELAY_MS           (100U)
#define DF_UDP_DISCONNECT_WAIT_MS           (200U)

static const char *TAG = "APP_UDP";

/* ====================================================================
 * Biến toàn cục nội bộ (Static Global Variables g_...)
 * ==================================================================== */
static int32_t           g_i32UdpFd = -1;
static volatile bool     g_bUdpRunning = false;
static volatile bool     g_bAnnounceEnabled = false;
static bool              g_bWifiInitialized = false;
static esp_netif_t      *g_pUdpApNetif = NULL;
static uint16_t          g_u16UdpDevicePort = DF_UDP_SERVER_PORT;
static TaskHandle_t      g_hUdpTaskHandle = NULL;

/* ====================================================================
 * Khai báo các hàm nội bộ (Internal Function Prototypes)
 * ==================================================================== */
static void      app_udp_Task(void *pvArg);
static esp_err_t app_udp_StartServerTask(void);
static esp_err_t app_udp_OpenSocket(uint16_t u16DevicePort);
static esp_err_t app_udp_StartSoftAp(void);
static void      app_udp_AddTimestamp(cJSON *pResponse);
static void      app_udp_AddDeviceIdentity(cJSON *pResponse, int32_t i32DevType);
static void      app_udp_AddStatus(cJSON *pResponse, const char *pcName, int32_t i32Code);
static bool      app_udp_ValidDeviceIdentity(const cJSON *pValue);
static bool      app_udp_CopyField(char *pcDestination, size_t u32Size, const cJSON *pItem);
static void      app_udp_SendDeviceAnnounce(void);
static void      app_udp_SendResponse(const struct sockaddr_in *psClient, socklen_t u32ClientLen, cJSON *pResponse);
static cJSON    *app_udp_HandleCommand(const cJSON *pRequest, bool *pbExitRequested);

/* ====================================================================
 * Triển khai các hàm nội bộ (Internal Functions)
 * ==================================================================== */

/**
 * @brief Lấy Unix Epoch timestamp (tính bằng giây) chuẩn theo hệ sinh thái Vconnex.
 * @param None.
 * @return uint32_t Giá trị timestamp (seconds).
 */
static uint32_t app_udp_GetEpochTimestamp(void)
{
    time_t sNow = 0;
    (void)time(&sNow);
    if (sNow < 1576000000) {
        /* Nếu chưa đồng bộ SNTP, dùng mốc cơ sở cộng thêm uptime tính bằng giây */
        sNow = 1576030822 + (time_t)(esp_timer_get_time() / 1000000LL);
    }
    return (uint32_t)sNow;
}

/**
 * @brief Thêm trường timeStamp vào đối tượng cJSON phản hồi theo chuẩn Unix Epoch time.
 * @param pResponse Con trỏ tới cJSON object cần thêm timestamp.
 * @return None.
 */
static void app_udp_AddTimestamp(cJSON *pResponse)
{
    if (pResponse == NULL) {
        return;
    }
    (void)cJSON_AddNumberToObject(pResponse, "timeStamp", (double)app_udp_GetEpochTimestamp());
}

/**
 * @brief Thêm thông tin định danh thiết bị (devT, devExtAddr) vào JSON.
 * @param pResponse Con trỏ tới cJSON object phản hồi.
 * @param i32DevType Loại thiết bị (mặc định 3097).
 * @return None.
 */
static void app_udp_AddDeviceIdentity(cJSON *pResponse, int32_t i32DevType)
{
    if (pResponse == NULL) {
        return;
    }

    uint8_t au8Mac[DF_UDP_MAC_ADDR_LEN] = {0};
    char acAddress[DF_UDP_MAC_STR_LEN] = {0};

    (void)esp_read_mac(au8Mac, ESP_MAC_BASE);
    (void)snprintf(acAddress, sizeof(acAddress), "%02X%02X%02X%02X%02X%02X",
                   au8Mac[0], au8Mac[1], au8Mac[2], au8Mac[3], au8Mac[4], au8Mac[5]);

    (void)cJSON_AddNumberToObject(pResponse, "devT", i32DevType);
    (void)cJSON_AddStringToObject(pResponse, "devExtAddr", acAddress);
}

/**
 * @brief Thêm tên lệnh và mã lỗi trạng thái vào JSON phản hồi.
 * @param pResponse Con trỏ tới cJSON object.
 * @param pcName Tên lệnh phản hồi.
 * @param i32Code Mã trạng thái lỗi.
 * @return None.
 */
static void app_udp_AddStatus(cJSON *pResponse, const char *pcName, int32_t i32Code)
{
    if ((pResponse == NULL) || (pcName == NULL)) {
        return;
    }
    (void)cJSON_AddStringToObject(pResponse, "name", pcName);
    (void)cJSON_AddNumberToObject(pResponse, "errorCode", i32Code);
}

/**
 * @brief Sao chép chuỗi an toàn từ cJSON object vào bộ nhớ đệm đích.
 * @param pcDestination Bộ đệm đích lưu chuỗi.
 * @param u32Size Kích thước tối đa của bộ đệm đích.
 * @param pItem Con trỏ phần tử cJSON dạng string.
 * @return true nếu sao chép thành công, false nếu không hợp lệ hoặc bị tràn.
 */
static bool app_udp_CopyField(char *pcDestination, size_t u32Size, const cJSON *pItem)
{
    if ((pcDestination == NULL) || (u32Size == 0U) || (!cJSON_IsString(pItem)) || (pItem->valuestring == NULL)) {
        return false;
    }
    int32_t i32Written = snprintf(pcDestination, u32Size, "%s", pItem->valuestring);
    return (i32Written >= 0) && ((size_t)i32Written < u32Size);
}

/**
 * @brief Kiểm tra tính hợp lệ của địa chỉ MAC và kiểu thiết bị nhận từ client.
 * @param pValue Con trỏ cJSON object chứa trường "devExtAddr" và "devT".
 * @return true nếu thông tin khớp với MAC thực của ESP32, false nếu sai lệch.
 */
static bool app_udp_ValidDeviceIdentity(const cJSON *pValue)
{
    if (!cJSON_IsObject(pValue)) {
        return false;
    }

    uint8_t au8Mac[DF_UDP_MAC_ADDR_LEN] = {0};
    char acExpected[DF_UDP_MAC_STR_LEN] = {0};

    const cJSON *pAddress = cJSON_GetObjectItem(pValue, "devExtAddr");
    (void)esp_read_mac(au8Mac, ESP_MAC_BASE);
    (void)snprintf(acExpected, sizeof(acExpected), "%02X%02X%02X%02X%02X%02X",
                   au8Mac[0], au8Mac[1], au8Mac[2], au8Mac[3], au8Mac[4], au8Mac[5]);

    if (!cJSON_IsString(pAddress) || (pAddress->valuestring == NULL) ||
        (strcmp(pAddress->valuestring, acExpected) != 0)) {
        return false;
    }

    const cJSON *pDevType = cJSON_GetObjectItem(pValue, "devT");
    if (cJSON_IsNumber(pDevType)) {
        if ((pDevType->valueint != DF_UDP_DEFAULT_DEV_TYPE) && (pDevType->valueint != 3099)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Gửi gói tin broadcast thông báo sự hiện diện của thiết bị (CmdGetDeviceID).
 * @param None.
 * @return None.
 */
static void app_udp_SendDeviceAnnounce(void)
{
    cJSON *pAnnounce = cJSON_CreateObject();
    if (pAnnounce == NULL) {
        ESP_LOGE(TAG, "Tạo cJSON announce thất bại");
        return;
    }

    int32_t i32DevType = DF_UDP_DEFAULT_DEV_TYPE;
    app_nvs_device_config_t sDeviceCfg = {0};
    if ((app_nvs_LoadDeviceConfig(&sDeviceCfg) == ESP_OK) && (sDeviceCfg.dev_type != 0)) {
        i32DevType = sDeviceCfg.dev_type;
    }

    (void)cJSON_AddStringToObject(pAnnounce, "name", "CmdGetDeviceID");
    app_udp_AddDeviceIdentity(pAnnounce, i32DevType);
    app_udp_AddTimestamp(pAnnounce);

    char *pcText = cJSON_PrintUnformatted(pAnnounce);
    if (pcText != NULL) {
        struct sockaddr_in sBroadcastAddr = {
            .sin_family = AF_INET,
            .sin_port = htons(DF_UDP_BROADCAST_PORT),
            .sin_addr.s_addr = htonl(INADDR_BROADCAST),
        };

        if (app_wifi_IsConnected()) {
            esp_netif_t *pStaNetif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (pStaNetif != NULL) {
                esp_netif_ip_info_t sIpInfo = {0};
                if (esp_netif_get_ip_info(pStaNetif, &sIpInfo) == ESP_OK) {
                    uint32_t u32BroadcastIp = sIpInfo.ip.addr | ~sIpInfo.netmask.addr;
                    sBroadcastAddr.sin_addr.s_addr = u32BroadcastIp;
                }
            }
        }

        int32_t i32Sent = sendto(g_i32UdpFd, pcText, strlen(pcText), 0,
                                 (const struct sockaddr *)&sBroadcastAddr, sizeof(sBroadcastAddr));
        if (i32Sent < 0) {
            ESP_LOGW(TAG, "Gửi announce broadcast thất bại: errno=%d", errno);
        } else {
            ESP_LOGI(TAG, "Broadcast announce lên %s:%u (%ld bytes)",
                     inet_ntoa(sBroadcastAddr.sin_addr),
                     (unsigned int)ntohs(sBroadcastAddr.sin_port),
                     (long)i32Sent);
        }
        cJSON_free(pcText);
    }

    cJSON_Delete(pAnnounce);
}

/**
 * @brief Gửi dữ liệu cJSON phản hồi về cho UDP client.
 * @param psClient Con trỏ cấu trúc địa chỉ client nhận tin.
 * @param u32ClientLen Độ dài cấu trúc địa chỉ client.
 * @param pResponse Con trỏ cJSON object chứa dữ liệu phản hồi.
 * @return None.
 */
static void app_udp_SendResponse(const struct sockaddr_in *psClient, socklen_t u32ClientLen, cJSON *pResponse)
{
    if ((psClient == NULL) || (pResponse == NULL) || (g_i32UdpFd < 0)) {
        return;
    }

    char *pcText = cJSON_PrintUnformatted(pResponse);
    if (pcText != NULL) {
        int32_t i32Sent = sendto(g_i32UdpFd, pcText, strlen(pcText), 0,
                                 (const struct sockaddr *)psClient, u32ClientLen);
        if (i32Sent < 0) {
            ESP_LOGE(TAG, "Gửi UDP response thất bại: errno=%d", errno);
        } else {
            ESP_LOGI(TAG, "Đã gửi response UDP (%ld bytes): %s", (long)i32Sent, pcText);
        }
        cJSON_free(pcText);
    }
}

/**
 * @brief Xử lý phân tích và thực thi gói lệnh UDP từ ứng dụng di động.
 * @param pRequest Con trỏ cJSON object chứa yêu cầu từ client.
 * @param pbExitRequested Cờ báo hiệu yêu cầu thoát chế độ cấu hình UDP.
 * @return cJSON* Con trỏ cJSON object phản hồi đã cấp phát; trả về NULL nếu lỗi.
 */
static cJSON *app_udp_HandleCommand(const cJSON *pRequest, bool *pbExitRequested)
{
    if ((pRequest == NULL) || (pbExitRequested == NULL)) {
        return NULL;
    }

    const cJSON *pName = cJSON_GetObjectItem(pRequest, "name");
    const cJSON *pValue = cJSON_GetObjectItem(pRequest, "value");

    if (!cJSON_IsString(pName) || (pName->valuestring == NULL)) {
        return NULL;
    }

    const char *pcCommandName = pName->valuestring;
    cJSON *pResponse = cJSON_CreateObject();
    if (pResponse == NULL) {
        return NULL;
    }

    /* 1. Lệnh truy vấn ID thiết bị */
    if (strcmp(pcCommandName, "CmdGetDeviceID") == 0) {
        (void)cJSON_AddStringToObject(pResponse, "name", pcCommandName);
        app_udp_AddDeviceIdentity(pResponse, DF_UDP_DEFAULT_DEV_TYPE);
        app_udp_AddTimestamp(pResponse);
        return pResponse;
    }

    /* 1.1 Lệnh đọc dữ liệu thiết bị từ ứng dụng */
    if (strcmp(pcCommandName, "CmdGetData") == 0) {
        int32_t i32DevType = DF_UDP_DEFAULT_DEV_TYPE;
        const cJSON *pDevTypeItem = cJSON_IsObject(pValue) ? cJSON_GetObjectItem(pValue, "devT") : NULL;
        if (cJSON_IsNumber(pDevTypeItem)) {
            i32DevType = (int32_t)pDevTypeItem->valueint;
        }
        app_udp_AddStatus(pResponse, pcCommandName, E_UDP_STATUS_SUCCESS);
        app_udp_AddDeviceIdentity(pResponse, i32DevType);
        app_udp_AddTimestamp(pResponse);
        return pResponse;
    }

    /* 2. Lệnh cài đặt thông tin mạng Wi-Fi */
    if (strcmp(pcCommandName, "CmdSetWifiInfo") == 0) {
        wifi_config_t sWifiCfg = {0};
        const cJSON *pSsid     = cJSON_IsObject(pValue) ? cJSON_GetObjectItem(pValue, "ssid")     : NULL;
        const cJSON *pPassword = cJSON_IsObject(pValue) ? cJSON_GetObjectItem(pValue, "password") : NULL;

        bool bValid = app_udp_CopyField((char *)sWifiCfg.sta.ssid,      sizeof(sWifiCfg.sta.ssid),     pSsid)     &&
                      app_udp_CopyField((char *)sWifiCfg.sta.password,  sizeof(sWifiCfg.sta.password), pPassword);

        int32_t i32DevType = DF_UDP_DEFAULT_DEV_TYPE;
        app_nvs_device_config_t sTmpCfg = {0};
        if ((app_nvs_LoadDeviceConfig(&sTmpCfg) == ESP_OK) && (sTmpCfg.dev_type != 0)) {
            i32DevType = sTmpCfg.dev_type;
        }

        esp_err_t eErr = ESP_ERR_INVALID_ARG;
        if (bValid) {
            sWifiCfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
            eErr = app_nvs_ClearWifiConfig();
            if ((eErr == ESP_OK) || (eErr == ESP_ERR_NOT_FOUND)) {
                eErr = app_nvs_SaveWifiConfig(&sWifiCfg);
                if (eErr == ESP_OK) {
                    eErr = app_nvs_SetProvisionedWifiConfig(true);
                }
            }
        }

        /* Response theo chuẩn Vconnex: name, devT, devExtAddr, errorCode, timeStamp */
        app_udp_AddStatus(pResponse, pcCommandName, (eErr == ESP_OK) ? E_UDP_STATUS_SUCCESS : E_UDP_STATUS_ERROR);
        app_udp_AddDeviceIdentity(pResponse, i32DevType);
        app_udp_AddTimestamp(pResponse);
        return pResponse;
    }

    /* 3. Lệnh lưu cấu hình thiết bị (MQTT, Broker, Key) */
    if (strcmp(pcCommandName, "CmdSetDeviceConfig") == 0) {
        app_nvs_device_config_t sDeviceCfg = {0};
        const cJSON *pItem = cJSON_IsObject(pValue) ? cJSON_GetObjectItem(pValue, "devT") : NULL;
        sDeviceCfg.dev_type = cJSON_IsNumber(pItem) ? (int32_t)pItem->valueint : DF_UDP_DEFAULT_DEV_TYPE;

        bool bValid = app_udp_ValidDeviceIdentity(pValue) &&
                      app_udp_CopyField(sDeviceCfg.dev_ext_addr, sizeof(sDeviceCfg.dev_ext_addr),
                                        cJSON_GetObjectItem(pValue, "devExtAddr"));
        bValid = app_udp_CopyField(sDeviceCfg.broker, sizeof(sDeviceCfg.broker),
                                   cJSON_GetObjectItem(pValue, "broker")) && bValid;

        (void)app_udp_CopyField(sDeviceCfg.username, sizeof(sDeviceCfg.username), cJSON_GetObjectItem(pValue, "username"));
        (void)app_udp_CopyField(sDeviceCfg.password, sizeof(sDeviceCfg.password), cJSON_GetObjectItem(pValue, "password"));
        (void)app_udp_CopyField(sDeviceCfg.mqtt_sub, sizeof(sDeviceCfg.mqtt_sub), cJSON_GetObjectItem(pValue, "mqttsub"));
        (void)app_udp_CopyField(sDeviceCfg.mqtt_pub, sizeof(sDeviceCfg.mqtt_pub), cJSON_GetObjectItem(pValue, "mqttpub"));
        (void)app_udp_CopyField(sDeviceCfg.mqtt_alert, sizeof(sDeviceCfg.mqtt_alert), cJSON_GetObjectItem(pValue, "mqttalert"));
        (void)app_udp_CopyField(sDeviceCfg.force_ota_url, sizeof(sDeviceCfg.force_ota_url), cJSON_GetObjectItem(pValue, "forceOtaUrl"));
        (void)app_udp_CopyField(sDeviceCfg.be_shared_key, sizeof(sDeviceCfg.be_shared_key), cJSON_GetObjectItem(pValue, "beSharedKey"));
        (void)app_udp_CopyField(sDeviceCfg.api_url, sizeof(sDeviceCfg.api_url), cJSON_GetObjectItem(pValue, "apiUrl"));
        (void)app_udp_CopyField(sDeviceCfg.api_secret_key, sizeof(sDeviceCfg.api_secret_key), cJSON_GetObjectItem(pValue, "apiSecretKey"));

        const cJSON *pUserId = cJSON_IsObject(pValue) ? cJSON_GetObjectItem(pValue, "userId") : NULL;
        if (cJSON_IsNumber(pUserId)) {
            (void)snprintf(sDeviceCfg.user_id, sizeof(sDeviceCfg.user_id), "%lld", (long long)pUserId->valueint);
        } else {
            (void)app_udp_CopyField(sDeviceCfg.user_id, sizeof(sDeviceCfg.user_id), pUserId);
        }

        esp_err_t eErr = bValid ? app_nvs_SaveDeviceConfig(&sDeviceCfg) : ESP_ERR_INVALID_ARG;
        app_udp_AddStatus(pResponse, pcCommandName, (eErr == ESP_OK) ? E_UDP_STATUS_SUCCESS : E_UDP_STATUS_ERROR);
        if (eErr == ESP_OK) {
            app_udp_AddDeviceIdentity(pResponse, sDeviceCfg.dev_type);
            *pbExitRequested = true;
        }
        (void)cJSON_AddStringToObject(pResponse, "software_version", DF_UDP_SOFTWARE_VERSION);
        app_udp_AddTimestamp(pResponse);
        return pResponse;
    }

    /* 4. Lệnh đọc cấu hình thiết bị đã lưu */
    if (strcmp(pcCommandName, "CmdGetDeviceConfig") == 0) {
        app_nvs_device_config_t sDeviceCfg = {0};
        esp_err_t eErr = app_udp_ValidDeviceIdentity(pValue) ?
                         app_nvs_LoadDeviceConfig(&sDeviceCfg) : ESP_ERR_INVALID_ARG;

        app_udp_AddStatus(pResponse, pcCommandName, (eErr == ESP_OK) ? E_UDP_STATUS_SUCCESS : E_UDP_STATUS_ERROR);
        if (eErr == ESP_OK) {
            app_udp_AddDeviceIdentity(pResponse, sDeviceCfg.dev_type);
            (void)cJSON_AddStringToObject(pResponse, "broker",           sDeviceCfg.broker);
            (void)cJSON_AddStringToObject(pResponse, "username",         sDeviceCfg.username);
            (void)cJSON_AddStringToObject(pResponse, "password",         sDeviceCfg.password);
            (void)cJSON_AddStringToObject(pResponse, "mqttsub",          sDeviceCfg.mqtt_sub);
            (void)cJSON_AddStringToObject(pResponse, "mqttpub",          sDeviceCfg.mqtt_pub);
            (void)cJSON_AddStringToObject(pResponse, "mqttalert",        sDeviceCfg.mqtt_alert);
            (void)cJSON_AddStringToObject(pResponse, "software_version", DF_UDP_SOFTWARE_VERSION);
            (void)cJSON_AddStringToObject(pResponse, "forceOtaUrl",      sDeviceCfg.force_ota_url);
            (void)cJSON_AddStringToObject(pResponse, "beSharedKey",      sDeviceCfg.be_shared_key);
            (void)cJSON_AddStringToObject(pResponse, "apiSecretKey",     sDeviceCfg.api_secret_key);
            (void)cJSON_AddStringToObject(pResponse, "addDeviceKey",     DF_UDP_DEFAULT_ADD_DEVICE_KEY);
        }
        app_udp_AddTimestamp(pResponse);
        return pResponse;
    }

    /* 5. Lệnh yêu cầu thoát chế độ cấu hình */
    if (strcmp(pcCommandName, "CmdExitConfiguration") == 0) {
        int32_t i32DevType = DF_UDP_DEFAULT_DEV_TYPE;
        const cJSON *pDevTItem = cJSON_IsObject(pValue) ? cJSON_GetObjectItem(pValue, "devT") : NULL;
        if (cJSON_IsNumber(pDevTItem)) {
            i32DevType = (int32_t)pDevTItem->valueint;
        } else {
            app_nvs_device_config_t sDeviceCfg = {0};
            if ((app_nvs_LoadDeviceConfig(&sDeviceCfg) == ESP_OK) && (sDeviceCfg.dev_type != 0)) {
                i32DevType = sDeviceCfg.dev_type;
            }
        }

        int32_t i32Code = app_udp_ValidDeviceIdentity(pValue) ? E_UDP_STATUS_SUCCESS : E_UDP_STATUS_ERROR;
        app_udp_AddStatus(pResponse, pcCommandName, i32Code);
        app_udp_AddDeviceIdentity(pResponse, i32DevType);
        app_udp_AddTimestamp(pResponse);

        if (i32Code == E_UDP_STATUS_SUCCESS) {
            *pbExitRequested = true;
        }
        return pResponse;
    }

    /* Lệnh không hỗ trợ */
    app_udp_AddStatus(pResponse, pcCommandName, E_UDP_STATUS_ERROR);
    return pResponse;
}

/**
 * @brief Task FreeRTOS nhận và xử lý các gói tin UDP từ client.
 * @param pvArg Tham số đầu vào task (không sử dụng).
 * @return None.
 */
static void app_udp_Task(void *pvArg)
{
    (void)pvArg;
    char acBuffer[DF_UDP_RX_BUFFER_SIZE];
    uint64_t u64LastBroadcastMs = 0U;

    while (g_bUdpRunning) {
        if (g_bAnnounceEnabled && app_wifi_IsConnected()) {
            uint64_t u64NowMs = (uint64_t)(esp_timer_get_time() / 1000ULL);
            if ((u64LastBroadcastMs == 0U) ||
                ((u64NowMs - u64LastBroadcastMs) >= DF_UDP_ANNOUNCE_INTERVAL_MS)) {
                app_udp_SendDeviceAnnounce();
                u64LastBroadcastMs = u64NowMs;
            }
        }

        struct sockaddr_in sClient = {0};
        socklen_t u32ClientLen = sizeof(sClient);
        int32_t i32Received = recvfrom(g_i32UdpFd, acBuffer, sizeof(acBuffer) - 1U, 0,
                                       (struct sockaddr *)&sClient, &u32ClientLen);

        if (i32Received < 0) {
            if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
                ESP_LOGE(TAG, "recvfrom thất bại: errno=%d", errno);
            }
            continue;
        }

        if (i32Received == 0) {
            continue;
        }

        acBuffer[i32Received] = '\0';
        char acSenderIp[INET_ADDRSTRLEN] = {0};
        (void)inet_ntop(AF_INET, &sClient.sin_addr, acSenderIp, sizeof(acSenderIp));
        ESP_LOGI(TAG, "Nhận UDP packet từ %s:%u (%ld bytes): %s",
                 acSenderIp, (unsigned int)ntohs(sClient.sin_port), (long)i32Received, acBuffer);

        cJSON *pRequest = cJSON_Parse(acBuffer);
        if (pRequest == NULL) {
            ESP_LOGE(TAG, "JSON không hợp lệ hoặc vượt quá giới hạn %u bytes", DF_UDP_RX_BUFFER_SIZE);
            cJSON *pErrorResponse = cJSON_CreateObject();
            if (pErrorResponse != NULL) {
                app_udp_AddStatus(pErrorResponse, "InvalidRequest", E_UDP_STATUS_ERROR);
                app_udp_AddTimestamp(pErrorResponse);
                app_udp_SendResponse(&sClient, u32ClientLen, pErrorResponse);
                cJSON_Delete(pErrorResponse);
            }
            continue;
        }

        bool bExitRequested = false;
        cJSON *pResponse = app_udp_HandleCommand(pRequest, &bExitRequested);
        if (pResponse != NULL) {
            app_udp_SendResponse(&sClient, u32ClientLen, pResponse);
        }

        const cJSON *pCommandName = cJSON_GetObjectItem(pRequest, "name");
        const cJSON *pResponseError = (pResponse != NULL) ? cJSON_GetObjectItem(pResponse, "errorCode") : NULL;

        const bool bWifiInfoReceived = (pCommandName != NULL) &&
            cJSON_IsString(pCommandName) && (pCommandName->valuestring != NULL) &&
            (strcmp(pCommandName->valuestring, "CmdSetWifiInfo") == 0) &&
            cJSON_IsNumber(pResponseError) && (pResponseError->valueint == E_UDP_STATUS_SUCCESS);

        cJSON_Delete(pResponse);
        cJSON_Delete(pRequest);

        if (bWifiInfoReceived) {
            /* 1. Ngắt kết nối STA cũ và xóa cờ trạng thái trước khi kết nối mạng mới */
            (void)app_wifi_Disconnect();
            vTaskDelay(pdMS_TO_TICKS(DF_UDP_DISCONNECT_WAIT_MS));

            wifi_config_t sWifiCfg = {0};
            esp_err_t eErr = app_nvs_LoadWifiConfig(&sWifiCfg);
            if (eErr == ESP_OK) {
                eErr = esp_wifi_set_config(WIFI_IF_STA, &sWifiCfg);
            }
            if (eErr == ESP_OK) {
                eErr = esp_wifi_set_mode(WIFI_MODE_STA);
            }
            if (eErr == ESP_OK) {
                app_wifi_SetAutoReconnect(true);
                eErr = esp_wifi_connect();
            }
            if (eErr != ESP_OK) {
                ESP_LOGE(TAG, "Chuyển sang STA sau CmdSetWifiInfo thất bại: %s", esp_err_to_name(eErr));
                g_bUdpRunning = false;
                break;
            }

            ESP_LOGI(TAG, "Đã chuyển sang STA, chờ kết nối và nhận IP...");
            const TickType_t xWaitStart = xTaskGetTickCount();
            while (!app_wifi_IsConnected() &&
                   ((xTaskGetTickCount() - xWaitStart) < pdMS_TO_TICKS(DF_UDP_STA_IP_WAIT_MS))) {
                vTaskDelay(pdMS_TO_TICKS(DF_UDP_WAIT_STEP_DELAY_MS));
            }

            if (!app_wifi_IsConnected()) {
                ESP_LOGE(TAG, "STA không nhận được IP trong %u ms", DF_UDP_STA_IP_WAIT_MS);
            } else {
                ESP_LOGI(TAG, "STA đã kết nối và nhận IP thành công!");
                g_bAnnounceEnabled = true;
            }

            /* Khởi động lại socket UDP trên giao diện STA để tiếp tục nhận lệnh cấu hình */
            if (g_i32UdpFd >= 0) {
                (void)shutdown(g_i32UdpFd, SHUT_RDWR);
                (void)close(g_i32UdpFd);
                g_i32UdpFd = -1;
            }

            if (app_udp_OpenSocket(DF_UDP_RESTART_DEVICE_PORT) != ESP_OK) {
                ESP_LOGE(TAG, "Không thể mở lại UDP socket trên STA");
                g_bUdpRunning = false;
                break;
            }
            ESP_LOGI(TAG, "UDP server đã mở lại trên cổng %u (STA)", g_u16UdpDevicePort);
            u64LastBroadcastMs = 0U;
            continue;
        }

        if (bExitRequested) {
            ESP_LOGI(TAG, "Nhận yêu cầu kết thúc cấu hình -> Đã lưu NVS, đang khởi động lại thiết bị...");
            vTaskDelay(pdMS_TO_TICKS(500)); /* Chờ 500ms để response UDP được gửi xong */
            esp_restart();
        }
    }

    if (g_i32UdpFd >= 0) {
        (void)close(g_i32UdpFd);
        g_i32UdpFd = -1;
    }
    g_hUdpTaskHandle = NULL;
    ESP_LOGI(TAG, "UDP server task kết thúc");
    vTaskDelete(NULL);
}

/**
 * @brief Khởi tạo giao diện mạng SoftAP để thiết bị di động kết nối trực tiếp.
 * @param None.
 * @return ESP_OK nếu thành công; mã lỗi nếu thất bại.
 */
static esp_err_t app_udp_StartSoftAp(void)
{
    esp_err_t eRet = ESP_OK;

    if (!g_bWifiInitialized) {
        eRet = esp_netif_init();
        if ((eRet != ESP_OK) && (eRet != ESP_ERR_INVALID_STATE)) {
            ESP_LOGE(TAG, "Lỗi khởi tạo netif: %s", esp_err_to_name(eRet));
            return eRet;
        }

        eRet = esp_event_loop_create_default();
        if ((eRet != ESP_OK) && (eRet != ESP_ERR_INVALID_STATE)) {
            ESP_LOGE(TAG, "Lỗi tạo default event loop: %s", esp_err_to_name(eRet));
            return eRet;
        }

        if (g_pUdpApNetif == NULL) {
            g_pUdpApNetif = esp_netif_create_default_wifi_ap();
            if (g_pUdpApNetif == NULL) {
                ESP_LOGE(TAG, "Tạo netif AP mặc định thất bại");
                return ESP_FAIL;
            }
        }

        wifi_init_config_t sInitCfg = WIFI_INIT_CONFIG_DEFAULT();
        eRet = esp_wifi_init(&sInitCfg);
        if ((eRet != ESP_OK) && (eRet != ESP_ERR_INVALID_STATE)) {
            ESP_LOGE(TAG, "Lỗi esp_wifi_init: %s", esp_err_to_name(eRet));
            return eRet;
        }

        g_bWifiInitialized = true;
    }

    wifi_config_t sApConfig = {0};
    (void)snprintf((char *)sApConfig.ap.ssid, sizeof(sApConfig.ap.ssid), "%s", DF_UDP_AP_SSID);
    (void)snprintf((char *)sApConfig.ap.password, sizeof(sApConfig.ap.password), "%s", DF_UDP_AP_PASSWORD);
    sApConfig.ap.ssid_len = (uint8_t)strlen((char *)sApConfig.ap.ssid);
    sApConfig.ap.channel = DF_UDP_AP_CHANNEL;
    sApConfig.ap.max_connection = DF_UDP_AP_MAX_CONN;
    sApConfig.ap.authmode = WIFI_AUTH_WPA2_PSK;

    eRet = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (eRet == ESP_OK) {
        eRet = esp_wifi_set_config(WIFI_IF_AP, &sApConfig);
    }
    if (eRet == ESP_OK) {
        eRet = esp_wifi_start();
    }
    if (eRet != ESP_OK) {
        ESP_LOGE(TAG, "Khởi động SoftAP thất bại: %s", esp_err_to_name(eRet));
    } else {
        ESP_LOGI(TAG, "SoftAP khởi động thành công: SSID=%s", DF_UDP_AP_SSID);
    }

    return eRet;
}

/**
 * @brief Khởi tạo và liên kết (bind) UDP socket lắng nghe kết nối.
 * @param u16DevicePort Số hiệu cổng UDP cần lắng nghe.
 * @return ESP_OK nếu thành công; ESP_FAIL nếu tạo socket hoặc bind thất bại.
 */
static esp_err_t app_udp_OpenSocket(uint16_t u16DevicePort)
{
    g_i32UdpFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (g_i32UdpFd < 0) {
        ESP_LOGE(TAG, "Tạo socket UDP thất bại: errno=%d", errno);
        return ESP_FAIL;
    }

    int32_t i32BroadcastEnabled = 1;
    (void)setsockopt(g_i32UdpFd, SOL_SOCKET, SO_BROADCAST, &i32BroadcastEnabled, sizeof(i32BroadcastEnabled));

    struct timeval sTv = {
        .tv_sec = DF_UDP_RCVTIMEO_SEC,
        .tv_usec = DF_UDP_RCVTIMEO_USEC
    };
    (void)setsockopt(g_i32UdpFd, SOL_SOCKET, SO_RCVTIMEO, &sTv, sizeof(sTv));

    struct sockaddr_in sAddress = {
        .sin_family = AF_INET,
        .sin_port = htons(u16DevicePort),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(g_i32UdpFd, (struct sockaddr *)&sAddress, sizeof(sAddress)) < 0) {
        ESP_LOGE(TAG, "Bind socket cổng %u thất bại: errno=%d", u16DevicePort, errno);
        (void)close(g_i32UdpFd);
        g_i32UdpFd = -1;
        return ESP_FAIL;
    }

    g_u16UdpDevicePort = u16DevicePort;
    return ESP_OK;
}

/**
 * @brief Tạo task FreeRTOS phục vụ UDP server.
 * @param None.
 * @return ESP_OK nếu thành công; ESP_ERR_NO_MEM nếu không đủ bộ nhớ tạo task.
 */
static esp_err_t app_udp_StartServerTask(void)
{
    if (g_hUdpTaskHandle != NULL) {
        ESP_LOGW(TAG, "UDP Task đã đang chạy");
        return ESP_OK;
    }

    g_bUdpRunning = true;
    BaseType_t xReturned = xTaskCreate(app_udp_Task, DF_UDP_TASK_NAME,
                                       DF_UDP_TASK_STACK_SIZE, NULL,
                                       DF_UDP_TASK_PRIORITY, &g_hUdpTaskHandle);
    if (xReturned != pdPASS) {
        g_bUdpRunning = false;
        if (g_i32UdpFd >= 0) {
            (void)close(g_i32UdpFd);
            g_i32UdpFd = -1;
        }
        ESP_LOGE(TAG, "Tạo task UDP server thất bại");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "UDP server đang lắng nghe trên cổng %u", g_u16UdpDevicePort);
    return ESP_OK;
}

/* ====================================================================
 * Triển khai các API công khai (Public APIs)
 * ==================================================================== */

/**
 * @brief Khởi tạo dịch vụ UDP cấu hình thủ công.
 */
esp_err_t app_udp_Init(void)
{
    if (g_bUdpRunning) {
        ESP_LOGW(TAG, "UDP service đã đang hoạt động");
        return ESP_OK;
    }

    g_bAnnounceEnabled = false;
    (void)app_wifi_Disconnect();

    esp_err_t eRet = app_udp_StartSoftAp();
    if (eRet != ESP_OK) {
        return eRet;
    }

    eRet = app_udp_OpenSocket(DF_UDP_SERVER_PORT);
    if (eRet != ESP_OK) {
        return eRet;
    }

    return app_udp_StartServerTask();
}

/**
 * @brief Hủy và dừng dịch vụ UDP cấu hình thủ công.
 */
esp_err_t app_udp_Deinit(void)
{
    g_bUdpRunning = false;
    g_bAnnounceEnabled = false;

    if (g_i32UdpFd >= 0) {
        (void)shutdown(g_i32UdpFd, SHUT_RDWR);
        (void)close(g_i32UdpFd);
        g_i32UdpFd = -1;
    }

    g_hUdpTaskHandle = NULL;

    /* Tắt SoftAP và chuyển wifi mode về STA */
    esp_err_t eRet = esp_wifi_set_mode(WIFI_MODE_STA);
    if (eRet != ESP_OK) {
        ESP_LOGW(TAG, "Chuyển wifi mode về STA gặp lỗi: %s", esp_err_to_name(eRet));
    }

    ESP_LOGI(TAG, "UDP Service đã dừng hoàn tất");
    return ESP_OK;
}

/**
 * @brief Kiểm tra xem dịch vụ UDP có đang hoạt động hay không.
 */
bool app_udp_IsRunning(void)
{
    return g_bUdpRunning;
}

/**
 * @brief Khởi động luồng cấu hình UDP (tương đương app_udp_Init).
 */
esp_err_t app_udp_StartConfiguration(void)
{
    return app_udp_Init();
}

/**
 * @brief Dừng luồng cấu hình UDP (tương đương app_udp_Deinit).
 */
esp_err_t app_udp_StopConfiguration(void)
{
    return app_udp_Deinit();
}
