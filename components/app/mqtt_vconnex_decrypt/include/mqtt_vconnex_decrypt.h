/**
 * @file    mqtt_vconnex_decrypt.h
 * @brief   Khai báo giao diện mã hóa và giải mã payload theo tiêu chuẩn AES-256-CBC của Vconnex.
 */

#ifndef MQTT_VCONNEX_DECRYPT_H
#define MQTT_VCONNEX_DECRYPT_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/* Hằng số cấu hình giới hạn đệm tĩnh */
#define DF_MQTT_CRYPTO_MAX_BUFFER_SIZE  512U

/**
 * @brief   Giải mã bản tin payload từ dạng mã hóa AES-256-CBC sang chuỗi thuần (plaintext).
 * @param   pu8Ciphertext Mảng byte dữ liệu đã mã hóa đầu vào.
 * @param   zCiphertextLen Độ dài của mảng ciphertext (phải là bội số của 16).
 * @param   pcApiSecretKey Khóa bí mật dùng làm khóa giải mã và IV (tối thiểu 32 ký tự).
 * @param   pcPlaintextOut Mảng tĩnh đầu ra để chứa chuỗi plaintext sau giải mã.
 * @param   zPlaintextOutMaxLen Kích thước tối đa của mảng đầu ra.
 * @param   pzPlaintextLenOut Con trỏ trả về độ dài thực tế của chuỗi plaintext.
 * @return  esp_err_t ESP_OK nếu giải mã thành công, ngược lại trả về mã lỗi cụ thể.
 */
esp_err_t decrypt_vconnex_payload(const uint8_t *pu8Ciphertext,
								  size_t zCiphertextLen,
								  const char *pcApiSecretKey,
								  char *pcPlaintextOut,
								  size_t zPlaintextOutMaxLen,
								  size_t *pzPlaintextLenOut);

/**
 * @brief   Mã hóa dữ liệu payload sang dạng AES-256-CBC theo chuẩn Vconnex.
 * @param   pcPlaintext Mảng dữ liệu thô đầu vào.
 * @param   zPlaintextLen Độ dài dữ liệu thô.
 * @param   pcApiSecretKey Khóa bí mật hệ thống.
 * @param   pu8CiphertextOut Mảng tĩnh đầu ra chứa dữ liệu sau mã hóa.
 * @param   zCiphertextOutMaxLen Kích thước tối đa của mảng đầu ra.
 * @param   pzCiphertextLenOut Con trỏ trả về độ dài dữ liệu sau mã hóa.
 * @return  esp_err_t ESP_OK nếu thành công.
 */
esp_err_t encrypt_vconnex_payload(const char *pcPlaintext,
								  size_t zPlaintextLen,
								  const char *pcApiSecretKey,
								  uint8_t *pu8CiphertextOut,
								  size_t zCiphertextOutMaxLen,
								  size_t *pzCiphertextLenOut);

#endif /* MQTT_VCONNEX_DECRYPT_H */