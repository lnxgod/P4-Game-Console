// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/xusb_host.c"

static uint8_t configuration[]={9,2,32,0,1,1,0,0x80,50,9,4,0,0,2,255,93,1,0,
                               7,5,0x81,3,32,0,4,7,5,1,3,32,0,8};
static unsigned opens,closes,claims,releases,submits,halts,flushes,frees;
static bool fail_open,fail_claim,fail_alloc,fail_submit,fail_release,fail_close,fail_flush;
static bool bus_pending;
static int object;
static void reset(void)
{
    /* A simulated reboot disposes resources deliberately retained after faults. */
    if(s.transfer) {free(s.transfer->data_buffer);free(s.transfer);}
    memset(&s,0,sizeof(s)); platform_gamepad_model_init(&s.model);
    s.state=SERVICE_RUNNING;s.client=&object;
    opens=closes=claims=releases=submits=halts=flushes=frees=0;
    fail_open=fail_claim=fail_alloc=fail_submit=fail_release=fail_close=fail_flush=false;
    bus_pending=false;configuration[16]=1;
}
static void attach(void)
{
    const usb_host_client_event_msg_t e={.event=USB_HOST_CLIENT_EVENT_NEW_DEV,.new_dev.address=3};
    client_event(&e,NULL);worker_step();
}
static void complete(const uint8_t *packet,size_t bytes)
{
    assert(s.transfer && bus_pending);bus_pending=false;
    s.transfer->status=USB_TRANSFER_STATUS_COMPLETED;
    s.transfer->actual_num_bytes=(int)bytes;
    if(bytes<=64U) memcpy(s.transfer->data_buffer,packet,bytes);
    transfer_done(s.transfer);
}
static void remove_pad(void)
{
    const usb_host_client_event_msg_t e={.event=USB_HOST_CLIENT_EVENT_DEV_GONE,.dev_gone.dev_hdl=&object};
    client_event(&e,NULL);
    platform_gamepad_snapshot_t snap;
    assert(platform_gamepad_xusb_get_snapshot(&snap)==ESP_OK);
    assert(!snap.state.connected && gamepad_state_is_neutral(&snap.state));
}
static void lifecycle(void)
{
    reset();attach();assert(claims==1 && submits==1);
    const uint32_t first=s.session;
    uint8_t packet[20]={0,20,0x11,0x10};
    complete(packet,20);worker_step();
    platform_gamepad_snapshot_t snap;
    assert(platform_gamepad_xusb_get_snapshot(&snap)==ESP_OK);
    assert(snap.state.buttons==(GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_START)|GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_SOUTH)));
    remove_pad();worker_step();
    assert(halts==1 && flushes==1 && frees==0 && closes==0);
    /* A reconnect event during cancellation must not be lost. */
    const usb_host_client_event_msg_t reconnect={.event=USB_HOST_CLIENT_EVENT_NEW_DEV,.new_dev.address=4};
    client_event(&reconnect,NULL);
    complete(packet,20); /* stale completion must not resurrect held buttons */
    worker_step();assert(frees==1 && releases==1 && closes==1);
    worker_step();assert(s.session!=first && submits==3);
    assert(platform_gamepad_xusb_get_snapshot(&snap)==ESP_OK);
    assert(snap.state.connected && gamepad_state_is_neutral(&snap.state));
    s.stop_requested=true;worker_step();assert(flushes==2);
    complete(packet,20);worker_step();assert(closes==2 && frees==2);
}
static void failures(void)
{
    reset();fail_open=true;attach();assert(!s.device && closes==0);
    reset();configuration[16]=0x81;attach();worker_step();assert(claims==0 && closes==1);
    reset();fail_claim=true;attach();worker_step();assert(closes==1 && releases==0);
    reset();fail_alloc=true;attach();worker_step();assert(closes==1 && releases==1);
    reset();fail_submit=true;attach();worker_step();assert(closes==1 && frees==1);
    reset();attach();uint8_t short_packet[3]={0,20,1};
    complete(short_packet,3);worker_step();assert(s.closing && !s.pending);
    worker_step();assert(closes==1 && frees==1);
    reset();attach();remove_pad();fail_flush=true;worker_step();
    assert(s.cleanup_fault && s.device && s.transfer && frees==0);
    reset();attach();remove_pad();worker_step();
    const uint8_t packet[20]={0,20};complete(packet,20);
    fail_release=true;worker_step();assert(s.cleanup_fault && closes==0 && frees==0);
    reset();attach();remove_pad();worker_step();complete(packet,20);
    fail_close=true;worker_step();assert(s.cleanup_fault && releases==1 && frees==0);
    reset();
}
int main(void){lifecycle();failures();puts("XUSB lifecycle: bounds, hotplug, cancellation and fault retention passed");return 0;}

void fake_log(const char *tag,const char *fmt,...){(void)tag;(void)fmt;}
const char *esp_err_to_name(esp_err_t e){(void)e;return "mock";}
int64_t esp_timer_get_time(void){static int64_t time;return ++time;}
EventGroupHandle_t xEventGroupCreate(void){return &object;}
void vEventGroupDelete(EventGroupHandle_t g){(void)g;}
EventBits_t xEventGroupSetBits(EventGroupHandle_t g,EventBits_t b){(void)g;return b;}
EventBits_t xEventGroupWaitBits(EventGroupHandle_t g,EventBits_t b,int a,int c,TickType_t t){(void)g;(void)b;(void)a;(void)c;(void)t;return 0;}
int xTaskCreate(void (*f)(void *),const char *n,unsigned z,void *a,unsigned p,TaskHandle_t *h){(void)f;(void)n;(void)z;(void)a;(void)p;(void)h;return 0;}
void vTaskSuspend(TaskHandle_t t){(void)t;}
void vTaskDelete(TaskHandle_t t){(void)t;}
int mbedtls_sha256(const unsigned char *d,size_t n,unsigned char *out,int m){(void)d;(void)n;(void)m;memset(out,1,32);return 0;}
esp_err_t platform_usb_host_class_acquire(platform_usb_class_t c,platform_usb_class_lease_t *l){(void)c;(void)l;return ESP_OK;}
esp_err_t platform_usb_host_class_release(platform_usb_class_lease_t *l){(void)l;return ESP_OK;}
esp_err_t platform_usb_host_get_info(platform_usb_host_info_t *i){i->state=PLATFORM_USB_HOST_QUIESCING;return ESP_OK;}
esp_err_t platform_gamepad_register_provider(platform_gamepad_transport_t t,platform_gamepad_snapshot_provider_t p){(void)t;(void)p;return ESP_OK;}
void platform_gamepad_unregister_provider(platform_gamepad_transport_t t,platform_gamepad_snapshot_provider_t p){(void)t;(void)p;}
esp_err_t usb_host_client_register(const usb_host_client_config_t *c,usb_host_client_handle_t *h){(void)c;*h=&object;return ESP_OK;}
esp_err_t usb_host_client_deregister(usb_host_client_handle_t h){(void)h;return ESP_OK;}
esp_err_t usb_host_client_handle_events(usb_host_client_handle_t h,TickType_t t){(void)h;(void)t;return ESP_ERR_TIMEOUT;}
esp_err_t usb_host_device_open(usb_host_client_handle_t c,uint8_t a,usb_device_handle_t *h){(void)c;(void)a;opens++;if(fail_open)return ESP_FAIL;*h=&object;return ESP_OK;}
esp_err_t usb_host_device_close(usb_host_client_handle_t c,usb_device_handle_t d){(void)c;(void)d;assert(!bus_pending);closes++;return fail_close?ESP_FAIL:ESP_OK;}
esp_err_t usb_host_get_active_config_descriptor(usb_device_handle_t d,const usb_config_desc_t **c){(void)d;*c=(const usb_config_desc_t *)configuration;return ESP_OK;}
esp_err_t usb_host_get_device_descriptor(usb_device_handle_t d,const usb_device_desc_t **c){(void)d;static const usb_device_desc_t desc={0x045e,0x028e};*c=&desc;return ESP_OK;}
esp_err_t usb_host_interface_claim(usb_host_client_handle_t c,usb_device_handle_t d,uint8_t i,uint8_t a){(void)c;(void)d;(void)i;(void)a;if(fail_claim)return ESP_FAIL;claims++;return ESP_OK;}
esp_err_t usb_host_interface_release(usb_host_client_handle_t c,usb_device_handle_t d,uint8_t i){(void)c;(void)d;(void)i;assert(!bus_pending);releases++;return fail_release?ESP_FAIL:ESP_OK;}
esp_err_t usb_host_transfer_alloc(size_t n,int z,usb_transfer_t **t){(void)z;if(fail_alloc)return ESP_ERR_NO_MEM;*t=calloc(1,sizeof(**t));assert(*t);(*t)->data_buffer=calloc(n,1);assert((*t)->data_buffer);return ESP_OK;}
esp_err_t usb_host_transfer_free(usb_transfer_t *t){assert(!bus_pending);free(t->data_buffer);free(t);frees++;return ESP_OK;}
esp_err_t usb_host_transfer_submit(usb_transfer_t *t){assert(t && !bus_pending);if(fail_submit)return ESP_FAIL;bus_pending=true;submits++;return ESP_OK;}
esp_err_t usb_host_endpoint_halt(usb_device_handle_t d,uint8_t e){(void)d;(void)e;halts++;return ESP_OK;}
esp_err_t usb_host_endpoint_flush(usb_device_handle_t d,uint8_t e){(void)d;(void)e;flushes++;return fail_flush?ESP_FAIL:ESP_OK;}
