// SPDX-License-Identifier: MIT
#include "test_doom_gc_startup_vfs.h"
#include "platform/readonly_blob.h"
#include "platform/game_storage.h"
#include "esp_vfs.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const esp_vfs_fs_ops_t *ops;
static void *vfs_context;
static int wad_fd;
static unsigned reads, fail_read;
esp_err_t esp_vfs_register_fs(const char *path,const esp_vfs_fs_ops_t *o,int flags,void *context)
{ assert(!strcmp(path,"/doom") && (flags&ESP_VFS_FLAG_READONLY_FS));ops=o;vfs_context=context;return ESP_OK; }
esp_err_t esp_vfs_unregister_fs(const char *path)
{ assert(!strcmp(path,"/doom"));return ESP_OK; }
bool platform_game_storage_game_locked(void) { return true; }
esp_err_t platform_game_storage_read_arena_wad(unsigned file,size_t offset,void *out,size_t bytes)
{
    assert(file<3);(void)offset;
    startup_storage_read(bytes);
    if (++reads==fail_read) return ESP_FAIL;
    memset(out,0xa5,bytes);return ESP_OK;
}
/* This fixture exercises ordinary block reads. Decline the optional startup
 * window and fail loudly if the VFS nevertheless routes a read through it. */
bool platform_game_storage_arena_sprite_begin(unsigned file)
{ assert(file<3); return false; }
esp_err_t platform_game_storage_arena_sprite_read(unsigned file,size_t offset,void *out,size_t bytes)
{ (void)file;(void)offset;(void)out;(void)bytes;assert(false);return ESP_ERR_INVALID_STATE; }
/* Lease teardown may close an optional scope even when none was opened. */
void platform_game_storage_arena_sprite_end(void) { }
esp_err_t platform_game_storage_get_locked_doom_snapshot(platform_game_storage_doom_title_t title,platform_game_storage_doom_snapshot_t *snapshot)
{ (void)title;(void)snapshot;assert(false);return ESP_FAIL; }
void startup_vfs_open(void)
{
    static const uint8_t marker=1;
    const platform_readonly_blob_config_t config={"/doom","freedoom2.wad",&marker,
        (size_t)PLATFORM_GAME_STORAGE_ARENA_BASE_WAD_BYTES};
    assert(platform_readonly_blob_register(&config)==ESP_OK);
    wad_fd=ops->open_p(vfs_context,"freedoom2.wad",O_RDONLY,0);assert(wad_fd>=0);
}
void startup_vfs_read(size_t bytes)
{
    uint8_t *buffer=malloc(bytes);assert(buffer);
    const off_t position=ops->lseek_p(vfs_context,wad_fd,0,SEEK_CUR);assert(position>=0);
    assert(ops->read_p(vfs_context,wad_fd,buffer,bytes)==(ssize_t)bytes);
    assert(ops->lseek_p(vfs_context,wad_fd,0,SEEK_CUR)==position+(off_t)bytes);
    for (size_t i=0;i<bytes;++i) assert(buffer[i]==0xa5);
    free(buffer);
}
void startup_vfs_read_failure(size_t bytes,unsigned fail_call)
{
    uint8_t *buffer=malloc(bytes);assert(buffer);
    const off_t position=ops->lseek_p(vfs_context,wad_fd,0,SEEK_CUR);assert(position>=0);
    reads=0;fail_read=fail_call;
    assert(ops->read_p(vfs_context,wad_fd,buffer,bytes)==-1 && errno==EIO);
    assert(ops->lseek_p(vfs_context,wad_fd,0,SEEK_CUR)==position);
    fail_read=0;free(buffer);
}
void startup_vfs_close(void)
{ assert(ops->close_p(vfs_context,wad_fd)==0);assert(platform_readonly_blob_unregister()==ESP_OK); }
