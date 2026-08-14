#include "readonly_blob_core.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void test_read_seek_stat_and_bounds(void)
{
    uint8_t data[257];
    for (size_t index = 0; index < sizeof(data); ++index) {
        data[index] = (uint8_t)(index ^ 0xa5U);
    }
    readonly_blob_core_t core;
    assert(readonly_blob_core_init(&core, data, sizeof(data), "doom1.wad"));
    assert(!readonly_blob_core_init(&core, data, sizeof(data), "bad/name"));
    assert(readonly_blob_core_init(&core, data, sizeof(data), "doom1.wad"));

    errno = 0;
    assert(readonly_blob_core_open(&core, "/missing.wad", O_RDONLY) == -1);
    assert(errno == ENOENT);
    errno = 0;
    assert(readonly_blob_core_open(&core, "/doom1.wad", O_WRONLY) == -1);
    assert(errno == EROFS);
    errno = 0;
    assert(readonly_blob_core_open(&core, "doom1.wad", O_RDONLY | O_CREAT) == -1);
    assert(errno == EROFS);

    const int fd = readonly_blob_core_open(&core, "/doom1.wad", O_RDONLY);
    assert(fd == 0);
    uint8_t output[32] = {0};
    assert(readonly_blob_core_read(&core, fd, output, 17U) == 17);
    assert(memcmp(output, data, 17U) == 0);
    assert(readonly_blob_core_lseek(&core, fd, -5, SEEK_CUR) == 12);
    assert(readonly_blob_core_read(&core, fd, output, sizeof(output)) == 32);
    assert(memcmp(output, &data[12], sizeof(output)) == 0);

    assert(readonly_blob_core_lseek(&core, fd, -4, SEEK_END) == 253);
    assert(readonly_blob_core_read(&core, fd, output, sizeof(output)) == 4);
    assert(memcmp(output, &data[253], 4U) == 0);
    assert(readonly_blob_core_read(&core, fd, output, 1U) == 0);
    errno = 0;
    assert(readonly_blob_core_lseek(&core, fd, 1, SEEK_END) == -1);
    assert(errno == EINVAL);
    errno = 0;
    assert(readonly_blob_core_lseek(&core, fd, -258, SEEK_END) == -1);
    assert(errno == EINVAL);

    assert(readonly_blob_core_lseek(&core, fd, 7, SEEK_SET) == 7);
    errno = 0;
    assert(readonly_blob_core_lseek(&core, fd, (off_t)INT64_MAX, SEEK_CUR) == -1);
    assert(errno == EINVAL);
    errno = 0;
    assert(readonly_blob_core_lseek(&core, fd, (off_t)INT64_MIN, SEEK_CUR) == -1);
    assert(errno == EINVAL);
    assert(readonly_blob_core_lseek(&core, fd, 0, SEEK_CUR) == 7);
    memset(output, 0, sizeof(output));
    assert(readonly_blob_core_pread(&core, fd, output, 9U, 100) == 9);
    assert(memcmp(output, &data[100], 9U) == 0);
    assert(readonly_blob_core_lseek(&core, fd, 0, SEEK_CUR) == 7);

    struct stat metadata;
    assert(readonly_blob_core_fstat(&core, fd, &metadata) == 0);
    assert(S_ISREG(metadata.st_mode));
    assert((metadata.st_mode & 0222) == 0);
    assert(metadata.st_size == (off_t)sizeof(data));
    assert(readonly_blob_core_stat(&core, "doom1.wad", &metadata) == 0);
    assert(readonly_blob_core_has_open_files(&core));
    assert(readonly_blob_core_close(&core, fd) == 0);
    assert(!readonly_blob_core_has_open_files(&core));
    errno = 0;
    assert(readonly_blob_core_close(&core, fd) == -1);
    assert(errno == EBADF);
}

static void test_descriptor_limit(void)
{
    const uint8_t data[] = {1U, 2U, 3U};
    readonly_blob_core_t core;
    assert(readonly_blob_core_init(&core, data, sizeof(data), "one.bin"));
    int descriptors[READONLY_BLOB_MAX_OPEN_FILES];
    for (int index = 0; index < READONLY_BLOB_MAX_OPEN_FILES; ++index) {
        descriptors[index] = readonly_blob_core_open(&core, "/one.bin", O_RDONLY);
        assert(descriptors[index] == index);
    }
    errno = 0;
    assert(readonly_blob_core_open(&core, "/one.bin", O_RDONLY) == -1);
    assert(errno == EMFILE);
    for (int index = 0; index < READONLY_BLOB_MAX_OPEN_FILES; ++index) {
        assert(readonly_blob_core_close(&core, descriptors[index]) == 0);
    }
}

int main(void)
{
    test_read_seek_stat_and_bounds();
    test_descriptor_limit();
    puts("P4_READONLY_BLOB HOST PASS file=single mode=read-only max_open=8");
    return 0;
}
