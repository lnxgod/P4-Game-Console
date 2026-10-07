// SPDX-License-Identifier: MIT
/* Exercise the production Console OS VFS, replacing only OS/SD services. */
#include "platform/readonly_blob.h"
#include "platform/readonly_blob_loading.h"
#include "platform/game_storage.h"
#include "esp_vfs.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static const esp_vfs_fs_ops_t *ops;
static void *ctx;
static bool locked=true, fail_read;
static unsigned last_file;
static bool sprite_supported, loading;
static unsigned sprite_begins, sprite_ends, diagnostic_logs, progress_calls;
static int64_t now_us;
static char last_diagnostic[512];
int64_t esp_timer_get_time(void) { return now_us; }
bool platform_readonly_blob_loading_progress(void)
{ ++progress_calls; now_us += 17; return loading; }
void vfs_test_log(const char *format, ...)
{
    if (strncmp(format,"P4_SPRITE_VFS ",14)!=0) return;
    va_list args; va_start(args,format);
    (void)vsnprintf(last_diagnostic,sizeof(last_diagnostic),format,args);
    va_end(args); ++diagnostic_logs;
}
static int call_ioctl(int fd, int command, ...)
{
    va_list args; va_start(args,command);
    const int result=ops->ioctl_p(ctx,fd,command,args);
    va_end(args); return result;
}
static const uint8_t doom[16]="IWAD-doom-only";
esp_err_t esp_vfs_register_fs(const char *path,const esp_vfs_fs_ops_t *o,int flags,void *c)
{ assert(!strcmp(path,"/doom") && (flags&ESP_VFS_FLAG_READONLY_FS)); ops=o; ctx=c; return ESP_OK; }
esp_err_t esp_vfs_unregister_fs(const char *path) { assert(!strcmp(path,"/doom")); return ESP_OK; }
bool platform_game_storage_game_locked(void) { return locked; }
esp_err_t platform_game_storage_read_arena_wad(unsigned file,size_t offset,void *out,size_t bytes)
{ (void)offset; assert(file<3); last_file=file; if(fail_read) return ESP_FAIL; memset(out,(int)(0xa0U+file),bytes); return ESP_OK; }
esp_err_t platform_game_storage_get_locked_doom_snapshot(platform_game_storage_doom_title_t title,platform_game_storage_doom_snapshot_t *s)
{ assert(title==PLATFORM_GAME_STORAGE_DOOM_TITLE_DOOM); s->wad_data=doom; s->wad_size_bytes=sizeof(doom); return ESP_OK; }
bool platform_game_storage_arena_sprite_begin(unsigned file)
{ assert(file<3); if (sprite_supported) ++sprite_begins; return sprite_supported; }
esp_err_t platform_game_storage_arena_sprite_read(unsigned file,size_t offset,void *out,size_t bytes)
{ now_us += 31; return platform_game_storage_read_arena_wad(file,offset,out,bytes); }
void platform_game_storage_arena_sprite_end(void) { ++sprite_ends; }
int main(void)
{
    assert(!platform_readonly_blob_loading_progress());
    platform_readonly_blob_config_t config={"/doom","freedoom2.wad",doom,(size_t)PLATFORM_GAME_STORAGE_FREEDOOM2_WAD_BYTES};
    locked=false; assert(platform_readonly_blob_register(&config)==ESP_ERR_INVALID_STATE); locked=true;
    fail_read=true; assert(platform_readonly_blob_register(&config)==ESP_FAIL); fail_read=false;
    assert(platform_readonly_blob_register(&config)==ESP_OK && last_file==2);
    const char *names[]={"/freedoom2.wad","/purehades.wad","/dwango5.wad"};
    const off_t sizes[]={PLATFORM_GAME_STORAGE_FREEDOOM2_WAD_BYTES,PLATFORM_GAME_STORAGE_PUREHADES_WAD_BYTES,PLATFORM_GAME_STORAGE_DWANGO5_WAD_BYTES};
    int fd[3]; uint8_t data[8];
    for(unsigned i=0;i<3;++i) {
        assert(ops->open_p(ctx,names[i],O_WRONLY,0)==-1 && errno==EROFS);
        fd[i]=ops->open_p(ctx,names[i],O_RDONLY,0); assert(fd[i]>=0);
        struct stat st; assert(ops->fstat_p(ctx,fd[i],&st)==0 && st.st_size==sizes[i]);
        /* Positive block metadata lets pinned Newlib use a block-sized
         * buffer and aligned seeks, instead of its 128-byte fallback.
         * This metadata test is not a target-libc performance proof. */
        assert(st.st_blksize > 0 && st.st_blksize <= 4096);
        const off_t block = (off_t)st.st_blksize;
        assert((block & (block - 1)) == 0);
        const off_t target = 1000003;
        const off_t aligned = target & -block;
        assert(aligned > 0 && target - aligned < block);
        struct stat named;
        assert(ops->dir->stat_p(ctx,names[i],&named)==0);
        assert(named.st_blksize == st.st_blksize && named.st_size == st.st_size);
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
    /* Only successful sprite scopes gather callback timings. A crossing read
     * services loading before and after each verified block; ending a scope
     * emits one aggregate, while a repeated end and ordinary reads emit none. */
    const unsigned logs_before=diagnostic_logs;
    assert(call_ioctl(fd[0],0x50345348,1)==-1 && errno==ENOMEM);
    assert(diagnostic_logs==logs_before);
    sprite_supported=true; loading=true;
    assert(call_ioctl(fd[0],0x50345348,1)==0 && sprite_begins==1);
    assert(call_ioctl(fd[0],0x50345348,1)==0 && sprite_begins==1);
    assert(call_ioctl(fd[1],0x50345348,1)==-1 && errno==EBUSY);
    assert(ops->lseek_p(ctx,fd[0],4093,SEEK_SET)==4093);
    const unsigned progress_before=progress_calls;
    assert(ops->read_p(ctx,fd[0],data,sizeof(data))==sizeof(data));
    assert(progress_calls-progress_before==3);
    assert(call_ioctl(fd[0],0x50345348,0)==0);
    assert(diagnostic_logs==logs_before+1);
    assert(strstr(last_diagnostic,"file=0 elapsed_us=113 reads=1 bytes=8 ")!=NULL);
    assert(strstr(last_diagnostic,"progress_calls=3 progress_us=51 progress_max_us=17")!=NULL);
    assert(call_ioctl(fd[0],0x50345348,0)==0 && diagnostic_logs==logs_before+1);
    assert(ops->read_p(ctx,fd[0],data,sizeof(data))==sizeof(data));
    assert(diagnostic_logs==logs_before+1);
    /* A new scope resets counters, retains failed-read callback cost and
     * is closed automatically with its descriptor. */
    assert(call_ioctl(fd[1],0x50345348,1)==0 && sprite_begins==2);
    assert(ops->lseek_p(ctx,fd[1],0,SEEK_SET)==0);
    fail_read=true;
    assert(ops->read_p(ctx,fd[1],data,1)==-1 && errno==EIO);
    fail_read=false;
    const unsigned ends_before=sprite_ends;
    assert(ops->close_p(ctx,fd[1])==0 && sprite_ends==ends_before+1);
    assert(diagnostic_logs==logs_before+2);
    assert(strstr(last_diagnostic,"file=1 elapsed_us=65 reads=1 bytes=0 ")!=NULL);
    assert(strstr(last_diagnostic,"progress_calls=2 progress_us=34 progress_max_us=17")!=NULL);
    loading=false;
    assert(platform_readonly_blob_unregister()==ESP_ERR_INVALID_STATE);
    for(unsigned i=0;i<3;++i) if (i!=1) assert(ops->close_p(ctx,fd[i])==0);
    assert(platform_readonly_blob_unregister()==ESP_OK);
    config.file_name="doom1.wad"; config.size_bytes=(size_t)PLATFORM_GAME_STORAGE_DOOM_WAD_BYTES;
    assert(platform_readonly_blob_register(&config)==ESP_OK);
    assert(ops->open_p(ctx,"purehades.wad",O_RDONLY,0)==-1 && errno==ENOENT);
    int normal=ops->open_p(ctx,"doom1.wad",O_RDONLY,0); assert(normal>=0);
    assert(ops->read_p(ctx,normal,data,4)==4 && !memcmp(data,"IWAD",4));
    assert(ops->close_p(ctx,normal)==0 && platform_readonly_blob_unregister()==ESP_OK);
    puts("arena VFS: three isolated read-only verified streams, error propagation, EOF, and separate ordinary Doom passed");
    return 0;
}
