// SPDX-License-Identifier: MIT
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include "mocks/mock.h"
#include "../src/platform_multiplayer_wifi.c"

static jmp_buf finished;
static bool radio_ready=true;
static unsigned worker_ticks, radio_starts;
static int selected_step;
static bool cancellation;

esp_err_t platform_ble_host_start(void){++radio_starts;return ESP_OK;}
bool platform_ble_host_ready(void){return radio_ready;}
platform_ble_host_status_t platform_ble_host_status(void)
{return (platform_ble_host_status_t){.state=radio_ready?PLATFORM_BLE_HOST_READY:PLATFORM_BLE_HOST_STARTING_RADIO};}

static void cancel_during_setup(int step)
{
    if(step==selected_step) {
        mock_init_hook=NULL;
        platform_multiplayer_wifi_disable();
    }
}

static void retry_on_new_selection(int delay)
{
    mock_time+=(int64_t)delay*1000;
    if(delay!=20) {
        assert(cancellation && !radio_ready);
        platform_multiplayer_wifi_disable();
        return;
    }
    ++worker_ticks;
    platform_multiplayer_wifi_status_t status=platform_multiplayer_wifi_status();
    if(worker_ticks<=2) {
        assert(!status.available && !status.starting);
        if(cancellation) {
            assert(!status.enabled && status.last_error==ESP_OK);
            assert(mock_start_calls==0);
            for(int i=selected_step+1;i<MOCK_INIT_STEPS;++i)assert(mock_init_calls[i]==0);
        } else {
            assert(status.enabled && status.last_error!=ESP_OK);
            assert(mock_init_calls[selected_step]==1);
            assert(radio_starts==1); /* Idle polling must not retry. */
        }
        if(worker_ticks==2) {
            platform_multiplayer_wifi_disable();
            radio_ready=true;
            assert(platform_multiplayer_wifi_enable(NULL,NULL)==ESP_OK);
        }
        return;
    }
    assert(worker_ticks==3);
    assert(status.enabled && status.available && !status.starting && status.last_error==ESP_OK);
    assert(mock_start_calls==1);
    for(int i=0;i<MOCK_INIT_STEPS;++i)
        assert(mock_init_calls[i]==(unsigned)(!cancellation && i==selected_step?2:1));
    platform_multiplayer_wifi_disable();
    longjmp(finished,1);
}

static void test_uncertain_wifi_init(const char *name)
{
    const bool applied=strcmp(name,"lost-init-reply")==0;
    const bool query_error=strcmp(name,"init-query-error")==0;
    const bool cancelled=strcmp(name,"init-query-cancel")==0;
    assert(applied || query_error || cancelled || strcmp(name,"init-not-applied")==0);
    mock_init_apply_then_timeout=applied;
    mock_fail_step=MOCK_WIFI_INIT;
    assert(platform_multiplayer_wifi_enable(NULL,NULL)==ESP_OK);
    assert(initialize(s_generation)==(applied?ESP_ERR_TIMEOUT:ESP_FAIL));
    assert(mock_init_calls[MOCK_WIFI_INIT]==1 && mock_get_mode_calls==0);
    platform_multiplayer_wifi_disable();
    assert(platform_multiplayer_wifi_enable(NULL,NULL)==ESP_OK);
    if(query_error)mock_get_mode_result=ESP_ERR_TIMEOUT;
    if(cancelled)mock_get_mode_hook=platform_multiplayer_wifi_disable;
    const esp_err_t result=initialize(s_generation);
    if(query_error || cancelled) {
        assert(result==(cancelled?ESP_ERR_INVALID_STATE:ESP_ERR_TIMEOUT));
        assert(mock_init_calls[MOCK_WIFI_INIT]==1 && mock_init_calls[MOCK_STORAGE]==0);
    } else {
        assert(result==ESP_OK);
        assert(mock_init_calls[MOCK_WIFI_INIT]==(applied?1U:2U));
        assert(mock_init_calls[MOCK_STORAGE]==1 && mock_init_calls[MOCK_IP_HANDLER]==1);
    }
    assert(mock_get_mode_calls==1);
    if(cancelled)assert(!platform_multiplayer_wifi_status().enabled);
}

int main(int argc,char **argv)
{
    if(argc==2) { test_uncertain_wifi_init(argv[1]);return 0; }
    assert(argc==3);
    cancellation=strcmp(argv[1],"cancel")==0;
    selected_step=atoi(argv[2]);
    assert(selected_step>=-1 && selected_step<MOCK_INIT_STEPS);
    if(cancellation) {
        if(selected_step<0)radio_ready=false;
        else mock_init_hook=cancel_during_setup;
    } else {
        assert(selected_step>=0);
        mock_fail_step=selected_step;
    }
    mock_delay_hook=retry_on_new_selection;
    assert(platform_multiplayer_wifi_enable(NULL,NULL)==ESP_OK);
    if(setjmp(finished)==0)worker(NULL);
    puts("Wi-Fi retry resumes completed setup and respects cancelled selections");
    return 0;
}
