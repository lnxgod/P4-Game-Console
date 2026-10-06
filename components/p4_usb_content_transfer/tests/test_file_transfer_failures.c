// SPDX-License-Identifier: MIT
/* Public wire-protocol regressions with an injected filesystem sync failure. */
#include "fake_sdk.h"
#include "p4/file_transfer.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool fail_sync;
static int sync_descriptor = -1;
static int64_t now_us;
static uint8_t captured_response[256];
static size_t captured_response_bytes;
static int transfer_test_fsync(int descriptor)
{
    sync_descriptor = descriptor;
    if (fail_sync) { errno = EIO; return -1; }
    return fsync(descriptor);
}
#define fsync transfer_test_fsync
#include "../src/file_transfer.c"
#undef fsync

void *heap_caps_malloc(size_t bytes, unsigned caps) { (void)caps; return malloc(bytes); }
void heap_caps_free(void *p) { free(p); }
int64_t esp_timer_get_time(void) { return now_us; }
int esp_task_wdt_reset(void) { return 0; }
void vTaskDelay(uint32_t ticks) { now_us += (int64_t)ticks * 1000; }
void transfer_test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
void mbedtls_sha256_init(mbedtls_sha256_context *c) { p4_sha256_init(c); }
void mbedtls_sha256_free(mbedtls_sha256_context *c) { memset(c, 0, sizeof(*c)); }
int mbedtls_sha256_starts(mbedtls_sha256_context *c, int mode) { assert(mode == 0); p4_sha256_init(c); return 0; }
int mbedtls_sha256_update(mbedtls_sha256_context *c, const unsigned char *b, size_t n) { p4_sha256_update(c, b, n); return 0; }
int mbedtls_sha256_finish(mbedtls_sha256_context *c, unsigned char out[32]) { p4_sha256_finish(c, out); return 0; }
static esp_err_t send_bytes(void *context, const uint8_t *bytes, size_t length)
{
    (void)context;
    assert(captured_response_bytes + length <= sizeof(captured_response));
    memcpy(captured_response + captured_response_bytes, bytes, length); captured_response_bytes += length;
    return ESP_OK;
}
static esp_err_t wait_tx(void *context, uint32_t ms) { (void)context; (void)ms; return ESP_OK; }
static esp_err_t set_baud(void *context, uint32_t baud) { (void)context; (void)baud; return ESP_OK; }
static void put32(uint8_t *p, uint32_t value)
{ for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8U * i)); }
static uint32_t wire_crc(const uint8_t *bytes, size_t length)
{
    uint32_t value = UINT32_MAX;
    while (length--) {
        value ^= *bytes++;
        for (unsigned i = 0; i < 8; ++i)
            value = (value >> 1) ^ ((value & 1U) ? UINT32_C(0xedb88320) : 0U);
    }
    return ~value;
}
static void upload(const char *name, uint8_t value, uint16_t flags)
{
    const uint8_t payload[] = {value, 2, 3, 4};
    uint8_t digest[32]; p4_sha256_t sha;
    p4_sha256_init(&sha); p4_sha256_update(&sha, payload, sizeof(payload)); p4_sha256_finish(&sha, digest);
    uint8_t request_bytes[88] = {'P','4','F','1',1,2};
    request_bytes[6] = (uint8_t)flags;
    put32(request_bytes + 8, sizeof(payload)); memcpy(request_bytes + 12, digest, sizeof(digest));
    assert(strlen(name) < 40); memcpy(request_bytes + 44, name, strlen(name));
    put32(request_bytes + 84, wire_crc(request_bytes, 84)); captured_response_bytes = 0;
    assert(p4_file_transfer_consume(request_bytes, sizeof(request_bytes)));
    assert(captured_response[4] == P4_FILE_TRANSFER_STATUS_OK);
    uint8_t chunk[20] = {'P','4','C','2'}; chunk[8] = sizeof(payload);
    put32(chunk + 12, wire_crc(payload, sizeof(payload))); memcpy(chunk + 16, payload, sizeof(payload));
    captured_response_bytes = 0; assert(p4_file_transfer_consume(chunk, sizeof(chunk)));
}
static void next_transfer(void)
{
    now_us += 1100000; p4_file_transfer_poll();
    assert(p4_file_transfer_info().state == P4_FILE_TRANSFER_IDLE);
}
static void check_file(const char *directory, const char *name, uint8_t value)
{
    char path[512]; assert(snprintf(path, sizeof(path), "%s/TRANSFER/%s", directory, name) > 0);
    FILE *file = fopen(path, "rb"); assert(file);
    uint8_t data[5]; assert(fread(data, 1, sizeof(data), file) == 4);
    assert(data[0] == value && data[1] == 2 && data[2] == 3 && data[3] == 4);
    assert(fclose(file) == 0);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    char root[] = "/tmp/p4-transfer-failures-XXXXXX"; assert(mkdtemp(root));
    const p4_content_transfer_transport_t transport = {send_bytes, wait_tx, set_baud, NULL, 115200};
    assert(p4_file_transfer_init(root, &transport) == ESP_OK); p4_file_transfer_set_available(true);
    if (strcmp(argv[1], "sync") == 0) {
        fail_sync = true; upload("NOTE.TXT", 7, 0);
        assert(p4_file_transfer_info().last_status == P4_FILE_TRANSFER_STATUS_IO);
        assert(sync_descriptor >= 0);
        errno = 0; assert(fcntl(sync_descriptor, F_GETFD) == -1 && errno == EBADF);
        /* A failed write must leave the service usable for a later upload. */
        next_transfer(); fail_sync = false; upload("NOTE.TXT", 8, 0);
        assert(p4_file_transfer_info().state == P4_FILE_TRANSFER_COMPLETE); check_file(root, "NOTE.TXT", 8);
        next_transfer(); fail_sync = true; upload("NOTE.TXT", 9, P4_FILE_TRANSFER_FLAG_REPLACE);
        assert(p4_file_transfer_info().last_status == P4_FILE_TRANSFER_STATUS_IO);
        errno = 0; assert(fcntl(sync_descriptor, F_GETFD) == -1 && errno == EBADF);
        check_file(root, "NOTE.TXT", 8);
    } else {
        assert(strcmp(argv[1], "namespace") == 0);
        upload("NOTE.TXT.P4T", 11, 0); next_transfer();
        upload("NOTE.TXT.P4B", 12, 0); next_transfer();
        /* Without --replace, the unrelated backup-named file must not occupy NOTE.TXT. */
        upload("NOTE.TXT", 13, 0); next_transfer();
        upload("NOTE.TXT", 14, P4_FILE_TRANSFER_FLAG_REPLACE);
        check_file(root, "NOTE.TXT.P4T", 11); check_file(root, "NOTE.TXT.P4B", 12); check_file(root, "NOTE.TXT", 14);
    }
    char path[512];
    const char *names[] = {"NOTE.TXT", "NOTE.TXT.P4T", "NOTE.TXT.P4B"};
    for (size_t i = 0; i < 3; ++i) {
        (void)snprintf(path, sizeof(path), "%s/TRANSFER/%s", root, names[i]);
        assert(unlink(path) == 0 || errno == ENOENT);
    }
    (void)snprintf(path, sizeof(path), "%s/TRANSFER/.P4FT", root);
    assert(rmdir(path) == 0 || errno == ENOENT);
    (void)snprintf(path, sizeof(path), "%s/TRANSFER", root); assert(rmdir(path) == 0); assert(rmdir(root) == 0);
    puts("upload failure isolation passed"); return 0;
}
