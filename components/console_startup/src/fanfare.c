// SPDX-License-Identifier: MIT
#include "console/startup.h"

/* Original five-hit treasure fanfare: four rising, harmonized strikes and
 * a sustained D-major finish. Warm synth harmonics, not single bell notes.
 * Fixed-point synthesis is identical on host and board. No sampled game audio. */
typedef struct {
    unsigned start_ms, duration_ms, release_ms;
    uint32_t step;
    int32_t gain;
} note_t;
#define PHASE_STEP(millihz) ((uint32_t)(UINT64_C(4294967296) * (millihz) / \
    (CONSOLE_STARTUP_SAMPLE_RATE * UINT64_C(1000))))
static const note_t notes[] = {
    /* Da: D major. Three voices strike together on every beat. */
    {  0, 130, 24, PHASE_STEP(293665), 3900},
    {  0, 130, 24, PHASE_STEP(369994), 3400},
    {  0, 130, 24, PHASE_STEP(440000), 3100},
    /* Na: E minor. */
    {140, 130, 24, PHASE_STEP(329628), 3900},
    {140, 130, 24, PHASE_STEP(391995), 3400},
    {140, 130, 24, PHASE_STEP(493883), 3100},
    /* Na: F-sharp minor. */
    {280, 130, 24, PHASE_STEP(369994), 3900},
    {280, 130, 24, PHASE_STEP(440000), 3400},
    {280, 130, 24, PHASE_STEP(554365), 3100},
    /* Na: G major. */
    {420, 130, 24, PHASE_STEP(391995), 3900},
    {420, 130, 24, PHASE_STEP(493883), 3400},
    {420, 130, 24, PHASE_STEP(587330), 3100},
    /* Naa: a broad D-major voicing, with a low root and octave lift. */
    {560, 440, 200, PHASE_STEP(146832), 3200},
    {560, 440, 200, PHASE_STEP(293665), 2400},
    {560, 440, 200, PHASE_STEP(440000), 2800},
    {560, 440, 200, PHASE_STEP(587330), 3000},
    {560, 440, 200, PHASE_STEP(739989), 2600},
};
static const int16_t sine[256] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739, 9512, 10278, 11039, 11793,
    12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530, 18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
    32767, 32757, 32728, 32678, 32609, 32521, 32412, 32285, 32137, 31971, 31785, 31580, 31356, 31113, 30852, 30571,
    30273, 29956, 29621, 29268, 28898, 28510, 28105, 27683, 27245, 26790, 26319, 25832, 25329, 24811, 24279, 23731,
    23170, 22594, 22005, 21403, 20787, 20159, 19519, 18868, 18204, 17530, 16846, 16151, 15446, 14732, 14010, 13279,
    12539, 11793, 11039, 10278, 9512, 8739, 7962, 7179, 6393, 5602, 4808, 4011, 3212, 2410, 1608, 804,
    0, -804, -1608, -2410, -3212, -4011, -4808, -5602, -6393, -7179, -7962, -8739, -9512, -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530, -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
    -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790, -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
    -30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971, -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
    -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285, -32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
    -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683, -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
    -23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868, -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
    -12539, -11793, -11039, -10278, -9512, -8739, -7962, -7179, -6393, -5602, -4808, -4011, -3212, -2410, -1608, -804,
};
static int32_t wave(uint32_t phase)
{
    const unsigned index = phase >> 24U;
    const int32_t fraction = (int32_t)((phase >> 16U) & 255U);
    const int32_t a = sine[index];
    const int32_t b = sine[(index + 1U) & 255U];
    return a + (b - a) * fraction / 256;
}
size_t console_startup_audio_frames(void)
{
    return CONSOLE_STARTUP_SAMPLE_RATE; /* 1000 ms including the fading tail. */
}
const char *console_startup_audio_status(size_t frame)
{
    return frame < console_startup_audio_frames()
        ? "Starting GameChangersAI OS..." : "Ready";
}
bool console_startup_audio_render(int16_t *out, size_t first, size_t count)
{
    const size_t total = console_startup_audio_frames();
    if (!out || first > total || count > total - first) return false;
    for (size_t i = 0; i < count; ++i) {
        const size_t frame = first + i;
        int32_t mixed = 0;
        for (size_t j = 0; j < sizeof(notes) / sizeof(notes[0]); ++j) {
            const note_t *note = &notes[j];
            const size_t start = note->start_ms * (CONSOLE_STARTUP_SAMPLE_RATE / 1000U);
            const size_t length = note->duration_ms * (CONSOLE_STARTUP_SAMPLE_RATE / 1000U);
            if (frame < start || frame - start >= length) continue;
            const size_t n = frame - start;
            /* Eight-ms attack, quick decay to a held body, soft release.
             * The short gaps articulate all five hits without clicks. */
            const size_t attack = 128U;
            const size_t decay = 640U;
            const size_t release = note->release_ms * (CONSOLE_STARTUP_SAMPLE_RATE / 1000U);
            int32_t envelope = 24576;
            if (n < attack) {
                envelope = (int32_t)(n * 32767U / attack);
            } else if (n - attack < decay) {
                envelope = 32767 - (int32_t)((n - attack) * 8191U / decay);
            }
            const size_t remaining = length - 1U - n;
            if (remaining < release) {
                envelope = (int32_t)((int64_t)envelope * (int64_t)remaining / (int64_t)release);
            }
            const uint32_t phase = (uint32_t)((uint64_t)n * note->step);
            const int32_t voice = (6 * wave(phase) + 3 * wave(phase * 2U) +
                2 * wave(phase * 3U) + wave(phase * 4U)) / 12;
            const int32_t level = note->gain * envelope / 32767;
            mixed += voice * level / 32767;
        }
        /* Fixed normalization retains headroom when the closing chord enters. */
        const int16_t sample = (int16_t)(mixed * 3 / 4);
        out[i * 2U] = sample;
        out[i * 2U + 1U] = sample;
    }
    return true;
}
