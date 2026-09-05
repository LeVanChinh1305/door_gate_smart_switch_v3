/**
 * @file    mqtt_vconnex_decrypt.c
 * @brief   Triển khai thuật toán mã hóa/giải mã AES-256-CBC sử dụng mbedTLS (Dùng đệm tĩnh, không malloc/free).
 */

#include "mqtt_vconnex_decrypt.h"
#include "esp_log.h"
#include "mbedtls/aes.h"
#include <string.h>

static const char *TAG = "MQTT_VCONNEX_DECRYPT";

esp_err_t decrypt_vconnex_payload(const uint8_t *pu8Ciphertext,
								  size_t zCiphertextLen,
								  const char *pcApiSecretKey,
								  char *pcPlaintextOut,
								  size_t zPlaintextOutMaxLen,
								  size_t *pzPlaintextLenOut)
{
	int iMbedResult = -1;
	size_t zKeyLen = 0U;
	size_t zPlaintextLen = 0U;
	mbedtls_aes_context sAes;

	/* 1. Kiểm tra tham số đầu vào nghiêm ngặt theo tiêu chuẩn an toàn firmware */
	if (pu8Ciphertext == NULL || pcApiSecretKey == NULL || 
		pcPlaintextOut == NULL || pzPlaintextLenOut == NULL) {
		return ESP_ERR_INVALID_ARG;
	}
	*pzPlaintextLenOut = 0U;

	zKeyLen = strlen(pcApiSecretKey);
	if (zKeyLen < 32U || zCiphertextLen == 0U || (zCiphertextLen % 16U) != 0U ||
		zCiphertextLen >= zPlaintextOutMaxLen) {
		ESP_LOGE(TAG, "Tham số hoặc kích thước ciphertext không hợp lệ: key=%u, cipher=%u",
				 (unsigned)zKeyLen, (unsigned)zCiphertextLen);
		return ESP_ERR_INVALID_SIZE;
	}

	zPlaintextLen = zCiphertextLen;

	/* 2. Thực hiện giải mã AES-256-CBC sử dụng mbedTLS */
	mbedtls_aes_init(&sAes);
	iMbedResult = mbedtls_aes_setkey_dec(&sAes, (const unsigned char *)pcApiSecretKey, 256U);
	if (iMbedResult == 0) {
		uint8_t au8Iv[16];
		(void)memcpy(au8Iv, pcApiSecretKey, sizeof(au8Iv));
		
		/* Giải mã trực tiếp vào bộ đệm tĩnh đầu ra */
		iMbedResult = mbedtls_aes_crypt_cbc(&sAes, MBEDTLS_AES_DECRYPT, zCiphertextLen,
									        au8Iv, pu8Ciphertext, (uint8_t *)pcPlaintextOut);
	}
	mbedtls_aes_free(&sAes);
	
	if (iMbedResult != 0) {
		ESP_LOGE(TAG, "Giải mã AES-256-CBC thất bại, mã lỗi mbedtls: %d", iMbedResult);
		return ESP_FAIL;
	}

	/* 3. Xử lý padding (0x20) thừa ở cuối chuỗi */
	while (zPlaintextLen > 0U && (uint8_t)pcPlaintextOut[zPlaintextLen - 1U] == 0x20U) {
		--zPlaintextLen;
	}
	pcPlaintextOut[zPlaintextLen] = '\0';
	
	*pzPlaintextLenOut = zPlaintextLen;
	return ESP_OK;
}

esp_err_t encrypt_vconnex_payload(const char *pcPlaintext,
								  size_t zPlaintextLen,
								  const char *pcApiSecretKey,
								  uint8_t *pu8CiphertextOut,
								  size_t zCiphertextOutMaxLen,
								  size_t *pzCiphertextLenOut)
{
	int iMbedResult = -1;
	size_t zKeyLen = 0U;
	size_t zCiphertextLen = 0U;
	mbedtls_aes_context sAes;
	
	/* Sử dụng mảng đệm tĩnh nội bộ để xử lý padding, loại bỏ hoàn toàn malloc */
	static uint8_t au8PaddedBuffer[DF_MQTT_CRYPTO_MAX_BUFFER_SIZE];

	if (pcPlaintext == NULL || pcApiSecretKey == NULL || 
		pu8CiphertextOut == NULL || pzCiphertextLenOut == NULL || zPlaintextLen == 0U) {
		return ESP_ERR_INVALID_ARG;
	}
	*pzCiphertextLenOut = 0U;

	zKeyLen = strlen(pcApiSecretKey);
	if (zKeyLen < 32U) {
		return ESP_ERR_INVALID_SIZE;
	}

	/* Tính toán độ dài sau khi căn chỉnh chuẩn block 16 bytes (PKCS/Space padding) */
	zCiphertextLen = ((zPlaintextLen + 15U) / 16U) * 16U;
	if (zCiphertextLen > DF_MQTT_CRYPTO_MAX_BUFFER_SIZE || zCiphertextLen > zCiphertextOutMaxLen) {
		ESP_LOGE(TAG, "Dữ liệu vượt quá kích thước đệm tĩnh cho phép");
		return ESP_ERR_INVALID_SIZE;
	}

	(void)memset(au8PaddedBuffer, 0, sizeof(au8PaddedBuffer));
	(void)memcpy(au8PaddedBuffer, pcPlaintext, zPlaintextLen);
	(void)memset(au8PaddedBuffer + zPlaintextLen, 0x20, zCiphertextLen - zPlaintextLen);

	mbedtls_aes_init(&sAes);
	iMbedResult = mbedtls_aes_setkey_enc(&sAes, (const unsigned char *)pcApiSecretKey, 256U);
	if (iMbedResult == 0) {
		uint8_t au8Iv[16];
		(void)memcpy(au8Iv, pcApiSecretKey, sizeof(au8Iv));
		iMbedResult = mbedtls_aes_crypt_cbc(&sAes, MBEDTLS_AES_ENCRYPT, zCiphertextLen,
									        au8Iv, au8PaddedBuffer, pu8CiphertextOut);
	}
	mbedtls_aes_free(&sAes);

	if (iMbedResult != 0) {
		ESP_LOGE(TAG, "Mã hóa AES-256-CBC thất bại, mã lỗi mbedtls: %d", iMbedResult);
		return ESP_FAIL;
	}
	
	*pzCiphertextLenOut = zCiphertextLen;
	return ESP_OK;
}