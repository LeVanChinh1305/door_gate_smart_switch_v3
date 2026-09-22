/**
 * @file    app_common.h
 * @brief   Định nghĩa dùng chung toàn dự án: macro tiện ích, kiểu trạng thái
 *          nghiệp vụ (e_app_status_t), macro kiểm tra tham số & propagate lỗi.
 *
 * @note    Không đặt macro/hằng số chỉ dùng riêng cho 1 module vào đây
 *          (địa chỉ thanh ghi, GPIO pin, timing phần cứng...). Những thứ đó
 *          vẫn khai báo trong header của chính module đó.
 */

#pragma once    

#include <stdint.h>     // kiểu số nguyên chuẩn
#include <stdbool.h>    // kiểu boolean chuẩn
#include <stddef.h>     // kiểu size_t chuẩn
#include <esp_err.h>    // ESP-IDF error code type

#ifdef __cplusplus      
extern "C" {
#endif
//=========================================================================================================
//============Macro tiện ích chung========================
//=========================================================================================================
#define DF_UNUSED(x) ((void)(x))                                    // ép kiểu biến không dùng thành void để tránh cảnh báo compiler
#define DF_ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))                 // số lượng phần tử trong mảng tĩnh: sizeof(mảng)/sizeof(phần tử đầu tiên), lưu ý: không dùng được với con trỏ
#define DF_MIN(a,b) (((a) < (b))?(a) : (b))                         // giá trị nhỏ nhất giữa hai giá trị
#define DF_MAX(a,b) (((a) > (b))?(a) : (b))                         // giá trị lớn nhất giữa hai giá trị
#define DF_CLAMP(x, min, max) (DF_MIN(DF_MAX((x), (min)), (max)))   // ép x về khoảng [min, max]



//=========================================================================================================
//============Hằng số dùng chung nhiều module==============
//=========================================================================================================
// số lượng phần tử queue 
#define DF_QUEUE_LENGTH_SMALL   (5U)
#define DF_QUEUE_LENGTH_MEDIUM  (10U)
#define DF_QUEUE_LENGTH_LARGE   (20U)

// Thời gian timeout Watchdog Task — dùng trong app_main.c khi init WDT và trong mọi task chạy dài (touch/relay/led) để biết chu kỳ phải "cho ăn". 
#define DF_WDT_TIMEOUT_MS        (10000UL)
// Mức ưu tiên Task (Task Priorities): Việc quy hoạch tập trung mức ưu tiên cho các luồng xử lý giúp kiểm soát luồng thực thi và ngăn chặn hiện tượng tranh chấp tài nguyên (Priority Inversion)
#define DF_TASK_PRIO_MAX        (6)   // dùng cho OTA 
#define DF_TASK_PRIO_CRITICAL   (5)   // relay_task — lệnh dừng/đảo chiều cửa
#define DF_TASK_PRIO_HIGH       (4)   // touch_task — đọc phím chạm
#define DF_TASK_PRIO_NORMAL     (3)   // udp_task, VÀ mqtt_cfg.task.priority (set tường minh để tránh trùng prio=5 với relay_task)
#define DF_TASK_PRIO_LOW        (1)   // led_task, buzzer_task
// Task Stack Size
#define DF_TASK_STACK_MIN       (1024)// siêu nhỏ 
#define DF_TASK_STACK_SMALL     (2048) // Dành cho task cơ bản, ít biến cục bộ (điều khiển Relay, hiệu ứng LED)
#define DF_TASK_STACK_MEDIUM    (3072) // Dành cho task logic vừa phải, xử lý mảng dữ liệu nội bộ hoặc chuỗi ngắn
#define DF_TASK_STACK_NETWORK   (4096) // Dành cho task giao tiếp mạng cơ bản (BluFi, MQTT, UDP) không mã hóa SSL
#define DF_TASK_STACK_LARGE     (6144) // Dành cho task xử lý cấu trúc dữ liệu phức tạp (phân tích chuỗi JSON lớn, cJSON)
#define DF_TASK_STACK_MAX       (8192) // Dành cho các tác vụ ngốn RAM đặc thù như OTA Update hoặc kết nối HTTPS/TLS
// Định danh phiên bản (Firmware/Hardware Version): Tham số bắt buộc phải có để đồng bộ lên server, phục vụ cho việc quản lý thiết bị và nâng cấp phần mềm từ xa (OTA).
#define DF_FIRMWARE_VERSION     "1.1.1"
#define DF_HARDWARE_REVISION    "V2"
// Thời gian đồng bộ hệ thống (Global Timings)
#define DF_INTERLOCK_DELAY_MS   (100)     // thời gian chờ giữa các lệnh điều khiển Relay để tránh xung đột cơ khí (cửa cuốn, cửa cổng)
#define DF_NETWORK_TIMEOUT_MS   (5000)    // thời gian chờ kết nối mạng (WiFi, MQTT, UDP) trước khi báo lỗi
#define DF_WIFI_CONNECT_TIMEOUT_MS  (120000) // thời gian chờ kết nối WiFi trước khi báo lỗi
#define DF_DEVICE_BEFORE_RESTART (1000) // Thời gian trước khi thiết bị restart 




//=========================================================================================================
//============Kiểu trạng thái nghiệp vụ dùng chung ========
/**
 * @brief Mã trạng thái cho các hàm LOGIC NGHIỆP VỤ (app_logic, app_device...).
 *
 * Phân biệt rõ với esp_err_t:
 *  - esp_err_t      :  - dùng cho hàm gọi trực tiếp driver/ESP-IDF (I2C, GPIO, Wi-Fi, NVS...) 
 *                      - các hàm này trả về các mã lỗi chuẩn của ESP-IDF (ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_TIMEOUT...).
 *  - e_app_status_t : dùng cho hàm xử lý nghiệp vụ nội bộ, nơi cần các trạng thái mà esp_err_t không diễn tả được, ví dụ "cửa đang chạy dở, chưa nhận lệnh mới" (E_APP_STATUS_BUSY).
 */
//=========================================================================================================
typedef enum{
    E_APP_STATUS_SUCCESS = 0,       // hàm thực thi thành công
    E_APP_STATUS_NO_CHANGE,         // hàm thực thi thành công nhưng không có thay đổi trạng thái (ví dụ: lệnh mở cửa khi cửa đã mở)
    E_APP_STATUS_BUSY,              // hàm thực thi thất bại vì thiết bị đang bận 
    E_APP_STATUS_ERROR,             // hàm thực thi thất bại vì lỗi không xác định
    E_APP_STATUS_TIMEOUT,           // hàm thực thi thất bại vì timeout
    E_APP_STATUS_INVALID_PARAM,     // hàm thực thi thất bại vì tham số đầu vào không hợp lệ
    E_APP_STATUS_NOT_INITIALIZED,   // hàm thực thi thất bại vì thiết bị chưa được khởi tạo
    E_APP_STATUS_NOT_SUPPORTED,     // hàm thực thi thất bại vì tính năng chưa được hỗ trợ
    E_APP_STATUS_NOT_FOUND,         // hàm thực thi thất bại vì không tìm thấy thiết bị/phần tử
    E_APP_STATUS_NOT_READY,         // hàm thực thi thất bại vì thiết bị chưa sẵn sàng
    E_APP_STATUS_ALREADY_EXISTS,    // hàm thực thi thất bại vì thiết bị/phần tử đã tồn tại
    E_APP_STATUS_OUT_OF_MEMORY,     // hàm thực thi thất bại vì hết bộ nhớ
    E_APP_STATUS_INTERNAL_ERROR,    // hàm thực thi thất bại vì lỗi nội bộ
    E_APP_STATUS_UNKNOWN            // hàm thực thi thất bại vì lỗi không xác định
} e_app_status_t;

/** Quy đổi e_app_status_t -> esp_err_t, dùng khi hàm logic nghiệp vụ cần trả kết quả ra ngoài cho một API mong đợi esp_err_t 
 * (vd: đăng ký callback của ESP-IDF). Định nghĩa trong app_common.c. */
esp_err_t app_common_StatusToEspErr(e_app_status_t eStatus);



//=========================================================================================================
//============Macro kiểm tra tham số & propagate lỗi========
//=========================================================================================================
// Macro kiểm tra tham số đầu vào của hàm, nếu sai thì log lỗi và return E_APP_STATUS_INVALID_PARAM
// dùng cho hàm trả về esp_err_t 
#define DF_CHECK_NULL_PARAM(ptr)                                \
    do {                                                        \
        if ((ptr) == NULL) {                                    \
            return ESP_ERR_INVALID_ARG;                         \
        }                                                       \
    } while(0)

// dùng cho hàm trả về e_app_status_t
#define DF_CHECK_NULL_PARAM_STATUS(ptr)                         \
    do{                                                         \
        if((ptr)== NULL){                                       \
            return E_APP_STATUS_INVALID_PARAM;                  \
        }                                                       \
    } while(0)

// gọi 1 hàm trả về e_app_status_t, nếu thất bại thì log lỗi và return luôn trạng thái đó - tránh lặp lại nhiều lần đoạn kiểm tra lỗi
#define DF_RETURN_IF_ERROR(expr)    \
    do{                             \
        esp_err_t eErrRet = (expr); \
        if(eErrRet != ESP_OK){      \
            return eErrRet;         \
        }                           \
    } while(0) // sau này chỉ cần gọi DF_RETURN_IF_ERROR(func(...)) thay vì viết 3 dòng kiểm tra lỗi như trên, giúp code gọn hơn và dễ đọc hơn



/* Các Macro hỗ trợ lấy dữ liệu từ JSON siêu gọn */
#define UPDATE_CFG_INT(json, key, dest) \
    do { \
        const cJSON *item = cJSON_GetObjectItem(json, key); \
        if (cJSON_IsNumber(item) && (dest) != item->valueint) { \
            (dest) = item->valueint; \
            bConfigChanged = true; \
        } \
    } while(0)

#define UPDATE_CFG_UINT32(json, key, dest) \
    do { \
        const cJSON *item = cJSON_GetObjectItem(json, key); \
        if (cJSON_IsNumber(item) && (dest) != (uint32_t)item->valuedouble) { \
            (dest) = (uint32_t)item->valuedouble; \
            bConfigChanged = true; \
        } \
    } while(0)

#ifdef __cplusplus
}
#endif 


