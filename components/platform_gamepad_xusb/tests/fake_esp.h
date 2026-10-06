#ifndef FAKE_ESP_H
#define FAKE_ESP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
enum {ESP_OK=0, ESP_FAIL=-1, ESP_ERR_INVALID_ARG=1, ESP_ERR_INVALID_STATE=2,
      ESP_ERR_TIMEOUT=3, ESP_ERR_NO_MEM=4, ESP_ERR_INVALID_RESPONSE=5};
typedef unsigned TickType_t;
typedef unsigned EventBits_t;
typedef void *EventGroupHandle_t;
typedef void *TaskHandle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define pdMS_TO_TICKS(n) (n)
#define pdFALSE 0
#define pdTRUE 1
#define pdPASS 1
void fake_log(const char *,const char *,...);
#define ESP_LOGI(...) fake_log(__VA_ARGS__)
#define ESP_LOGD(...) fake_log(__VA_ARGS__)
#define ESP_LOGW(...) fake_log(__VA_ARGS__)
#define ESP_LOGE(...) fake_log(__VA_ARGS__)
const char *esp_err_to_name(esp_err_t);
int64_t esp_timer_get_time(void);
EventGroupHandle_t xEventGroupCreate(void);
void vEventGroupDelete(EventGroupHandle_t);
EventBits_t xEventGroupSetBits(EventGroupHandle_t,EventBits_t);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t,EventBits_t,int,int,TickType_t);
int xTaskCreate(void (*)(void *),const char *,unsigned,void *,unsigned,TaskHandle_t *);
void vTaskSuspend(TaskHandle_t);
void vTaskDelete(TaskHandle_t);
int mbedtls_sha256(const unsigned char *,size_t,unsigned char *,int);
typedef void *usb_device_handle_t;
typedef void *usb_host_client_handle_t;
typedef struct usb_transfer usb_transfer_t;
struct usb_transfer {
    usb_device_handle_t device_handle;
    uint8_t bEndpointAddress;
    void (*callback)(usb_transfer_t *);
    void *context;
    int num_bytes,actual_num_bytes,status;
    uint8_t *data_buffer;
};
enum {USB_HOST_CLIENT_EVENT_NEW_DEV=1,USB_HOST_CLIENT_EVENT_DEV_GONE=2,
      USB_TRANSFER_STATUS_COMPLETED=0};
typedef struct {
    int event;
    struct {uint8_t address;} new_dev;
    struct {usb_device_handle_t dev_hdl;} dev_gone;
} usb_host_client_event_msg_t;
typedef struct {
    bool is_synchronous;
    int max_num_event_msg;
    struct {
        void (*client_event_callback)(const usb_host_client_event_msg_t *,void *);
        void *callback_arg;
    } async;
} usb_host_client_config_t;
typedef struct __attribute__((packed)) {
    uint8_t bLength,bDescriptorType;
    uint16_t wTotalLength;
} usb_config_desc_t;
typedef struct {uint16_t idVendor,idProduct;} usb_device_desc_t;
esp_err_t usb_host_client_register(const usb_host_client_config_t *,usb_host_client_handle_t *);
esp_err_t usb_host_client_deregister(usb_host_client_handle_t);
esp_err_t usb_host_client_handle_events(usb_host_client_handle_t,TickType_t);
esp_err_t usb_host_device_open(usb_host_client_handle_t,uint8_t,usb_device_handle_t *);
esp_err_t usb_host_device_close(usb_host_client_handle_t,usb_device_handle_t);
esp_err_t usb_host_get_active_config_descriptor(usb_device_handle_t,const usb_config_desc_t **);
esp_err_t usb_host_get_device_descriptor(usb_device_handle_t,const usb_device_desc_t **);
esp_err_t usb_host_interface_claim(usb_host_client_handle_t,usb_device_handle_t,uint8_t,uint8_t);
esp_err_t usb_host_interface_release(usb_host_client_handle_t,usb_device_handle_t,uint8_t);
esp_err_t usb_host_transfer_alloc(size_t,int,usb_transfer_t **);
esp_err_t usb_host_transfer_free(usb_transfer_t *);
esp_err_t usb_host_transfer_submit(usb_transfer_t *);
esp_err_t usb_host_endpoint_halt(usb_device_handle_t,uint8_t);
esp_err_t usb_host_endpoint_flush(usb_device_handle_t,uint8_t);
#endif
