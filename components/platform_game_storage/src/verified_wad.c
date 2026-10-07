// SPDX-License-Identifier: MIT
#include "verified_wad.h"
#include <string.h>
#include <stdio.h>

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}

bool p4_verified_wad_validate(p4_verified_reader_t *r, p4_wad_kind_t kind)
{
    const bool pure_hades=kind==P4_WAD_PURE_HADES, dwango=kind==P4_WAD_DWANGO5;
    if (kind>P4_WAD_DWANGO5) return false;
    static const char names[11][9] = {
        "MAP01", "THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS",
        "SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP"
    };
    static const char extras[16][9] = {
        "D_RUNNIN", "D_STALKS", "D_COUNTD", "D_BETWEE", "D_DOOM",
        "D_DM2TTL", "D_DM2INT", "DEHACKED", "UMAPINFO", "CWILV00",
        "CWILV01", "CWILV02", "CWILV03", "CWILV04", "M_DOOM", "TITLEPIC"
    };
    static const size_t record_bytes[11] = {0,10,14,30,4,12,4,28,26,0,2};
    uint8_t header[12];
    if (!r || r->size>64U*1024U*1024U ||
        !p4_verified_read(r,0,header,sizeof(header)) ||
        memcmp(header,kind!=P4_WAD_IWAD?"PWAD":"IWAD",4)!=0) return false;
    const size_t count=u32(header+4), directory=u32(header+8);
    if (!count || count>8192 || directory<12 || directory>r->size ||
        count>(r->size-directory)/16U || (pure_hades && count!=71) || (dwango && count!=317)) return false;
    for (size_t i=0; i<count; ++i) {
        uint8_t entry[16];
        if (!p4_verified_read(r,directory+i*16U,entry,sizeof(entry))) return false;
        const size_t offset=u32(entry), size=u32(entry+4);
        if (offset>r->size || size>r->size-offset) return false;
        if (!pure_hades && !dwango) continue;
        if (dwango && i>=264) continue;
        char expected[9]={0};
        if (dwango || i<55) {
            if (i%11U==0) (void)snprintf(expected,sizeof(expected),"MAP%02u",(unsigned)(i/11U+1U));
            else memcpy(expected,names[i%11U],9);
        } else memcpy(expected,extras[i-55U],9);
        if (strncmp((const char *)entry+8,expected,8)!=0) return false;
        if (pure_hades && i>=55) {
            if (i>=62) continue;
            uint8_t midi[4];
            if (size<14 || !p4_verified_read(r,offset,midi,4) ||
                memcmp(midi,"MThd",4)!=0) return false;
            continue;
        }
        const size_t part=i%11U, stride=record_bytes[part];
        if (stride && (!size || size%stride)) return false;
        if (part==1) {
            unsigned starts=0;
            /* The engine retains the first ten starts; extra DWANGO starts are legal. */
            if (size/10U>4096) return false;
            for (size_t t=0; t<size; t+=10U) {
                uint8_t thing[10];
                if (!p4_verified_read(r,offset+t,thing,sizeof(thing))) return false;
                if (thing[6]==11 && thing[7]==0) ++starts;
            }
            if (starts<4 || (pure_hades && starts>10)) return false;
        }
    }
    return true;
}
