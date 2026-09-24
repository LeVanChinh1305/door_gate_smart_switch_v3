/**
 * @file    app_common.c
 * @brief   Triển khai các macro tiện ích, kiểu trạng thái nghiệp vụ (e_app_status_t), macro kiểm tra tham số & propagate lỗi đã khai báo trong app_common.h
 */

#include "app_common.h"

/**
 * @brief   Quy đổi e_app_status_t -> esp_err_t, dùng khi hàm logic nghiệp vụ cần trả kết quả ra ngoài cho một API mong đợi esp_err_t
 * @param   eStatus: trạng thái nghiệp vụ cần quy đổi
 * @return  esp_err_t: trạng thái lỗi tương ứng gần nhất với eStatus 
 */
esp_err_t app_common_StatusToEspErr(e_app_status_t eStatus)
{
    esp_err_t eRet = ESP_OK;
    switch (eStatus) {
        case E_APP_STATUS_SUCCESS:
            eRet = ESP_OK;
            break;
        case E_APP_STATUS_NO_CHANGE:
            eRet = ESP_OK;
            break;
        case E_APP_STATUS_BUSY:
            eRet = ESP_ERR_INVALID_STATE;
            break;
        case E_APP_STATUS_ERROR:
            eRet = ESP_FAIL;
            break;
        case E_APP_STATUS_TIMEOUT:
            eRet = ESP_ERR_TIMEOUT;
            break;
        case E_APP_STATUS_INVALID_PARAM:
            eRet = ESP_ERR_INVALID_ARG;
            break;
        case E_APP_STATUS_NOT_INITIALIZED:
            eRet = ESP_ERR_INVALID_STATE;
            break;
        case E_APP_STATUS_NOT_SUPPORTED:
            eRet = ESP_ERR_NOT_SUPPORTED;
            break;
        case E_APP_STATUS_NOT_FOUND:
            eRet = ESP_ERR_NOT_FOUND;
            break;
        case E_APP_STATUS_NOT_READY:
            eRet = ESP_ERR_INVALID_STATE;
            break;
        case E_APP_STATUS_ALREADY_EXISTS:
            eRet = ESP_ERR_INVALID_STATE;
            break;
        case E_APP_STATUS_OUT_OF_MEMORY:
            eRet = ESP_ERR_NO_MEM;
            break;
        case E_APP_STATUS_INTERNAL_ERROR:
            eRet = ESP_FAIL;
            break;
        // bao gồm cả trường hợp E_APP_STATUS_UNKNOWN
        default:
            eRet = ESP_FAIL;
            break;
    }
    return eRet;
}