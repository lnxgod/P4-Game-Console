#!/usr/bin/env python3
"""Prepare an ignored, source-pinned Wacky Wheels racing-core experiment."""
from pathlib import Path
import hashlib, shutil, subprocess
ROOT = Path(__file__).resolve().parents[2]
UPSTREAM = ROOT / '.tools/wacky-wheels-upstream'
OUT = ROOT / '.tools/wacky-p4-port'
REV = '7dd510096c58ee84c36970a11c4f57b4a9c2a4cf'
CORE = 'ai duck dynamic_object finish fixed_math font hud lap minimap pcx perspective physics race racer render_queue renderer sprite track water weapon world_object victory music intro'.split()
if subprocess.check_output(['git','-C',str(UPSTREAM),'rev-parse','HEAD'],text=True).strip() != REV:
    raise SystemExit('Upstream revision mismatch')
subprocess.run(['git','-C',str(UPSTREAM),'diff','--quiet',REV,'--','src'],check=True)
DATA = ROOT / '.tools/wacky-shareware/WACKY.DAT'
if hashlib.sha256(DATA.read_bytes()).hexdigest() != 'ae36b4204f1b44fbdb26294a45495e46616feffcb18e67d48de9324d76bed85f':
    raise SystemExit('Shareware data hash mismatch')
OUT.mkdir(exist_ok=True)
for p in (UPSTREAM/'src').glob('ww_*.h'):
    shutil.copyfile(p, OUT/p.name)
for name in CORE:
    shutil.copyfile(UPSTREAM/'src'/f'ww_{name}.c', OUT/f'ww_{name}.c')
# Keep upstream intact. Import portable game logic and adapt OS boundaries.
def change(name, old, new):
    p=OUT/name;s=p.read_text();assert old in s,(name,old);p.write_text(s.replace(old,new))
# Race rendering uses only page zero. Borrow the host's RGB565 surface as
# indexed scratch and expand backwards, removing 512 KiB of desktop buffers.
change('ww_display.h','uint8_t pages[WW_DISPLAY_PAGES][WW_SCREEN_WIDTH * WW_SCREEN_HEIGHT];\n    uint32_t rgba[WW_SCREEN_WIDTH * WW_SCREEN_HEIGHT];','uint8_t *pixels;\n    bool advance_simulation;')
# Avoid the two 57/65 KiB automatic temporaries, which overflow P4 task stacks.
for name,typ,arg in [('ww_race.c','WwRace','race'),('ww_track.c','WwTrack','track')]:
    p=OUT/name;s=p.read_text();s=s.replace(f'{typ} loaded;',f'{typ} *loaded = {arg};')
    s=s.replace('&loaded.', '&loaded->').replace('loaded.', 'loaded->')
    s=s.replace('&loaded, 0, sizeof(loaded)', 'loaded, 0, sizeof(*loaded)')
    s=s.replace(f'*{arg} = loaded;', '/* Initialized directly in caller-owned state. */')
    s=s.replace('(&loaded);','(loaded);').replace('(&loaded,', '(loaded,');p.write_text(s)
# Reuse the upstream byte blitters; remove SDL window management/presentation.
s=(UPSTREAM/'src/ww_display.c').read_text()
s=s[s.index('void ww_display_set_palette'):s.index('bool ww_display_present')]
s=s.replace('display->pages[display->draw_page]','display->pixels')
a=s.index('void ww_display_copy_page');b=s.index('/* Chunky equivalents',a)
s=s[:a]+s[b:]
(OUT/'ww_display.c').write_text('#include "ww_display.h"\n#include <string.h>\n'+s+'\nbool ww_display_present(WwDisplay *d) { return d && d->pixels; }\n')
# Archive storage comes from the immutable OS resource payload, never a file.
s=(UPSTREAM/'src/ww_archive.c').read_text();s=s[s.index('const WwArchiveEntry *ww_archive_find'):]
(OUT/'ww_archive.c').write_text('#include "ww_archive.h"\n#define WW_ARCHIVE_PAYLOAD_SKIP_BYTES 2\n'+s)
# Portable helpers, with logs remaining visible in the host probe.
s=(UPSTREAM/'src/ww_common.c').read_text();(OUT/'ww_common.c').write_text(s)
for name in ['SDL.h','port.c','port_support.c','port_support.h','portable_libc.c','network.c','network.h','sound.c','sound.h','front.c','front.h','controls.c','controls.h']:
    shutil.copyfile(ROOT/'scripts/wacky'/name, OUT/name)

# Extra interpolated draws must not advance externally allocated world sprites.
change('ww_race.c', '!ww_world_object_update(&race->track, &race->world_sprites)',
       '(display->advance_simulation && !ww_world_object_update(&race->track, &race->world_sprites))')
# Rendering needs the color bank only; avoid physics/mask/SIN reads per pixel.
p=OUT/'ww_renderer.c';s=p.read_text()
a=s.index('    WwTrackSurfaceSample sample;');b=s.index('\n}',a)
s=s[:a]+'    unsigned tx = x >> 5, ty = y >> 5;\n    if (!track || !track->tile_pixels || tx >= 128 || ty >= 128) return false;\n    unsigned tile = 0;\n    if (tx >= 32 && tx < 96 && ty >= 32 && ty < 96)\n        tile = track->tile_index[(ty - 32) * 64 + tx - 32];\n    if (tile >= WW_TRACK_TILES_PER_BANK) return false;\n    *color = track->tile_pixels[(size_t)tile * 1024 + (y & 31) * 32 + (x & 31)];\n    return true;'+s[b:];p.write_text(s)

# Keep embedded code independent of file I/O, newlib reentrancy and locale.
p=OUT/'ww_common.c';s=p.read_text();a=s.index('static void ww_vlog')
s=s[:a]+'#ifndef WW_P4_EMBEDDED\n'+s[a:]+'\n#else\nvoid ww_log(const char *f, ...) { (void)f; }\nvoid ww_error(const char *f, ...) { (void)f; }\n#endif\n';p.write_text(s)
p=OUT/'ww_track.c';s=p.read_text().replace('    errno = 0;\n','').replace('errno != 0 || end == line','end == line');p.write_text(s)

p=OUT/'ww_common.c';s=p.read_text().replace('#include <ctype.h>', 'int ww_p4_toupper(int);').replace('toupper(', 'ww_p4_toupper(').replace('ww_p4_ww_p4_toupper(', 'ww_p4_toupper(');p.write_text(s)

print(OUT)

# The victory UI reads normalized input, with no SDL event ownership.
p=OUT/'ww_input.c';p.write_text('\n#include "ww_input.h"\nbool ww_input_down(const WwInput *i,WwDosScanCode c){return i && (unsigned)c<128 && i->keys[c];}\nbool ww_input_pressed(const WwInput *i,WwDosScanCode c){return i && (unsigned)c<128 && i->pressed[c];}\n')

# The original animated intro uses the same bounded PCM player as gameplay.
shutil.copyfile(ROOT/'scripts/wacky/intro_audio.h',OUT/'ww_audio.h')
# Time trial has no opponents. Inactive racers must not advance paths/laps.
p=OUT/'ww_race.c';s=p.read_text().replace('for (racer_index = 1; racer_index < WW_RACER_COUNT; ++racer_index) {', 'for (racer_index = 1; racer_index < WW_RACER_COUNT; ++racer_index) {\n            if (!race->racers[racer_index].active) continue;');p.write_text(s)
