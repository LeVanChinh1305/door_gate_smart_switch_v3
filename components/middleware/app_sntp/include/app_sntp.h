#ifndef APP_SNTP_H
#define APP_SNTP_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Khởi tạo dịch vụ SNTP để đồng bộ thời gian thực
 */
void app_sntp_Init(void);

/**
 * @brief Chờ quá trình đồng bộ thời gian hoàn tất (Có Timeout)
 * @param u32TimeoutMs Thời gian chờ tối đa (milli-giây)
 * @return true nếu đồng bộ thành công, false nếu quá thời gian chờ
 */
bool app_sntp_WaitForSync(uint32_t u32TimeoutMs);

/**
 * @brief Gán thời gian hệ thống thủ công từ Unix Timestamp
 * @param i64TimestampSec Thời gian tính bằng giây
 */
void app_sntp_SetSystemTime(int64_t i64TimestampSec);

#endif