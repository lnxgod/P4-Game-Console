#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_TIMEOUT 3
#define ESP_ERR_INVALID_ARG 4
#define ESP_ERR_NOT_FOUND 5
#define ESP_LOGI(tag,...) ((void)(tag))
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(x) (x)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
typedef void *SemaphoreHandle_t;
typedef void *TaskHandle_t;
static inline void *xSemaphoreCreateMutex(void){return (void *)1;}
static inline int xSemaphoreTake(void *s,int n){(void)s;(void)n;return 1;}
static inline int xSemaphoreGive(void *s){(void)s;return 1;}
static inline void vSemaphoreDelete(void *s){(void)s;}
static inline void vTaskDelay(int n){(void)n;}
static inline int xTaskCreateWithCaps(void (*f)(void *),const char *n,int bytes,void *arg,int priority,void **out,int caps)
{(void)f;(void)n;(void)bytes;(void)arg;(void)priority;(void)caps;*out=(void *)1;return 1;}
static int64_t mock_time=100000;
static inline int64_t esp_timer_get_time(void){return mock_time;}
static inline const char *esp_err_to_name(int e){(void)e;return "mock";}
typedef int esp_event_base_t;
enum { IP_EVENT=1,WIFI_EVENT,IP_EVENT_STA_GOT_IP,WIFI_EVENT_AP_STACONNECTED,WIFI_EVENT_AP_STADISCONNECTED,WIFI_EVENT_STA_DISCONNECTED,ESP_EVENT_ANY_ID=-1 };
typedef struct {uint32_t addr;} mock_ip_t;
typedef struct {mock_ip_t ip,gw;} esp_netif_ip_info_t;
typedef struct {esp_netif_ip_info_t ip_info;} ip_event_got_ip_t;
typedef int esp_netif_t;
typedef struct {int dummy;} wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
typedef struct {uint8_t ssid[33],bssid[6],primary; int8_t rssi; int authmode;} wifi_ap_record_t;
typedef struct {bool show_hidden;int scan_type;struct{int passive;}scan_time;} wifi_scan_config_t;
typedef struct {struct{uint8_t ssid[32],ssid_len,channel,max_connection;int authmode,beacon_interval;}ap;struct{uint8_t ssid[32],bssid[6],channel;bool bssid_set;struct{int authmode;}threshold;}sta;} wifi_config_t;
enum { WIFI_STORAGE_RAM,WIFI_MODE_AP,WIFI_MODE_STA,WIFI_IF_AP,WIFI_IF_STA,WIFI_AUTH_OPEN,WIFI_SCAN_TYPE_PASSIVE,WIFI_PS_NONE };
static wifi_ap_record_t mock_records[32]; static uint16_t mock_count;
static wifi_config_t mock_config; static int mock_mode;
static inline int esp_netif_init(void){return 0;}
static inline int esp_event_loop_create_default(void){return 0;}
static inline esp_netif_t *esp_netif_create_default_wifi_sta(void){return (void *)1;}
static inline esp_netif_t *esp_netif_create_default_wifi_ap(void){return (void *)2;}
static inline int esp_wifi_init(const wifi_init_config_t *c){(void)c;return 0;}
static inline int esp_wifi_set_storage(int n){(void)n;return 0;}
static inline int esp_event_handler_register(int b,int id,void (*f)(void *,int,int32_t,void *),void *c){(void)b;(void)id;(void)f;(void)c;return 0;}
static inline int esp_wifi_scan_stop(void){return 0;}
static inline int esp_wifi_stop(void){return 0;}
static inline int esp_wifi_set_mode(int n){mock_mode=n;return 0;}
static inline int esp_wifi_set_config(int n,const wifi_config_t *c){(void)n;mock_config=*c;return 0;}
static inline int esp_wifi_start(void){return 0;}
static inline int esp_wifi_set_ps(int n){(void)n;return 0;}
static inline int esp_wifi_connect(void){return 0;}
static inline int esp_netif_get_ip_info(esp_netif_t *n,esp_netif_ip_info_t *ip){(void)n;ip->ip.addr=inet_addr("127.0.0.1");return 0;}
static inline int esp_wifi_scan_start(const wifi_scan_config_t *c,bool wait){(void)c;(void)wait;return 0;}
static inline int esp_wifi_scan_get_ap_records(uint16_t *n,wifi_ap_record_t *records){if(*n>mock_count)*n=mock_count;memcpy(records,mock_records,*n*sizeof(*records));return 0;}
