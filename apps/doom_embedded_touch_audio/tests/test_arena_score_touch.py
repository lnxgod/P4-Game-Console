#!/usr/bin/env python3
"""Test the actual Arena touch hook at Tab5 and legacy viewport geometries."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / 'apps/doom_embedded_touch_audio/main/doom_embedded_touch_audio_main.c'

def function(source, name):
    start = source.index(f'static void {name}(')
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "doom_touch/input.h"
#include "doom_arena_ui.h"
static bool last_pressed;
void p4_doom_arena_score_touch(bool pressed) { last_pressed=pressed; }
/* TOUCH_HOOK */
static doom_touch_contact_t logical(unsigned x,unsigned y)
{
    return (doom_touch_contact_t){
        (uint16_t)(DOOM_TOUCH_VIEWPORT_LEFT +
            (x * DOOM_TOUCH_VIEWPORT_WIDTH + 319U) / 320U),
        (uint16_t)(DOOM_TOUCH_VIEWPORT_TOP +
            (y * DOOM_TOUCH_VIEWPORT_HEIGHT + 199U) / 200U)};
}
static void check(unsigned x,unsigned y,bool expected)
{
    doom_touch_frame_t frame; doom_touch_frame_init(&frame);
    frame.contact_count=1; frame.contacts[0]=logical(x,y);
    service_arena_score_touch(&frame); assert(last_pressed==expected);
    if(expected) { /* SCORE does not also move, fire, change weapons or open menu. */
        doom_touch_input_t input; doom_touch_input_init(&input);
        assert(doom_touch_input_update(&input,&frame)); assert(input.active_actions==0);
    }
}
int main(void)
{
    for(unsigned y=0;y<200;++y)
        for(unsigned x=0;x<320;++x)
            check(x,y,x>=P4_DOOM_SCORE_LEFT &&
                x<P4_DOOM_SCORE_LEFT+P4_DOOM_SCORE_WIDTH &&
                y>=P4_DOOM_SCORE_TOP && y<P4_DOOM_SCORE_TOP+P4_DOOM_SCORE_HEIGHT);
    doom_touch_frame_t frame; doom_touch_frame_init(&frame);
    service_arena_score_touch(&frame); assert(!last_pressed);
    frame.contact_count=2;
    frame.contacts[0]=logical(289,151); frame.contacts[1]=logical(110,20);
    service_arena_score_touch(&frame); assert(last_pressed);
    doom_touch_input_t input; doom_touch_input_init(&input);
    assert(doom_touch_input_update(&input,&frame));
    assert(input.active_actions==((1U<<DOOM_TOUCH_ACTION_FIRE)|(1U<<DOOM_TOUCH_ACTION_MENU_ACCEPT)));
    frame.contact_count=1; frame.contacts[0]=(doom_touch_contact_t){0,0};
    service_arena_score_touch(&frame); assert(!last_pressed);
    frame.contacts[0]=(doom_touch_contact_t){DOOM_TOUCH_SCREEN_WIDTH-1,DOOM_TOUCH_SCREEN_HEIGHT-1};
    service_arena_score_touch(&frame); assert(!last_pressed);
    return 0;
}
'''

def main():
    source = SOURCE.read_text()
    hook = function(source, 'service_arena_score_touch')
    # This hook's input is the successfully converted, bounded touch snapshot.
    service = function(source, 'service_touch')
    assert service.index('if (result != ESP_OK || !converted)') < service.index('service_arena_score_touch(&touch_frame)')
    assert 'p4_doom_arena_score_touch(false)' in function(source, 'neutralize_touch_input')
    with tempfile.TemporaryDirectory(prefix='p4-arena-touch-') as directory:
        path = Path(directory)
        (path/'test.c').write_text(HARNESS.replace('/* TOUCH_HOOK */',hook))
        for width,height in ((1280,720),(1024,600),(800,480)):
            binary=path/f'test-{width}'
            subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra',
                '-Wconversion','-Wshadow','-Werror','-fsanitize=address,undefined',
                f'-DDOOM_TOUCH_SCREEN_WIDTH={width}U',f'-DDOOM_TOUCH_SCREEN_HEIGHT={height}U',
                '-I',str(ROOT/'components/doom_touch_input/include'),
                '-I',str(ROOT/'apps/console_os/main'),str(path/'test.c'),
                str(ROOT/'components/doom_touch_input/src/input.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
    print('Arena SCORE touch: all 320x200 hit points, 3 viewports, multitouch, neutral passed')

if __name__=='__main__':
    main()
