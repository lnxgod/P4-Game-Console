// SPDX-License-Identifier: MIT
#ifndef P4_TRANSFER_TEST_SDK_H
#define P4_TRANSFER_TEST_SDK_H
#include <stddef.h>
#include <stdint.h>
#include "sha256.h"
typedef int esp_err_t;
enum { ESP_OK=0, ESP_ERR_INVALID_ARG=1, ESP_ERR_INVALID_STATE=2, ESP_ERR_NO_MEM=3, MALLOC_CAP_SPIRAM=1, MALLOC_CAP_8BIT=2 };
void *heap_caps_malloc(size_t bytes,unsigned caps);
void heap_caps_free(void *pointer);
int64_t esp_timer_get_time(void);
int esp_task_wdt_reset(void);
void vTaskDelay(uint32_t ticks);
void transfer_test_log(const char *tag,const char *format,...);
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_LOGI(...) transfer_test_log(__VA_ARGS__)
#define ESP_LOGW(...) transfer_test_log(__VA_ARGS__)
#define ESP_LOGE(...) transfer_test_log(__VA_ARGS__)
typedef p4_sha256_t mbedtls_sha256_context;
void mbedtls_sha256_init(mbedtls_sha256_context *context);
void mbedtls_sha256_free(mbedtls_sha256_context *context);
int mbedtls_sha256_starts(mbedtls_sha256_context *context,int is224);
int mbedtls_sha256_update(mbedtls_sha256_context *context,const unsigned char *bytes,size_t count);
int mbedtls_sha256_finish(mbedtls_sha256_context *context,unsigned char digest[32]);
#endif
