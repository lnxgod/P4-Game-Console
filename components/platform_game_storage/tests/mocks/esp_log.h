#pragma once
#ifdef P4_TEST_VFS_DIAGNOSTICS
void vfs_test_log(const char *format, ...);
#define ESP_LOGI(tag,...) ((void)(tag), vfs_test_log(__VA_ARGS__))
#else
#include <stdio.h>
#define ESP_LOGI(tag,...) do { (void)(tag); if (0) printf(__VA_ARGS__); } while (0)
#endif
