// SPDX-License-Identifier: MIT
#ifndef TEST_DOOM_GC_STARTUP_VFS_H
#define TEST_DOOM_GC_STARTUP_VFS_H
#include <stdbool.h>
#include <stddef.h>
void startup_vfs_open(void);
void startup_vfs_close(void);
void startup_vfs_read(size_t bytes);
void startup_vfs_read_failure(size_t bytes, unsigned fail_call);
/* Test clock is shared; ESP-IDF mocks remain isolated by translation unit. */
void startup_storage_read(size_t bytes);
#endif
