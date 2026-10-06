// SPDX-License-Identifier: MIT
#ifndef SETTINGS_TEST_SDK_H
#define SETTINGS_TEST_SDK_H
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
typedef unsigned nvs_handle_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NVS_INVALID_LENGTH 0x110c
#define ESP_MAC_BASE 0
#define NVS_READWRITE 1
void test_log(const char *,const char *,...);
#define ESP_LOGI test_log
#define ESP_LOGW test_log
const char *esp_err_to_name(esp_err_t);
esp_err_t esp_read_mac(uint8_t *,int);
esp_err_t nvs_flash_init(void);
esp_err_t nvs_open(const char *,int,nvs_handle_t *);
void nvs_close(nvs_handle_t);
esp_err_t nvs_get_u8(nvs_handle_t,const char *,uint8_t *);
esp_err_t nvs_set_u8(nvs_handle_t,const char *,uint8_t);
esp_err_t nvs_get_str(nvs_handle_t,const char *,char *,size_t *);
esp_err_t nvs_set_str(nvs_handle_t,const char *,const char *);
esp_err_t nvs_get_blob(nvs_handle_t,const char *,void *,size_t *);
esp_err_t nvs_set_blob(nvs_handle_t,const char *,const void *,size_t);
esp_err_t nvs_commit(nvs_handle_t);
#endif
