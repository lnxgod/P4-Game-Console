/* SPDX-License-Identifier: MIT */
#include "p4/game_package.h"
#include "p4/multiplayer_registry.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    bool package = argc == 3 && strcmp(argv[1], "--package") == 0;
    if (argc != 2 && !package) return 2;
    FILE *f=fopen(argv[package?2:1],"rb");if(!f)return 2;
    if(fseek(f,0,SEEK_END))return 2;
    long n=ftell(f);if(n<=0 || n>512*1024 || fseek(f,0,SEEK_SET))return 2;
    unsigned char *b=malloc((size_t)n);if(!b)return 2;
    if(fread(b,1,(size_t)n,f)!=(size_t)n)return 2;
    fclose(f);
    if (package) {
        p4_game_package_info_t info;
        p4_mp_game_registry_t registry;
        p4_game_package_result_t parsed=p4_game_package_parse(b,(size_t)n,&info);
        if(parsed!=P4_GAME_PACKAGE_VALID || !info.multiplayer_profile_declared ||
           !(info.optional_capabilities & P4_GAME_CAP_MULTIPLAYER_SESSION) ||
           info.multiplayer_profile.message_bytes!=64 || info.multiplayer_profile.protocol!=2 ||
           info.multiplayer_profile.tick_rate_hz!=12 || info.multiplayer_profile.max_players!=4 ||
           !(info.optional_capabilities & P4_GAME_CAP_AUDIO_STREAM) || !(info.optional_capabilities & P4_GAME_CAP_SAVE)) { free(b); return 1; }
        p4_mp_game_registry_init(&registry);
        p4_mp_registration_result_t result=p4_mp_game_registry_register_package(&registry,&info,4);
        bool ok=result==P4_MP_REGISTRATION_ACCEPTED && p4_mp_game_registry_find_launcher(&registry,9001)!=NULL;
        printf("Console OS Host/Join registration: %s\n",p4_mp_registration_result_name(result));
        free(b);return ok?0:1;
    }
    p4_game_package_result_t r=p4_game_package_validate_elf(b,(size_t)n);
    printf("Console OS ELF validator: %s\n",p4_game_package_result_name(r));
    free(b);return r==P4_GAME_PACKAGE_VALID ? 0:1;
}
