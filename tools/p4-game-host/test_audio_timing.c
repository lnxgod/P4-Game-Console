// SPDX-License-Identifier: MIT
#include "host_service.h"
#include "frame_pacer.h"
#include "p4/audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned failures;
#define CHECK(c) do {if(!(c)){fprintf(stderr,"AUDIO TIMING FAIL %d: %s\n",__LINE__,#c);++failures;}}while(0)
typedef struct {uint32_t samples,elapsed,presses,releases,updates;bool exit,fail;} source_t;
typedef struct {p4_audio_mixer_t mixer;uint32_t samples,mismatches,pumps;bool fail;} sink_t;
static int16_t pattern(uint32_t at) {return (int16_t)(1000U+at%14000U);}
static bool start(p4_game_context_t *c) {(void)c;return true;}
static bool render(p4_game_context_t *c,p4_game_surface_t *s) {(void)c;(void)s;return true;}
static void stop(p4_game_context_t *c) {(void)c;}
static p4_game_result_t update(p4_game_context_t *c,const p4_game_input_t *in,uint32_t ms)
{
    source_t *s=c->state;++s->updates;
    if(s->exit) return P4_GAME_EXIT_TO_LAUNCHER;
    if(s->fail) return P4_GAME_ERROR;
    s->elapsed+=ms;
    if(in->pressed&P4_BUTTON_A) ++s->presses;
    if(in->released&P4_BUTTON_B) ++s->releases;
    CHECK((in->held&P4_BUTTON_RIGHT)!=0U);
    unsigned remaining=ms*16U;
    while(remaining) {
        const unsigned frames=remaining>256U?256U:remaining;
        int16_t block[512];
        for(unsigned f=0U;f<frames;++f) {const int16_t v=pattern(s->samples++);block[f*2U]=v;block[f*2U+1U]=(int16_t)-v;}
        (void)p4_game_submit_pcm16_stereo(c,block,frames);remaining-=frames;
    }
    return P4_GAME_CONTINUE;
}
static const p4_game_descriptor_t game={.api_version=1,.launcher_id=1,.id="org.p4console.audio-timing-test",
    .title="Timing Test",.subtitle="Host fixture",.required_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS,
    .optional_capabilities=P4_GAME_CAP_AUDIO_STREAM,.state_bytes=sizeof(source_t),
    .start=start,.update=update,.render=render,.stop=stop};
static bool pump(void *context,uint32_t ms)
{
    sink_t *sink=context;++sink->pumps;if(sink->fail) return false;
    CHECK(ms>0U && ms<=16U);int16_t block[512];
    const unsigned frames=ms*16U;CHECK(p4_audio_mixer_render(&sink->mixer,block,frames));
    for(unsigned f=0U;f<frames;++f) {
        const int16_t v=pattern(sink->samples++);
        if(block[f*2U]!=v || block[f*2U+1U]!=(int16_t)-v) ++sink->mismatches;
    }
    return true;
}
static void setup(p4_game_instance_t *instance,source_t *source,sink_t *sink)
{
    memset(source,0,sizeof(*source));memset(sink,0,sizeof(*sink));
    const p4_game_services_t services={.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS|P4_GAME_CAP_AUDIO_STREAM,
        .audio_context=&sink->mixer,.submit_pcm16_stereo=p4_audio_mixer_service_submit_pcm16_stereo};
    CHECK(p4_game_instance_start(instance,&game,&services,source,sizeof(*source)));
}
int main(void)
{
    p4_host_pacer_t pacer = {0};
    uint64_t deadline = 0U;
    for (unsigned i=0U; i<600U; ++i) deadline=p4_host_pacer_next(&pacer,deadline);
    CHECK(deadline == UINT64_C(10000000000));
    deadline += UINT64_C(1000000000);
    CHECK(p4_host_pacer_next(&pacer, deadline) == deadline);
    CHECK(p4_host_pacer_next(&pacer, deadline) > deadline);
    static const uint32_t jitter[]={16U,17U,23U,32U,48U,19U,100U,1U,15U};
    p4_game_instance_t instance={0};source_t source;sink_t sink;
    p4_game_input_t input={.held=P4_BUTTON_RIGHT,.pressed=P4_BUTTON_A,.released=P4_BUTTON_B};
    // Reproduce the previous host's elapsed-time producer/fixed-size consumer.
    setup(&instance,&source,&sink);
    for(unsigned i=0U;i<90U;++i) {
        CHECK(p4_game_instance_update(&instance,&input,jitter[i%9U])==P4_GAME_CONTINUE);
        CHECK(pump(&sink,16U));
    }
    CHECK(sink.mixer.stream_blocks_rejected>0U && sink.mismatches>0U);
    printf("Previous timing reproduced: %u rejected blocks, %u discontinuous frames\n",
        sink.mixer.stream_blocks_rejected,sink.mismatches);
    p4_game_instance_stop(&instance);
    setup(&instance,&source,&sink);
    uint32_t total=0U;
    for(unsigned i=0U;i<900U;++i) {
        const uint32_t ms=jitter[i%9U];total+=ms;
        CHECK(p4_host_service_update(&instance,&input,ms,pump,&sink)==P4_GAME_CONTINUE);
    }
    CHECK(source.elapsed==total && source.samples==total*16U && sink.samples==source.samples);
    CHECK(source.presses==900U && source.releases==900U);
    CHECK(sink.mismatches==0U && sink.mixer.stream_blocks_rejected==0U && sink.mixer.stream_underrun_frames==0U);
    CHECK(sink.mixer.stream_queued_frames==0U && sink.mixer.clipped_samples==0U);
    const uint32_t before=source.elapsed;
    CHECK(p4_host_service_update(&instance,&input,UINT32_MAX,pump,&sink)==P4_GAME_CONTINUE);
    CHECK(source.elapsed-before==P4_GAME_MAX_FRAME_DELTA_MS);
    const uint32_t pumps=sink.pumps;source.exit=true;
    CHECK(p4_host_service_update(&instance,&input,48U,pump,&sink)==P4_GAME_EXIT_TO_LAUNCHER && sink.pumps==pumps);
    source.exit=false;source.fail=true;
    CHECK(p4_host_service_update(&instance,&input,48U,pump,&sink)==P4_GAME_ERROR);
    source.fail=false;sink.fail=true;
    CHECK(p4_host_service_update(&instance,&input,48U,pump,&sink)==P4_GAME_ERROR);
    p4_game_instance_stop(&instance);
    printf("Fixed timing: exact PCM continuity through 900 jittered updates, input edges once, bounded catch-up and propagated errors; %u failures\n",failures);
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
