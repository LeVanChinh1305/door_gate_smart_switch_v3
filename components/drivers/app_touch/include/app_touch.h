// note: giao tiếp I2C 

#ifndef APP_TOUCH_H
#define APP_TOUCH_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include <stddef.h> 

#ifdef __cplusplus
extern "C" {
#endif

//I2C Configuration 
#define DF_TOUCH_I2C_PORT            I2C_NUM_0
#define DF_TOUCH_I2C_ADDR            0x37U
#define DF_TOUCH_PIN_SDA             GPIO_NUM_6
#define DF_TOUCH_PIN_SCL             GPIO_NUM_7
#define DF_TOUCH_TIMEOUT_MS          20U
#define DF_TOUCH_I2C_FREQ_HZ         100000U

//Register Addresses 
// Status Registers
#define DF_TOUCH_REG_BUTTON_STAT            0xAAU    // Trạng thái nút hiện tại
#define DF_TOUCH_REG_LATCHED_BUTTON_STAT    0xACU    // Trạng thái nút đã latch (khuyến nghị)
#define DF_TOUCH_REG_PROX_STAT              0xAEU    // Trạng thái proximity
#define DF_TOUCH_REG_LATCHED_PROX_STAT      0xAFU    // Proximity đã latch

#define DF_TOUCH_REG_FAMILY_ID              0x8FU    // Family ID
#define DF_TOUCH_REG_DEVICE_ID              0x90U    // Device ID (2 bytes)
#define DF_TOUCH_REG_DEVICE_REV             0x92U    // Device Revision

#define DF_TOUCH_REG_CTRL_CMD_STATUS        0x88U    // Kết quả lệnh CTRL_CMD
#define DF_TOUCH_REG_CTRL_CMD_ERR           0x89U    // Lỗi lệnh
#define DF_TOUCH_REG_SYSTEM_STATUS          0x8AU    // Trạng thái hệ thống
#define DF_TOUCH_REG_GPO_DATA               0xDAU    // Trạng thái chân GPO

// Command Registers
#define DF_TOUCH_REG_CTRL_CMD               0x86U    // Gửi lệnh (Save, Reset...)
#define DF_TOUCH_REG_GPO_OUTPUT_STATE       0x80U    // Điều khiển GPO từ Host

// Configuration Registers
#define DF_TOUCH_REG_SENSOR_EN              0x00U    // Bật/tắt từng sensor (CS0–CS7)
#define DF_TOUCH_REG_I2C_ADDR               0x51U    // Địa chỉ I2C
#define DF_TOUCH_REG_DEVICE_CFG0            0x4DU
#define DF_TOUCH_REG_DEVICE_CFG1            0x4EU
#define DF_TOUCH_REG_DEVICE_CFG2            0x4FU
#define DF_TOUCH_REG_DEVICE_CFG3            0x50U

#define DF_TOUCH_REG_SENSITIVITY0           0x08U    // Độ nhạy sensor 0-3
#define DF_TOUCH_REG_SENSITIVITY1           0x09U    // Độ nhạy sensor 4-7

#define DF_TOUCH_REG_GPO_CFG                0x40U    // Cấu hình GPO
#define DF_TOUCH_REG_SPO_CFG                0x4CU    // Cấu hình chân đặc biệt (HI/BUZ)
#define DF_TOUCH_REG_REFRESH_CTRL           0x52U    // Tần suất scan
#define DF_TOUCH_REG_STATE_TIMEOUT          0x55U    // Timeout chuyển trạng thái

// Bit-field definitions

// SENSOR_EN (0x00)
#define DF_TOUCH_SENSOR_EN_CS0              (1U << 0)
#define DF_TOUCH_SENSOR_EN_CS1              (1U << 1)
#define DF_TOUCH_SENSOR_EN_CS2              (1U << 2)
#define DF_TOUCH_SENSOR_EN_CS3              (1U << 3)
#define DF_TOUCH_SENSOR_EN_CS4              (1U << 4)
#define DF_TOUCH_SENSOR_EN_CS5              (1U << 5)
#define DF_TOUCH_SENSOR_EN_CS6              (1U << 6)
#define DF_TOUCH_SENSOR_EN_CS7              (1U << 7)   // CS7 = SPO1 trên 3108

// BUTTON_STAT (0xAA) & LATCHED_BUTTON_STAT (0xAC)
// #define DF_TOUCH_BTN_CS0                    (1U << 0)
// #define DF_TOUCH_BTN_CS1                    (1U << 1)
// #define DF_TOUCH_BTN_CS2                    (1U << 2)
// #define DF_TOUCH_BTN_CS3                    (1U << 3)
// #define DF_TOUCH_BTN_CS4                    (1U << 4)
#define DF_TOUCH_BTN_CS5                    (1U << 5)
#define DF_TOUCH_BTN_CS6                    (1U << 6)
#define DF_TOUCH_BTN_CS7                    (1U << 7)

// CTRL_CMD (0x86)
#define DF_TOUCH_CMD_NONE                   0x00U
#define DF_TOUCH_CMD_SAVE_CHECK_CRC         0x02U    // Tính CRC + Save vào Flash
#define DF_TOUCH_CMD_CALC_CRC               0x03U    // Chỉ tính CRC (debug)
#define DF_TOUCH_CMD_ENTER_LOW_POWER        0x07U
#define DF_TOUCH_CMD_CLEAR_LATCHED          0x08U    // Clear LATCHED_BUTTON_STAT & PROX
#define DF_TOUCH_CMD_RESET_PS0_FILTER       0x09U
#define DF_TOUCH_CMD_RESET_PS1_FILTER       0x0AU
#define DF_TOUCH_CMD_SW_RESET               0xFFU    // Software Reset

// CTRL_CMD_STATUS (0x88)
#define DF_TOUCH_CMD_STATUS_ERR             (1U << 0)   // 0 = OK, 1 = lỗi

// CTRL_CMD_ERR (0x89)
#define DF_TOUCH_CMD_ERR_SUCCESS            0x00U
#define DF_TOUCH_CMD_ERR_FLASH_FAIL         0xFDU    // 253
#define DF_TOUCH_CMD_ERR_CRC_MISMATCH       0xFEU    // 254
#define DF_TOUCH_CMD_ERR_INVALID_CMD        0xFFU    // 255

// SYSTEM_STATUS (0x8A)
#define DF_TOUCH_SYS_STATUS_F_DEFAULT       (1U << 0)   // 1 = đang dùng factory default

// GPO_OUTPUT_STATE (0x80)
#define DF_TOUCH_GPO0                       (1U << 0)
#define DF_TOUCH_GPO1                       (1U << 1)
#define DF_TOUCH_GPO2                       (1U << 2)
#define DF_TOUCH_GPO3                       (1U << 3)

// Default Values 
#define DF_TOUCH_DEVICE_ID_DEFAULT          0x0A03U // Little Endian: 0x03 0x0A
#define DF_TOUCH_FAMILY_ID_DEFAULT          0x9AU

// Các fuction thường dùng 
/**
 * @brief   Khởi tạo driver cảm ứng, cấu hình phần cứng và kiểm tra chip.
 * @param   None
 * @return  esp_err_t: ESP_OK nếu thành công, hoặc mã lỗi I2C.
 * @note    Hàm khởi động cốt lõi của module. Hàm này làm 3 việc: 
 *              1. Cấu hình các chân GPIO thành chuẩn I2C.
 *              2. Đăng ký thiết bị với bus I2C của ESP32.
 *              3. Đợi IC cảm ứng khởi động xong và gọi hàm kiểm tra xem IC có giao tiếp thành công không.
 */
esp_err_t app_touch_Init(void);

// Low-level I2C
/**
 * @brief   Ghi 1 byte cấu hình vào một thanh ghi cụ thể trên chip.
 * @param   reg: Địa chỉ thanh ghi cần ghi.
 * @param   value: Giá trị cần ghi vào thanh ghi.
 * @return  esp_err_t: ESP_OK nếu ghi thành công, hoặc mã lỗi I2C.
 * @note    Sở hữu vòng lặp Retry. Nếu môi trường nhiễu làm việc ghi thất bại, hệ thống sẽ nghỉ vài mili-giây rồi ghi lại để tăng độ bền bỉ.
 */
esp_err_t app_touch_WriteReg(uint8_t reg, uint8_t value);

/**
 * @brief   Đọc 1 byte từ một thanh ghi cụ thể trên chip.
 * @param   reg: Địa chỉ thanh ghi cần đọc.
 * @param   value: Con trỏ trỏ tới biến dùng để lưu giá trị đọc về.
 * @return  esp_err_t: ESP_OK nếu đọc thành công, hoặc mã lỗi I2C.
 */
esp_err_t app_touch_ReadReg(uint8_t reg, uint8_t *value);


/**
 * @brief   Đọc nhiều byte liên tiếp từ thanh ghi của chip.
 * @param   reg: Địa chỉ thanh ghi bắt đầu đọc.
 * @param   buf: Con trỏ trỏ tới mảng buffer lưu dữ liệu đọc về.
 * @param   len: Số lượng byte cần đọc.
 * @return  esp_err_t: ESP_OK nếu đọc thành công, hoặc mã lỗi I2C.
 */
esp_err_t app_touch_ReadRegs(uint8_t reg, uint8_t *buf, size_t len);

// Kiểm tra chip
/**
 * @brief   Kiểm tra sự tồn tại và tính hợp lệ của IC cảm ứng trên mạch.
 * @param   None
 * @return  esp_err_t: ESP_OK nếu đúng chip, ESP_ERR_INVALID_RESPONSE nếu sai chip.
 * @note    Hàm này đọc Family ID và Device ID rồi so sánh với giá trị gốc (0x9A và 0x0A03). 
 *          Giúp xác nhận linh kiện hàn trên mạch có chính xác là IC CY8CMBR3108 hay không.
 */
esp_err_t app_touch_CheckDevice(void);


/**
 * @brief   Đọc mã thiết bị (Device ID) của chip.
 * @param   device_id: Con trỏ lưu mã thiết bị (16-bit).
 * @return  esp_err_t: ESP_OK nếu đọc thành công, hoặc mã lỗi I2C.
 * @note    Dữ liệu từ IC gửi về là 2 byte tách rời. Hàm này có xử lý dịch bit (Little Endian) để ghép thành một số 16-bit hoàn chỉnh.
 */
esp_err_t app_touch_GetDeviceId(uint16_t *device_id);




/**
 * @brief   Đọc mã cấu trúc phần cứng (Family ID) của chip.
 * @param   family_id: Con trỏ lưu mã họ thiết bị (8-bit).
 * @return  esp_err_t: ESP_OK nếu đọc thành công.
 */
esp_err_t app_touch_GetFamilyId(uint8_t *family_id);

// Đọc trạng thái nút
/**
 * @brief   Đọc trạng thái thực thời (real-time) của mặt kính.
 * @param   status: Con trỏ lưu byte trạng thái các nút bấm.
 * @return  esp_err_t: ESP_OK nếu đọc thành công.
 * @note    Khi ngón tay đang đè lên nút nào, bit của nút tương ứng trong biến status sẽ mang giá trị 1.
 */
esp_err_t app_touch_ReadButtonStatus(uint8_t *status);


/**
 * @brief   Đọc trạng thái "chốt" (latched) của nút bấm.
 * @param   status: Con trỏ lưu byte trạng thái chốt của các nút.
 * @return  esp_err_t: ESP_OK nếu đọc thành công.
 * @note    Đây là tính năng cốt lõi chống lỡ nhịp bấm. Nếu người dùng chạm tay rất nhanh (chạm rồi thả ra ngay) 
 *          trong lúc ESP32 đang bận xử lý mạng, trạng thái chạm vẫn được IC giữ lại (latch) ở thanh ghi này.
 */
esp_err_t app_touch_ReadLatchedButtonStatus(uint8_t *status);

// Lệnh điều khiển
/**
 * @brief   Gửi lệnh điều khiển (Command) xuống chip cảm ứng.
 * @param   cmd: Mã lệnh cần gửi (ví dụ: Lưu cấu hình, reset filter).
 * @return  esp_err_t: ESP_OK nếu lệnh thực thi thành công, ESP_ERR_TIMEOUT hoặc ESP_FAIL nếu lỗi.
 * @note    Mô phỏng chu trình chuẩn của Cypress: Đợi IC xử lý xong lệnh cũ -> Đẩy lệnh mới -> Đợi IC xác nhận mã lỗi để biết lệnh có thành công hay không.
 */
esp_err_t app_touch_SendCmd(uint8_t cmd);


/**
 * @brief   Gửi lệnh xóa bộ nhớ đệm trạng thái nút bấm (Clear Latched).
 * @param   None
 * @return  esp_err_t: ESP_OK nếu xóa thành công.
 * @note    Thường được gọi ngay sau khi dùng hàm app_touch_ReadLatchedButtonStatus() để dọn dẹp cờ chạm cũ, chuẩn bị nhận lần chạm tiếp theo.
 */
esp_err_t app_touch_ClearLatched(void);


/**
 * @brief   Gửi lệnh yêu cầu IC CY8CMBR3108 tự khởi động lại bằng phần mềm.
 * @param   None
 * @return  esp_err_t: ESP_OK nếu gửi lệnh thành công.
 * @note    Ép ESP32 chờ một khoảng thời gian (100ms) để IC khởi động xong. Thường dùng khi phát hiện IC bị treo hoặc sau khi ghi xong cấu hình mới.
 */
esp_err_t app_touch_SoftReset(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_TOUCH_H */