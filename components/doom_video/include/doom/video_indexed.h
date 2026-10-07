#ifndef DOOM_VIDEO_INDEXED_H
#define DOOM_VIDEO_INDEXED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Convert an immutable 320x200 indexed frame with the exact touch overlay.
 * palette contains 256 already gamma-corrected numeric 0x00RRGGBB values.
 * With prescale false the output is 320x200; true selects the exact existing
 * 6:5 nearest-neighbor 384x240 conversion. stride counts source bytes and
 * pitch counts destination uint16_t words. Output padding stays untouched.
 *
 * Caller provides the complete source stride*200 bytes, palette[256],
 * destination pitch*(prescale ? 240 : 200) words, and row_scratch[320].
 * All four declared ranges, including padding, must be pairwise disjoint.
 * Palette and scratch require uint32_t alignment; output requires uint16_t
 * alignment. Source and palette must stay immutable throughout the call.
 * The worker caller must allocate scratch in INTERNAL memory; this portable
 * primitive cannot verify memory capabilities. No storage is retained or
 * allocated, and no full XRGB frame is produced.
 *
 * Invalid pointers, extents, overlap, strides or action masks return false
 * before any output or scratch write. Pointer checks validate arithmetic and
 * alignment, not allocation size or accessibility; supplying storage of the
 * stated extents is the caller's responsibility. Feature enablement belongs
 * to the caller; this additive primitive does not change the default path.
 */
bool doom_video_convert_indexed_touch_to_rgb565(
    const uint8_t *source,
    size_t stride,
    const uint32_t palette[256],
    uint32_t active_actions,
    uint16_t *dest,
    size_t pitch,
    uint32_t *row_scratch,
    bool prescale
);

#ifdef __cplusplus
}
#endif

#endif
