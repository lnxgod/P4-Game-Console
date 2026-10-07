// SPDX-License-Identifier: MIT
#include "trusted_wad_table.h"
#include "verified_reader.h"
#include <string.h>
bool p4_trusted_wad_table_validate(const p4_trusted_wad_table_t *table,
    unsigned file_index, const char *symbol, const char *path, size_t size,
    const char *whole_sha256_hex, const uint8_t content_sha256[32],
    bool (*sha256)(const void *,size_t,uint8_t[32]), p4_trusted_wad_view_t *out)
{
    if (!out) return false;
    *out=(p4_trusted_wad_view_t){0};
    if (!table || !symbol || !path || !whole_sha256_hex || !content_sha256 ||
        !sha256 || !table->symbol || !table->path || !table->digests ||
        !size || size>64U*1024U*1024U || file_index>=3U || table->schema!=1U ||
        table->file_index!=file_index || strcmp(table->symbol,symbol)!=0 ||
        strcmp(table->path,path)!=0 || table->size!=size ||
        table->block_bytes!=P4_VERIFIED_BLOCK_BYTES ||
        memcmp(table->content_sha256,content_sha256,32)!=0 ||
        strlen(whole_sha256_hex)!=64U) return false;
    const size_t blocks=size/P4_VERIFIED_BLOCK_BYTES+
        (size%P4_VERIFIED_BLOCK_BYTES!=0U?1U:0U);
    if (blocks>SIZE_MAX/P4_VERIFIED_DIGEST_BYTES ||
        table->block_count!=blocks ||
        table->digest_bytes!=blocks*P4_VERIFIED_DIGEST_BYTES) return false;
    static const char hex[]="0123456789abcdef";
    for (size_t i=0;i<32U;++i) {
        if (hex[table->whole_sha256[i]>>4]!=whole_sha256_hex[i*2U] ||
            hex[table->whole_sha256[i]&15U]!=whole_sha256_hex[i*2U+1U]) return false;
    }
    uint8_t actual[32];
    /* Bounded to 512 KiB by the 64 MiB source limit. This detects bad table
     * pairing/corruption; the generator and artifact binding supply trust. */
    if (!sha256(table->digests,table->digest_bytes,actual) ||
        memcmp(actual,table->table_sha256,sizeof(actual))!=0) return false;
    *out=(p4_trusted_wad_view_t){.digests=table->digests,
                               .digest_bytes=table->digest_bytes};
    return true;
}
