// SPDX-License-Identifier: MIT
/* Exercise the production Console OS VFS, replacing only OS/SD services. */
#include "platform/readonly_blob.h"
#include "platform/game_storage.h"
#include "esp_vfs.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
static const esp_vfs_fs_ops_t *ops;
static void *ctx;
static bool locked=true, fail_read;
static unsigned last_file;
static const uint8_t doom[16]="IWAD-doom-only";
esp_err_t esp_vfs_register_fs(const char *path,const esp_vfs_fs_ops_t *o,int flags,void *c)
{ assert(!strcmp(path,"/doom") && (flags&ESP_VFS_FLAG_READONLY_FS)); ops=o; ctx=c; return ESP_OK; }
esp_err_t esp_vfs_unregister_fs(const char *path) { assert(!strcmp(path,"/doom")); return ESP_OK; }
bool platform_game_storage_game_locked(void) { return locked; }
esp_err_t platform_game_storage_read_arena_wad(unsigned file,size_t offset,void *out,size_t bytes)
{ (void)offset; assert(file<3); last_file=file; if(fail_read) return ESP_FAIL; memset(out,(int)(0xa0U+file),bytes); return ESP_OK; }
esp_err_t platform_game_storage_get_locked_doom_snapshot(platform_game_storage_doom_title_t title,platform_game_storage_doom_snapshot_t *s)
{ assert(title==PLATFORM_GAME_STORAGE_DOOM_TITLE_DOOM); s->wad_data=doom; s->wad_size_bytes=sizeof(doom); return ESP_OK; }
int main(void)
{
    platform_readonly_blob_config_t config={"/doom","freedoom2.wad",doom,(size_t)PLATFORM_GAME_STORAGE_FREEDOOM2_WAD_BYTES};
    locked=false; assert(platform_readonly_blob_register(&config)==ESP_ERR_INVALID_STATE); locked=true;
    fail_read=true; assert(platform_readonly_blob_register(&config)==ESP_FAIL); fail_read=false;
    assert(platform_readonly_blob_register(&config)==ESP_OK && last_file==2);
    const char *names[]={"/freedoom2.wad","/purehell.wad","/dwango5.wad"};
    const off_t sizes[]={PLATFORM_GAME_STORAGE_FREEDOOM2_WAD_BYTES,PLATFORM_GAME_STORAGE_PUREHELL_WAD_BYTES,PLATFORM_GAME_STORAGE_DWANGO5_WAD_BYTES};
    int fd[3]; uint8_t data[8];
    for(unsigned i=0;i<3;++i) {
        assert(ops->open_p(ctx,names[i],O_WRONLY,0)==-1 && errno==EROFS);
        fd[i]=ops->open_p(ctx,names[i],O_RDONLY,0); assert(fd[i]>=0);
        struct stat st; assert(ops->fstat_p(ctx,fd[i],&st)==0 && st.st_size==sizes[i]);
    }
    /* Interleave open descriptors to catch accidental base/PWAD aliasing. */
    for(unsigned i=0;i<9;++i) {
        const unsigned file=(i+1U)%3U;
        assert(ops->read_p(ctx,fd[file],data,sizeof(data))==sizeof(data));
        assert(last_file==file && data[0]==0xa0U+file);
    }
    assert(ops->pread_p(ctx,fd[2],data,sizeof(data),sizes[2]-3)==3 && last_file==2);
    assert(ops->pread_p(ctx,fd[2],data,sizeof(data),sizes[2])==0);
    assert(ops->lseek_p(ctx,fd[1],-2,SEEK_END)==sizes[1]-2);
    assert(ops->read_p(ctx,fd[1],data,sizeof(data))==2 && last_file==1);
    fail_read=true; assert(ops->read_p(ctx,fd[0],data,1)==-1 && errno==EIO); fail_read=false;
    assert(platform_readonly_blob_unregister()==ESP_ERR_INVALID_STATE);
    for(unsigned i=0;i<3;++i) assert(ops->close_p(ctx,fd[i])==0);
    assert(platform_readonly_blob_unregister()==ESP_OK);
    config.file_name="doom1.wad"; config.size_bytes=(size_t)PLATFORM_GAME_STORAGE_DOOM_WAD_BYTES;
    assert(platform_readonly_blob_register(&config)==ESP_OK);
    assert(ops->open_p(ctx,"purehell.wad",O_RDONLY,0)==-1 && errno==ENOENT);
    int normal=ops->open_p(ctx,"doom1.wad",O_RDONLY,0); assert(normal>=0);
    assert(ops->read_p(ctx,normal,data,4)==4 && !memcmp(data,"IWAD",4));
    assert(ops->close_p(ctx,normal)==0 && platform_readonly_blob_unregister()==ESP_OK);
    puts("arena VFS: three isolated read-only verified streams, error propagation, EOF, and separate ordinary Doom passed");
    return 0;
}
