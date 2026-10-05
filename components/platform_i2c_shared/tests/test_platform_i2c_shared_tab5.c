#include "platform_i2c_shared/bus.h"
#include "platform/tab5.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

struct mock_i2c_bus { int token; };
static struct mock_i2c_bus board_bus;
static esp_err_t init_result;
static unsigned init_calls;
esp_err_t platform_tab5_init(void) { ++init_calls;return init_result; }
i2c_master_bus_handle_t platform_tab5_i2c(void) { return &board_bus; }
esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus,uint16_t address,int timeout)
{ assert(bus==&board_bus && address==0x43 && timeout==100);return ESP_OK; }
/* Any physical allocation/deletion by a borrowed client is a failure. */
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *cfg,i2c_master_bus_handle_t *out)
{ (void)cfg;(void)out;assert(0);return ESP_FAIL; }
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus)
{ (void)bus;assert(0);return ESP_FAIL; }
int main(void)
{
    platform_i2c_shared_t *bus=NULL,*second=NULL;
    init_result=ESP_FAIL;assert(platform_i2c_shared_create(&bus)==ESP_FAIL && !bus);
    init_result=ESP_OK;assert(platform_i2c_shared_create(&bus)==ESP_OK && bus);
    assert(init_calls==2 && platform_i2c_shared_handle(bus)==&board_bus);
    assert(platform_i2c_shared_create(&second)==ESP_ERR_INVALID_STATE && !second);
    assert(init_calls==2);
    assert(platform_i2c_shared_probe(bus,0x43,100)==ESP_OK);
    assert(platform_i2c_shared_probe(bus,0x78,100)==ESP_ERR_INVALID_ARG);
    assert(platform_i2c_shared_probe(bus,0x43,1001)==ESP_ERR_INVALID_ARG);
    assert(platform_i2c_shared_destroy(&bus)==ESP_OK && !bus);
    assert(platform_i2c_shared_create(&bus)==ESP_OK && platform_i2c_shared_handle(bus)==&board_bus);
    assert(platform_i2c_shared_destroy(&bus)==ESP_OK && !bus);
    puts("TAB5 SHARED BUS PASS board-owned lifetime survives client handoff");return 0;
}
