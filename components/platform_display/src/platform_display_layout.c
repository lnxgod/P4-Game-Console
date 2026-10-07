// SPDX-License-Identifier: MIT

#include "platform_display_layout.h"

#include <string.h>

#include "platform/board.h"

enum {
    SOURCE_WIDTH = 320,
    SOURCE_HEIGHT = 200,
    LOGICAL_WIDTH = PLATFORM_BOARD_DISPLAY_WIDTH,
    LOGICAL_HEIGHT = PLATFORM_BOARD_DISPLAY_HEIGHT,
    DESTINATION_WIDTH = PLATFORM_BOARD_DISPLAY_NATIVE_WIDTH,
    DESTINATION_HEIGHT = PLATFORM_BOARD_DISPLAY_NATIVE_HEIGHT,
    VIEWPORT_WIDTH = PLATFORM_BOARD_GAME_VIEWPORT_WIDTH,
    VIEWPORT_HEIGHT = PLATFORM_BOARD_GAME_VIEWPORT_HEIGHT,
    LEFT_MARGIN = PLATFORM_BOARD_GAME_MARGIN_LEFT,
    TOP_MARGIN = PLATFORM_BOARD_GAME_MARGIN_TOP,
};

_Static_assert(VIEWPORT_WIDTH >= SOURCE_WIDTH &&
                   VIEWPORT_HEIGHT >= SOURCE_HEIGHT,
               "display scaler requires a non-shrinking viewport");
_Static_assert(LEFT_MARGIN + VIEWPORT_WIDTH <= LOGICAL_WIDTH &&
                   TOP_MARGIN + VIEWPORT_HEIGHT <= LOGICAL_HEIGHT,
               "game viewport must fit the logical display");
_Static_assert(PLATFORM_BOARD_DISPLAY_ROTATION_CW_DEGREES == 0U ||
                   PLATFORM_BOARD_DISPLAY_ROTATION_CW_DEGREES == 90U,
               "display layout supports native or clockwise-90 scanout");

#if defined(CONFIG_P4_BOARD_M5STACK_TAB5) && CONFIG_P4_BOARD_M5STACK_TAB5
/* Reading a full PSRAM column thrashes cache sets on the 720x1280 panel.
 * Tile both axes so adjacent source pixels are reused while still resident.
 * All scaling remains the same integer nearest-neighbor mapping. */
static bool layout_tab5_tiled(const uint16_t *source, size_t source_stride,
    size_t source_width, size_t source_height, uint16_t *destination,
    size_t destination_stride)
{
    enum { TILE = 32 };
    _Static_assert(TOP_MARGIN == 0 && VIEWPORT_HEIGHT == DESTINATION_WIDTH,
                   "Tab5 tiled viewport must span native width");
    const size_t first_y = LOGICAL_WIDTH - LEFT_MARGIN - VIEWPORT_WIDTH;
    const size_t end_y = LOGICAL_WIDTH - LEFT_MARGIN;
    size_t source_rows[DESTINATION_WIDTH];
    for (size_t x=0; x<DESTINATION_WIDTH; ++x)
        source_rows[x] = (x * source_height / VIEWPORT_HEIGHT) * source_stride;
    for (size_t y=0; y<first_y; ++y)
        memset(destination+y*destination_stride, 0, DESTINATION_WIDTH*sizeof(uint16_t));
    for (size_t y=end_y; y<DESTINATION_HEIGHT; ++y)
        memset(destination+y*destination_stride, 0, DESTINATION_WIDTH*sizeof(uint16_t));
    for (size_t by=first_y; by<end_y; by+=TILE) {
        const size_t limit_y = by+TILE<end_y ? by+TILE : end_y;
        for (size_t bx=0; bx<DESTINATION_WIDTH; bx+=TILE) {
            const size_t limit_x = bx+TILE<DESTINATION_WIDTH ? bx+TILE : DESTINATION_WIDTH;
            for (size_t y=by; y<limit_y; ++y) {
                const size_t sx = (LOGICAL_WIDTH-1U-y-LEFT_MARGIN)*source_width/VIEWPORT_WIDTH;
                uint16_t *row = destination+y*destination_stride;
                for (size_t x=bx; x<limit_x; ++x)
                    row[x] = source[source_rows[x]+sx];
            }
        }
    }
    return true;
}
#endif

#if defined(CONFIG_P4_BOARD_M5STACK_TAB5) && CONFIG_P4_BOARD_M5STACK_TAB5
bool platform_display_layout_rgb565_1280x720(const uint16_t *source,
    size_t source_stride, uint16_t *destination, size_t destination_stride, size_t height)
{
    if (!source || !destination || source_stride < 1280U ||
        source_stride > SIZE_MAX / 720U / sizeof(*source) ||
        destination_stride < 720U || destination_stride > SIZE_MAX / 1280U / sizeof(*destination) ||
        height < 1280U) return false;
    for (size_t by=0; by<1280U; by+=32U)
        for (size_t bx=0; bx<720U; bx+=32U)
            for (size_t y=by; y<by+32U && y<1280U; ++y)
                for (size_t x=bx; x<bx+32U && x<720U; ++x)
                    destination[y*destination_stride+x]=source[x*source_stride+1279U-y];
    return true;
}

bool platform_display_layout_rgb565_1152x720(const uint16_t *source,
    size_t source_stride_pixels, uint16_t *destination,
    size_t destination_stride_pixels, size_t destination_height)
{
    if (source == NULL || destination == NULL || source_stride_pixels < 1152U ||
        source_stride_pixels > SIZE_MAX / 720U / sizeof(*source) ||
        destination_stride_pixels < DESTINATION_WIDTH ||
        destination_stride_pixels > SIZE_MAX / DESTINATION_HEIGHT / sizeof(*destination) ||
        destination_height < DESTINATION_HEIGHT) return false;
    return layout_tab5_tiled(source, source_stride_pixels, 1152U, 720U,
        destination, destination_stride_pixels);
}
#endif

bool platform_display_layout_rgb565_320x200(const uint16_t *source,
                                            size_t source_stride_pixels,
                                            uint16_t *destination,
                                            size_t destination_stride_pixels,
                                            size_t destination_height)
{
    if (source == NULL || destination == NULL ||
        source_stride_pixels < SOURCE_WIDTH ||
        destination_stride_pixels < DESTINATION_WIDTH ||
        destination_height < DESTINATION_HEIGHT) {
        return false;
    }

#if defined(CONFIG_P4_BOARD_M5STACK_TAB5) && CONFIG_P4_BOARD_M5STACK_TAB5
    return layout_tab5_tiled(source, source_stride_pixels, 320, 200,
                             destination, destination_stride_pixels);
#elif PLATFORM_BOARD_DISPLAY_ROTATION_CW_DEGREES == 90U
    for (size_t native_y = 0U; native_y < DESTINATION_HEIGHT; ++native_y) {
        uint16_t *const destination_row =
            destination + native_y * destination_stride_pixels;
        memset(destination_row, 0,
               (size_t)DESTINATION_WIDTH * sizeof(*destination_row));
        const size_t logical_x = (size_t)LOGICAL_WIDTH - 1U - native_y;
        const size_t viewport_x = logical_x - LEFT_MARGIN;
        if (viewport_x >= VIEWPORT_WIDTH) {
            continue;
        }
        const size_t source_x = viewport_x * SOURCE_WIDTH / VIEWPORT_WIDTH;
        for (size_t viewport_y = 0U; viewport_y < VIEWPORT_HEIGHT;
             ++viewport_y) {
            const size_t source_y =
                viewport_y * SOURCE_HEIGHT / VIEWPORT_HEIGHT;
            destination_row[TOP_MARGIN + viewport_y] =
                source[source_y * source_stride_pixels + source_x];
        }
    }
#else
    for (size_t native_y = 0U; native_y < DESTINATION_HEIGHT; ++native_y) {
        uint16_t *const destination_row =
            destination + native_y * destination_stride_pixels;
        memset(destination_row, 0,
               (size_t)DESTINATION_WIDTH * sizeof(*destination_row));
        const size_t viewport_y = native_y - TOP_MARGIN;
        if (viewport_y >= VIEWPORT_HEIGHT) {
            continue;
        }
        const size_t source_y = viewport_y * SOURCE_HEIGHT / VIEWPORT_HEIGHT;
        for (size_t viewport_x = 0U; viewport_x < VIEWPORT_WIDTH;
             ++viewport_x) {
            const size_t source_x =
                viewport_x * SOURCE_WIDTH / VIEWPORT_WIDTH;
            destination_row[LEFT_MARGIN + viewport_x] =
                source[source_y * source_stride_pixels + source_x];
        }
    }
#endif
    return true;
}

bool platform_display_layout_rgb565_768x480(const uint16_t *source,
                                            size_t source_stride_pixels,
                                            uint16_t *destination,
                                            size_t destination_stride_pixels,
                                            size_t destination_height)
{
    enum { CONTENT_WIDTH = 768, CONTENT_HEIGHT = 480 };
    if (source == NULL || destination == NULL ||
        source_stride_pixels < CONTENT_WIDTH ||
        destination_stride_pixels < DESTINATION_WIDTH ||
        destination_height < DESTINATION_HEIGHT) {
        return false;
    }
#if defined(CONFIG_P4_BOARD_M5STACK_TAB5) && CONFIG_P4_BOARD_M5STACK_TAB5
    return layout_tab5_tiled(source, source_stride_pixels, 768, 480,
                             destination, destination_stride_pixels);
#elif PLATFORM_BOARD_DISPLAY_ROTATION_CW_DEGREES == 90U
    for (size_t native_y = 0U; native_y < DESTINATION_HEIGHT; ++native_y) {
        uint16_t *const row = destination + native_y * destination_stride_pixels;
        memset(row, 0, (size_t)DESTINATION_WIDTH * sizeof(*row));
        const size_t logical_x = (size_t)LOGICAL_WIDTH - 1U - native_y;
        const size_t viewport_x = logical_x - LEFT_MARGIN;
        if (viewport_x >= VIEWPORT_WIDTH) {
            continue;
        }
        const size_t source_x = viewport_x * CONTENT_WIDTH / VIEWPORT_WIDTH;
        for (size_t viewport_y = 0U; viewport_y < VIEWPORT_HEIGHT;
             ++viewport_y) {
            const size_t source_y =
                viewport_y * CONTENT_HEIGHT / VIEWPORT_HEIGHT;
            row[TOP_MARGIN + viewport_y] =
                source[source_y * source_stride_pixels + source_x];
        }
    }
#else
    for (size_t native_y = 0U; native_y < DESTINATION_HEIGHT; ++native_y) {
        uint16_t *const row = destination + native_y * destination_stride_pixels;
        memset(row, 0, (size_t)DESTINATION_WIDTH * sizeof(*row));
        const size_t viewport_y = native_y - TOP_MARGIN;
        if (viewport_y >= VIEWPORT_HEIGHT) {
            continue;
        }
        const size_t source_y = viewport_y * CONTENT_HEIGHT / VIEWPORT_HEIGHT;
        for (size_t viewport_x = 0U; viewport_x < VIEWPORT_WIDTH;
             ++viewport_x) {
            const size_t source_x =
                viewport_x * CONTENT_WIDTH / VIEWPORT_WIDTH;
            row[LEFT_MARGIN + viewport_x] =
                source[source_y * source_stride_pixels + source_x];
        }
    }
#endif
    return true;
}

bool platform_display_layout_map_content_region_ccw(
    const platform_display_rgb565_region_t *source,
    platform_display_rgb565_region_t *destination)
{
    enum {
        CONTENT_WIDTH = 768,
        CONTENT_HEIGHT = 480,
        NATIVE_CONTENT_TOP = 16,
    };
    if (source == NULL || destination == NULL || source->width == 0U ||
        source->height == 0U || source->x >= CONTENT_WIDTH ||
        source->y >= CONTENT_HEIGHT ||
        source->width > CONTENT_WIDTH - source->x ||
        source->height > CONTENT_HEIGHT - source->y) {
        return false;
    }
#if PLATFORM_BOARD_DISPLAY_ROTATION_CW_DEGREES == 90U
    destination->x = source->y;
    destination->y = (uint16_t)(NATIVE_CONTENT_TOP + CONTENT_WIDTH -
                                (source->x + source->width));
    destination->width = source->height;
    destination->height = source->width;
    return true;
#else
    (void)CONTENT_HEIGHT;
    return false;
#endif
}

static bool tab5_region_valid(const platform_display_rgb565_region_t *r)
{
    return r&&r->width&&r->height&&r->x<1280U&&r->y<720U&&
        r->width<=1280U-r->x&&r->height<=720U-r->y;
}
bool platform_display_layout_tab5_damage(
    const platform_display_rgb565_region_t *current,
    const platform_display_rgb565_region_t *previous,
    platform_display_rgb565_region_t *source,
    platform_display_rgb565_region_t *destination)
{
    if(!tab5_region_valid(current)||!source||!destination||
       (previous&&!tab5_region_valid(previous)))return false;
    *source=(platform_display_rgb565_region_t){0,0,1280,720};
    if(previous){
        const unsigned x=current->x<previous->x?current->x:previous->x;
        const unsigned y=current->y<previous->y?current->y:previous->y;
        const unsigned cr=(unsigned)current->x+current->width,pr=(unsigned)previous->x+previous->width;
        const unsigned cb=(unsigned)current->y+current->height,pb=(unsigned)previous->y+previous->height;
        *source=(platform_display_rgb565_region_t){(uint16_t)x,(uint16_t)y,
            (uint16_t)((cr>pr?cr:pr)-x),(uint16_t)((cb>pb?cb:pb)-y)};
    }
    /* Pinned IDF 5.5.3 PPA invalidates the rotated output height, new_block_h;
     * narrow left-edge damage therefore needs no artificial enlargement. */
    *destination=(platform_display_rgb565_region_t){source->y,
        (uint16_t)(1280U-source->x-source->width),source->height,source->width};
    return true;
}

static uint32_t region_area(
    const platform_display_rgb565_region_t *region)
{
    return (uint32_t)region->width * (uint32_t)region->height;
}

static bool region_contains(
    const platform_display_rgb565_region_t *outer,
    const platform_display_rgb565_region_t *inner)
{
    return outer->x <= inner->x && outer->y <= inner->y &&
        (uint32_t)outer->x + outer->width >=
            (uint32_t)inner->x + inner->width &&
        (uint32_t)outer->y + outer->height >=
            (uint32_t)inner->y + inner->height;
}

bool platform_display_layout_compact_content_region(
    platform_display_rgb565_region_t *regions,
    size_t *region_count,
    size_t region_capacity,
    const platform_display_rgb565_region_t *candidate)
{
    platform_display_rgb565_region_t mapped;
    if (regions == NULL || region_count == NULL || candidate == NULL ||
        *region_count > region_capacity ||
        !platform_display_layout_map_content_region_ccw(candidate, &mapped)) {
        return false;
    }
    platform_display_rgb565_region_t merged = *candidate;
    size_t index = 0U;
    while (index < *region_count) {
        if (region_contains(&regions[index], &merged)) {
            return true;
        }
        const uint32_t left = regions[index].x < merged.x
            ? regions[index].x : merged.x;
        const uint32_t top = regions[index].y < merged.y
            ? regions[index].y : merged.y;
        const uint32_t region_right =
            (uint32_t)regions[index].x + regions[index].width;
        const uint32_t merged_right = (uint32_t)merged.x + merged.width;
        const uint32_t right = region_right > merged_right
            ? region_right : merged_right;
        const uint32_t region_bottom =
            (uint32_t)regions[index].y + regions[index].height;
        const uint32_t merged_bottom = (uint32_t)merged.y + merged.height;
        const uint32_t bottom = region_bottom > merged_bottom
            ? region_bottom : merged_bottom;
        const uint32_t bounding_area = (right - left) * (bottom - top);
        const uint32_t separate_area =
            region_area(&regions[index]) + region_area(&merged);
        if (region_contains(&merged, &regions[index]) ||
            bounding_area <= separate_area) {
            merged = (platform_display_rgb565_region_t){
                .x = (uint16_t)left,
                .y = (uint16_t)top,
                .width = (uint16_t)(right - left),
                .height = (uint16_t)(bottom - top),
            };
            regions[index] = regions[*region_count - 1U];
            --*region_count;
            index = 0U;
            continue;
        }
        ++index;
    }
    if (*region_count == region_capacity) {
        return false;
    }
    regions[*region_count] = merged;
    ++*region_count;
    return true;
}

bool platform_display_layout_rgb565_384x240(const uint16_t *source,
                                            size_t source_stride_pixels,
                                            uint16_t *destination,
                                            size_t destination_stride_pixels,
                                            size_t destination_height)
{
    enum { SHELL_WIDTH = 384, SHELL_HEIGHT = 240 };
    if (source == NULL || destination == NULL ||
        source_stride_pixels < SHELL_WIDTH ||
        destination_stride_pixels < DESTINATION_WIDTH ||
        destination_height < DESTINATION_HEIGHT) {
        return false;
    }
#if defined(CONFIG_P4_BOARD_M5STACK_TAB5) && CONFIG_P4_BOARD_M5STACK_TAB5
    return layout_tab5_tiled(source, source_stride_pixels, 384, 240,
                             destination, destination_stride_pixels);
#elif PLATFORM_BOARD_DISPLAY_ROTATION_CW_DEGREES == 90U
    for (size_t native_y = 0U; native_y < DESTINATION_HEIGHT; ++native_y) {
        uint16_t *const row = destination + native_y * destination_stride_pixels;
        memset(row, 0, (size_t)DESTINATION_WIDTH * sizeof(*row));
        const size_t logical_x = (size_t)LOGICAL_WIDTH - 1U - native_y;
        const size_t viewport_x = logical_x - LEFT_MARGIN;
        if (viewport_x >= VIEWPORT_WIDTH) {
            continue;
        }
        const size_t source_x = viewport_x * SHELL_WIDTH / VIEWPORT_WIDTH;
        for (size_t viewport_y = 0U; viewport_y < VIEWPORT_HEIGHT;
             ++viewport_y) {
            const size_t source_y =
                viewport_y * SHELL_HEIGHT / VIEWPORT_HEIGHT;
            row[TOP_MARGIN + viewport_y] =
                source[source_y * source_stride_pixels + source_x];
        }
    }
#else
    for (size_t native_y = 0U; native_y < DESTINATION_HEIGHT; ++native_y) {
        uint16_t *const row = destination + native_y * destination_stride_pixels;
        memset(row, 0, (size_t)DESTINATION_WIDTH * sizeof(*row));
        const size_t viewport_y = native_y - TOP_MARGIN;
        if (viewport_y >= VIEWPORT_HEIGHT) {
            continue;
        }
        const size_t source_y = viewport_y * SHELL_HEIGHT / VIEWPORT_HEIGHT;
        for (size_t viewport_x = 0U; viewport_x < VIEWPORT_WIDTH;
             ++viewport_x) {
            const size_t source_x =
                viewport_x * SHELL_WIDTH / VIEWPORT_WIDTH;
            row[LEFT_MARGIN + viewport_x] =
                source[source_y * source_stride_pixels + source_x];
        }
    }
#endif
    return true;
}

bool platform_display_layout_rgb565_to_rgb888_1280x720(
    const uint16_t *source, size_t source_stride_pixels,
    uint8_t *destination, size_t destination_stride_bytes,
    size_t destination_height)
{
    enum {
        HDMI_WIDTH = 1280,
        HDMI_HEIGHT = 720,
        HDMI_PIXEL_BYTES = 3,
        HDMI_LEFT_MARGIN = 160,
        HDMI_TOP_MARGIN = 60,
        HDMI_SCALE = 3,
    };
    const size_t active_row_bytes = HDMI_WIDTH * HDMI_PIXEL_BYTES;
    if (source == NULL || destination == NULL ||
        source_stride_pixels < SOURCE_WIDTH ||
        destination_stride_bytes < active_row_bytes ||
        destination_height < HDMI_HEIGHT) {
        return false;
    }
    for (size_t y = 0U; y < HDMI_HEIGHT; ++y) {
        memset(destination + y * destination_stride_bytes, 0,
               active_row_bytes);
    }
    for (size_t source_y = 0U; source_y < SOURCE_HEIGHT; ++source_y) {
        uint8_t *const first_row = destination +
            (HDMI_TOP_MARGIN + source_y * HDMI_SCALE) *
                destination_stride_bytes;
        const uint16_t *const source_row =
            source + source_y * source_stride_pixels;
        for (size_t source_x = 0U; source_x < SOURCE_WIDTH; ++source_x) {
            const uint16_t pixel = source_row[source_x];
            const uint8_t red = (uint8_t)(
                ((pixel >> 11U) & 0x1fU) * 255U / 31U);
            const uint8_t green = (uint8_t)(
                ((pixel >> 5U) & 0x3fU) * 255U / 63U);
            const uint8_t blue = (uint8_t)((pixel & 0x1fU) * 255U / 31U);
            const size_t destination_x =
                HDMI_LEFT_MARGIN + source_x * HDMI_SCALE;
            for (size_t repeat = 0U; repeat < HDMI_SCALE; ++repeat) {
                uint8_t *const out = first_row +
                    (destination_x + repeat) * HDMI_PIXEL_BYTES;
                out[0] = red;
                out[1] = green;
                out[2] = blue;
            }
        }
        for (size_t repeat = 1U; repeat < HDMI_SCALE; ++repeat) {
            memcpy(first_row + repeat * destination_stride_bytes,
                   first_row, active_row_bytes);
        }
    }
    return true;
}

bool platform_display_layout_rgb565_384x240_to_rgb888_1280x720(
    const uint16_t *source, size_t source_stride_pixels,
    uint8_t *destination, size_t destination_stride_bytes,
    size_t destination_height)
{
    enum {
        SHELL_WIDTH = 384,
        SHELL_HEIGHT = 240,
        HDMI_WIDTH = 1280,
        HDMI_HEIGHT = 720,
        HDMI_PIXEL_BYTES = 3,
        HDMI_VIEWPORT_WIDTH = 960,
        HDMI_VIEWPORT_HEIGHT = 600,
        HDMI_LEFT_MARGIN = 160,
        HDMI_TOP_MARGIN = 60,
    };
    const size_t active_row_bytes = HDMI_WIDTH * HDMI_PIXEL_BYTES;
    if (source == NULL || destination == NULL ||
        source_stride_pixels < SHELL_WIDTH ||
        destination_stride_bytes < active_row_bytes ||
        destination_height < HDMI_HEIGHT) {
        return false;
    }
    for (size_t y = 0U; y < HDMI_HEIGHT; ++y) {
        memset(destination + y * destination_stride_bytes, 0,
               active_row_bytes);
    }
    for (size_t viewport_y = 0U; viewport_y < HDMI_VIEWPORT_HEIGHT;
         ++viewport_y) {
        const size_t source_y =
            viewport_y * SHELL_HEIGHT / HDMI_VIEWPORT_HEIGHT;
        uint8_t *const output_row = destination +
            (HDMI_TOP_MARGIN + viewport_y) * destination_stride_bytes;
        const uint16_t *const source_row =
            source + source_y * source_stride_pixels;
        for (size_t viewport_x = 0U; viewport_x < HDMI_VIEWPORT_WIDTH;
             ++viewport_x) {
            const size_t source_x =
                viewport_x * SHELL_WIDTH / HDMI_VIEWPORT_WIDTH;
            const uint16_t pixel = source_row[source_x];
            const uint8_t red =
                (uint8_t)(((pixel >> 11U) & 0x1fU) * 255U / 31U);
            const uint8_t green =
                (uint8_t)(((pixel >> 5U) & 0x3fU) * 255U / 63U);
            const uint8_t blue =
                (uint8_t)((pixel & 0x1fU) * 255U / 31U);
            uint8_t *const out = output_row +
                (HDMI_LEFT_MARGIN + viewport_x) * HDMI_PIXEL_BYTES;
            out[0] = red;
            out[1] = green;
            out[2] = blue;
        }
    }
    return true;
}

bool platform_display_layout_rgb565_prescaled_game_384x240(
    const uint16_t *source, size_t source_stride_pixels,
    uint16_t *destination, size_t destination_stride_pixels,
    size_t destination_height)
{
#if defined(CONFIG_P4_BOARD_M5STACK_TAB5) && CONFIG_P4_BOARD_M5STACK_TAB5
    if (source == NULL || destination == NULL || source_stride_pixels < 384U ||
        source_stride_pixels > SIZE_MAX / 240U / sizeof(*source) ||
        destination_stride_pixels < DESTINATION_WIDTH ||
        destination_stride_pixels > SIZE_MAX / DESTINATION_HEIGHT / sizeof(*destination) ||
        destination_height < DESTINATION_HEIGHT)
        return false;

    /* The prescale keeps every original pixel at ceil(index * 6 / 5).
     * Recover the exact baseline 320x200 sampling first, then map those
     * coordinates into the expanded source. Direct 384x240 scaling would
     * move nearest-neighbor boundaries when the accelerator fails. */
    enum { TILE = 32 };
    const size_t first_y = LOGICAL_WIDTH - LEFT_MARGIN - VIEWPORT_WIDTH;
    const size_t end_y = LOGICAL_WIDTH - LEFT_MARGIN;
    size_t source_rows[DESTINATION_WIDTH];
    for (size_t x = 0U; x < DESTINATION_WIDTH; ++x) {
        const size_t original_y = x * 200U / VIEWPORT_HEIGHT;
        source_rows[x] = ((original_y * 6U + 4U) / 5U) * source_stride_pixels;
    }
    for (size_t y = 0U; y < first_y; ++y)
        memset(destination + y * destination_stride_pixels, 0,
               DESTINATION_WIDTH * sizeof(*destination));
    for (size_t y = end_y; y < DESTINATION_HEIGHT; ++y)
        memset(destination + y * destination_stride_pixels, 0,
               DESTINATION_WIDTH * sizeof(*destination));
    for (size_t by = first_y; by < end_y; by += TILE) {
        const size_t limit_y = by + TILE < end_y ? by + TILE : end_y;
        for (size_t bx = 0U; bx < DESTINATION_WIDTH; bx += TILE) {
            const size_t limit_x = bx + TILE < DESTINATION_WIDTH
                ? bx + TILE : DESTINATION_WIDTH;
            for (size_t y = by; y < limit_y; ++y) {
                const size_t original_x =
                    (LOGICAL_WIDTH - 1U - y - LEFT_MARGIN) * 320U / VIEWPORT_WIDTH;
                const size_t source_x = (original_x * 6U + 4U) / 5U;
                uint16_t *row = destination + y * destination_stride_pixels;
                for (size_t x = bx; x < limit_x; ++x)
                    row[x] = source[source_rows[x] + source_x];
            }
        }
    }
    return true;
#else
    (void)source;
    (void)source_stride_pixels;
    (void)destination;
    (void)destination_stride_pixels;
    (void)destination_height;
    return false;
#endif
}
