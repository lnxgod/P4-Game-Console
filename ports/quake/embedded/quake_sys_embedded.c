// SPDX-License-Identifier: GPL-2.0-or-later

#include "quake_embedded_internal.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
#include "quakegeneric.h"
#include "quakedef.h"

enum {
    FILE_HANDLE_COUNT = 10,
    FILE_ROOT_BYTES = 128,
    /* Keep slow SD reads below the platform watchdog starvation window. */
    FILE_READ_CHUNK_BYTES = 4096,
};

qboolean isDedicated;

static FILE *s_handles[FILE_HANDLE_COUNT];
static char s_allowed_root[FILE_ROOT_BYTES];

bool p4_quake_sys_set_root(const char *root)
{
    if (root == NULL) {
        return false;
    }
    const size_t length = strlen(root);
    if (length == 0U || length >= sizeof(s_allowed_root) || root[0] != '/' ||
        strstr(root, "..") != NULL || strchr(root, '\\') != NULL) {
        return false;
    }
    memcpy(s_allowed_root, root, length + 1U);
    return true;
}

static bool path_allowed(const char *path)
{
    if (path == NULL || s_allowed_root[0] == '\0' || strstr(path, "..") != NULL ||
        strchr(path, '\\') != NULL) {
        return false;
    }
    const size_t root_length = strlen(s_allowed_root);
    return strncmp(path, s_allowed_root, root_length) == 0 &&
        (path[root_length] == '\0' || path[root_length] == '/');
}

FILE *p4_quake_fopen(const char *path, const char *mode)
{
    if (!path_allowed(path) || mode == NULL || mode[0] != 'r' ||
        strchr(mode, '+') != NULL) {
        return NULL;
    }
    return fopen(path, mode);
}

static int find_handle(void)
{
    for (int index = 1; index < FILE_HANDLE_COUNT; ++index) {
        if (s_handles[index] == NULL) {
            return index;
        }
    }
    return -1;
}

static bool valid_handle(int handle)
{
    return handle > 0 && handle < FILE_HANDLE_COUNT && s_handles[handle] != NULL;
}

int Sys_FileOpenRead(char *path, int *handle_out)
{
    if (path == NULL || handle_out == NULL) {
        return -1;
    }
    *handle_out = -1;
    const int handle = find_handle();
    if (handle < 0) {
        return -1;
    }
    FILE *const file = p4_quake_fopen(path, "rb");
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) {
        if (file != NULL) {
            (void)fclose(file);
        }
        return -1;
    }
    const long length = ftell(file);
    if (length < 0L || length > INT32_MAX || fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        return -1;
    }
    s_handles[handle] = file;
    *handle_out = handle;
    if (length >= 64L * 1024L) {
        printf("P4_QUAKE FILE_OPEN path=%s bytes=%ld\n", path, length);
    }
    return (int)length;
}

int Sys_FileOpenWrite(char *path)
{
    (void)path;
    return -1;
}

void Sys_FileClose(int handle)
{
    if (valid_handle(handle)) {
        (void)fclose(s_handles[handle]);
        s_handles[handle] = NULL;
    }
}

void Sys_FileSeek(int handle, int position)
{
    if (valid_handle(handle) && position >= 0) {
        (void)fseek(s_handles[handle], (long)position, SEEK_SET);
    }
}

int Sys_FileRead(int handle, void *destination, int count)
{
    if (!valid_handle(handle) || destination == NULL || count < 0) {
        return 0;
    }
    uint8_t *cursor = destination;
    size_t total = 0U;
    const size_t requested = (size_t)count;
    while (total < requested) {
        size_t chunk = requested - total;
        if (chunk > FILE_READ_CHUNK_BYTES) {
            chunk = FILE_READ_CHUNK_BYTES;
        }
        const size_t actual = fread(
            &cursor[total], 1U, chunk, s_handles[handle]);
        total += actual;
        if (actual != chunk) {
            break;
        }
        if (total < requested) {
            /* One real tick lets both idle tasks service their watchdogs. */
            vTaskDelay(1U);
        }
    }
    return (int)total;
}

int Sys_FileWrite(int handle, void *data, int count)
{
    (void)handle;
    (void)data;
    (void)count;
    return 0;
}

int Sys_FileTime(char *path)
{
    FILE *const file = p4_quake_fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }
    (void)fclose(file);
    return 1;
}

void Sys_mkdir(char *path)
{
    (void)path;
}

void Sys_MakeCodeWriteable(unsigned long start_address, unsigned long length)
{
    (void)start_address;
    (void)length;
}

void Sys_Error(char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    fputs("P4_QUAKE FATAL ", stderr);
    vfprintf(stderr, format, arguments);
    fputc('\n', stderr);
    va_end(arguments);
    p4_quake_report_fatal();
    Host_Shutdown();
    QG_Quit();
}

void Sys_Printf(char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vprintf(format, arguments);
    va_end(arguments);
}

void Sys_Quit(void)
{
    Host_Shutdown();
    QG_Quit();
}

double Sys_FloatTime(void)
{
    return (double)esp_timer_get_time() / 1000000.0;
}

char *Sys_ConsoleInput(void)
{
    return NULL;
}

void Sys_Sleep(void)
{
}

void Sys_SendKeyEvents(void)
{
}

void Sys_HighFPPrecision(void)
{
}

void Sys_LowFPPrecision(void)
{
}
