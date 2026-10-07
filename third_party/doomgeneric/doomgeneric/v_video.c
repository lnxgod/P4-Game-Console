//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 1993-2008 Raven Software
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	Gamma correction LUT stuff.
//	Functions to draw patches (by post) directly to screen.
//	Functions to blit a block to the screen.
//

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "i_system.h"

#include "doomtype.h"

#include "deh_str.h"
#include "i_swap.h"
#include "i_video.h"
#include "m_bbox.h"
#include "m_misc.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

#include "config.h"
#ifdef HAVE_LIBPNG
#include <png.h>
#endif

// TODO: There are separate RANGECHECK defines for different games, but this
// is common code. Fix this.
#define RANGECHECK

// Blending table used for fuzzpatch, etc.
// Only used in Heretic/Hexen

byte *tinttable = NULL;

// villsa [STRIFE] Blending table used for Strife
byte *xlatab = NULL;

// The screen buffer that the v_video.c code draws to.

static byte *dest_screen = NULL;
// Alternate native buffers can represent a vertical region of the screen.
// Keeping its global origin preserves the fractional 12/5 UI scaling phase.
static int dest_origin_y;
static int dest_height = SCREENHEIGHT;

int dirtybox[4]; 

// haleyjd 08/28/10: clipping callback function for patches.
// This is needed for Chocolate Strife, which clips patches to the screen.
static vpatchclipfunc_t patchclip_callback = NULL;

//
// V_MarkRect 
// 
void V_MarkRect(int x, int y, int width, int height) 
{ 
    // If we are temporarily using an alternate screen, do not 
    // affect the update box.

    if (dest_screen == I_VideoBuffer)
    {
        M_AddToBox (dirtybox, x, y); 
        M_AddToBox (dirtybox, x + width-1, y + height-1); 
    }
} 
 

//
// V_CopyRect 
// 
void V_CopyRect(int srcx, int srcy, byte *source,
                int width, int height,
                int destx, int desty)
{ 
    byte *src;
    byte *dest; 
 
#ifdef RANGECHECK 
    if (width < 0 || height < 0
     || srcx < 0
     || srcx + width > SCREENWIDTH
     || srcy < 0
     || srcy + height > SCREENHEIGHT 
     || destx < 0
     || destx + width > SCREENWIDTH
     || desty < dest_origin_y
     || desty + height > dest_origin_y + dest_height)
    {
        I_Error ("Bad V_CopyRect");
    }
#endif 

    V_MarkRect(destx, desty, width, height); 
 
    src = source + SCREENWIDTH * srcy + srcx; 
    dest = dest_screen + SCREENWIDTH * (desty - dest_origin_y) + destx; 

    for ( ; height>0 ; height--) 
    { 
        memcpy(dest, src, width); 
        src += SCREENWIDTH; 
        dest += SCREENWIDTH; 
    } 
} 
 
//
// V_SetPatchClipCallback
//
// haleyjd 08/28/10: Added for Strife support.
// By calling this function, you can setup runtime error checking for patch 
// clipping. Strife never caused errors by drawing patches partway off-screen.
// Some versions of vanilla DOOM also behaved differently than the default
// implementation, so this could possibly be extended to those as well for
// accurate emulation.
//
void V_SetPatchClipCallback(vpatchclipfunc_t func)
{
    patchclip_callback = func;
}

// UI assets retain Doom's canonical coordinate system. Each post is painted
// directly into the native raster; there is no low-resolution rendered frame.
#if SCREENWIDTH != P4_DOOM_CANONICAL_WIDTH || SCREENHEIGHT != P4_DOOM_CANONICAL_HEIGHT
static void V_NativePatch(int x, int y, patch_t *patch, boolean flipped,
                          int blend, boolean native_anchor, int only_column)
{
    int width = SHORT(patch->width), height = SHORT(patch->height);
    int col, start = only_column < 0 ? 0 : only_column;
    int end = only_column < 0 ? width : only_column + 1;
    int origin_x, origin_y;

    if (width <= 0 || height <= 0 || start < 0 || end > width)
        return;
    if (only_column < 0)
    {
        if (native_anchor)
        {
            x -= P4_DOOM_SCALE_X(SHORT(patch->leftoffset));
            y -= P4_DOOM_SCALE_Y(SHORT(patch->topoffset));
        }
        else
        {
            x -= SHORT(patch->leftoffset);
            y -= SHORT(patch->topoffset);
        }
        if (patchclip_callback && !patchclip_callback(patch, x, y))
            return;
        // Keep Doom's canonical UI rejection before reading any column table
        // (the intentional invalid intermission patch relies on this).
        if (!native_anchor && blend != 3
            && (x < 0 || y < 0 || x + width > P4_DOOM_CANONICAL_WIDTH
                || y + height > P4_DOOM_CANONICAL_HEIGHT))
            I_Error("Bad native V_DrawPatch x=%i y=%i width=%i height=%i",
                    x, y, width, height);
    }
    origin_x = native_anchor ? x : 0;
    origin_y = native_anchor ? y : 0;
    for (col = start; col < end; ++col)
    {
        int draw_col = only_column < 0 ? col : 0;
        int x0 = origin_x + P4_DOOM_SCALE_X((native_anchor ? 0 : x) + draw_col);
        int x1 = origin_x + P4_DOOM_SCALE_X((native_anchor ? 0 : x) + draw_col + 1);
        int source_col = flipped ? width - col - 1 : col;
        column_t *post = (column_t *) ((byte *)patch + LONG(patch->columnofs[source_col]));
        if (x0 < 0) x0 = 0;
        if (x1 > SCREENWIDTH) x1 = SCREENWIDTH;
        if (x0 >= x1) continue;
        while (post->topdelta != 0xff)
        {
            const byte *source = (byte *)post + 3;
            int row;
            for (row = 0; row < post->length; ++row)
            {
                int sy = post->topdelta + row;
                int y0 = origin_y + P4_DOOM_SCALE_Y((native_anchor ? 0 : y) + sy);
                int y1 = origin_y + P4_DOOM_SCALE_Y((native_anchor ? 0 : y) + sy + 1);
                int dy;
                if (y0 < dest_origin_y) y0 = dest_origin_y;
                if (y1 > dest_origin_y + dest_height) y1 = dest_origin_y + dest_height;
                for (dy = y0; dy < y1; ++dy)
                {
                    byte *dest = dest_screen + (dy - dest_origin_y) * SCREENWIDTH + x0;
                    if (blend == 0)
                        memset(dest, source[row], x1 - x0);
                    else
                    {
                        int dx;
                        for (dx = x0; dx < x1; ++dx, ++dest)
                        {
                            if (blend == 3)
                                *dest = tinttable[*dest << 8];
                            else if (blend == 2)
                                *dest = xlatab[*dest + (source[row] << 8)];
                            else
                                *dest = tinttable[(*dest << 8) + source[row]];
                        }
                    }
                }
            }
            post = (column_t *) ((byte *)post + post->length + 4);
        }
    }
    if (dest_screen == I_VideoBuffer)
        V_MarkRect(0, 0, SCREENWIDTH, SCREENHEIGHT);
}
#endif

// Native anchors are used by native world borders and automap markers. The
// patch asset itself is still scaled, rather than changing world coordinates.
void V_DrawPatchNative(int x, int y, patch_t *patch)
{
#if SCREENWIDTH != P4_DOOM_CANONICAL_WIDTH || SCREENHEIGHT != P4_DOOM_CANONICAL_HEIGHT
    V_NativePatch(x, y, patch, false, 0, true, -1);
#else
    V_DrawPatch(x, y, patch);
#endif
}

void V_DrawPatchColumn(int x, int y, patch_t *patch, int col)
{
#if SCREENWIDTH != P4_DOOM_CANONICAL_WIDTH || SCREENHEIGHT != P4_DOOM_CANONICAL_HEIGHT
    V_NativePatch(x, y, patch, false, 0, false, col);
#else
    column_t *post;
    if (col < 0 || col >= SHORT(patch->width) || x < 0 || x >= SCREENWIDTH)
        return;
    post = (column_t *) ((byte *)patch + LONG(patch->columnofs[col]));
    while (post->topdelta != 0xff)
    {
        const byte *source = (byte *)post + 3;
        int row;
        for (row = 0; row < post->length; ++row)
        {
            int dy = y + post->topdelta + row;
            if (dy >= dest_origin_y && dy < dest_origin_y + dest_height)
                dest_screen[(dy - dest_origin_y) * SCREENWIDTH + x] = source[row];
        }
        post = (column_t *) ((byte *)post + post->length + 4);
    }
#endif
}

//
// V_DrawPatch
// Masks a column based masked pic to the screen. 
//

void V_DrawPatch(int x, int y, patch_t *patch)
{
#if SCREENWIDTH != P4_DOOM_CANONICAL_WIDTH || SCREENHEIGHT != P4_DOOM_CANONICAL_HEIGHT
    V_NativePatch(x, y, patch, false, 0, false, -1);
    return;
#endif
 
    int count;
    int col;
    column_t *column;
    byte *desttop;
    byte *dest;
    byte *source;
    int w;

    y -= SHORT(patch->topoffset);
    x -= SHORT(patch->leftoffset);

    // haleyjd 08/28/10: Strife needs silent error checking here.
    if(patchclip_callback)
    {
        if(!patchclip_callback(patch, x, y))
            return;
    }

#ifdef RANGECHECK
    if (x < 0
     || x + SHORT(patch->width) > SCREENWIDTH
     || y < 0
     || y + SHORT(patch->height) > SCREENHEIGHT)
    {
        I_Error("Bad V_DrawPatch x=%i y=%i patch.width=%i patch.height=%i topoffset=%i leftoffset=%i", x, y, patch->width, patch->height, patch->topoffset, patch->leftoffset);
    }
#endif

    V_MarkRect(x, y, SHORT(patch->width), SHORT(patch->height));

    col = 0;
    desttop = dest_screen + (y - dest_origin_y) * SCREENWIDTH + x;

    w = SHORT(patch->width);

    for ( ; col<w ; x++, col++, desttop++)
    {
        column = (column_t *)((byte *)patch + LONG(patch->columnofs[col]));

        // step through the posts in a column
        while (column->topdelta != 0xff)
        {
            source = (byte *)column + 3;
            dest = desttop + column->topdelta*SCREENWIDTH;
            count = column->length;

            while (count--)
            {
                *dest = *source++;
                dest += SCREENWIDTH;
            }
            column = (column_t *)((byte *)column + column->length + 4);
        }
    }
}

//
// V_DrawPatchFlipped
// Masks a column based masked pic to the screen.
// Flips horizontally, e.g. to mirror face.
//

void V_DrawPatchFlipped(int x, int y, patch_t *patch)
{
#if SCREENWIDTH != P4_DOOM_CANONICAL_WIDTH || SCREENHEIGHT != P4_DOOM_CANONICAL_HEIGHT
    V_NativePatch(x, y, patch, true, 0, false, -1);
    return;
#endif

    int count;
    int col; 
    column_t *column; 
    byte *desttop;
    byte *dest;
    byte *source; 
    int w; 
 
    y -= SHORT(patch->topoffset); 
    x -= SHORT(patch->leftoffset); 

    // haleyjd 08/28/10: Strife needs silent error checking here.
    if(patchclip_callback)
    {
        if(!patchclip_callback(patch, x, y))
            return;
    }

#ifdef RANGECHECK 
    if (x < 0
     || x + SHORT(patch->width) > SCREENWIDTH
     || y < 0
     || y + SHORT(patch->height) > SCREENHEIGHT)
    {
        I_Error("Bad V_DrawPatchFlipped");
    }
#endif

    V_MarkRect (x, y, SHORT(patch->width), SHORT(patch->height));

    col = 0;
    desttop = dest_screen + (y - dest_origin_y) * SCREENWIDTH + x;

    w = SHORT(patch->width);

    for ( ; col<w ; x++, col++, desttop++)
    {
        column = (column_t *)((byte *)patch + LONG(patch->columnofs[w-1-col]));

        // step through the posts in a column
        while (column->topdelta != 0xff )
        {
            source = (byte *)column + 3;
            dest = desttop + column->topdelta*SCREENWIDTH;
            count = column->length;

            while (count--)
            {
                *dest = *source++;
                dest += SCREENWIDTH;
            }
            column = (column_t *)((byte *)column + column->length + 4);
        }
    }
}



//
// V_DrawPatchDirect
// Draws directly to the screen on the pc. 
//

void V_DrawPatchDirect(int x, int y, patch_t *patch)
{
    V_DrawPatch(x, y, patch); 
} 

//
// V_DrawTLPatch
//
// Masks a column based translucent masked pic to the screen.
//

void V_DrawTLPatch(int x, int y, patch_t * patch)
{
#if SCREENWIDTH != P4_DOOM_CANONICAL_WIDTH || SCREENHEIGHT != P4_DOOM_CANONICAL_HEIGHT
    V_NativePatch(x, y, patch, false, 1, false, -1);
    return;
#endif

    int count, col;
    column_t *column;
    byte *desttop, *dest, *source;
    int w;

    y -= SHORT(patch->topoffset);
    x -= SHORT(patch->leftoffset);

    if (x < 0
     || x + SHORT(patch->width) > SCREENWIDTH 
     || y < 0
     || y + SHORT(patch->height) > SCREENHEIGHT)
    {
        I_Error("Bad V_DrawTLPatch");
    }

    col = 0;
    desttop = dest_screen + (y - dest_origin_y) * SCREENWIDTH + x;

    w = SHORT(patch->width);
    for (; col < w; x++, col++, desttop++)
    {
        column = (column_t *) ((byte *) patch + LONG(patch->columnofs[col]));

        // step through the posts in a column

        while (column->topdelta != 0xff)
        {
            source = (byte *) column + 3;
            dest = desttop + column->topdelta * SCREENWIDTH;
            count = column->length;

            while (count--)
            {
                *dest = tinttable[((*dest) << 8) + *source++];
                dest += SCREENWIDTH;
            }
            column = (column_t *) ((byte *) column + column->length + 4);
        }
    }
}

//
// V_DrawXlaPatch
//
// villsa [STRIFE] Masks a column based translucent masked pic to the screen.
//

void V_DrawXlaPatch(int x, int y, patch_t * patch)
{
#if SCREENWIDTH != P4_DOOM_CANONICAL_WIDTH || SCREENHEIGHT != P4_DOOM_CANONICAL_HEIGHT
    V_NativePatch(x, y, patch, false, 2, false, -1);
    return;
#endif

    int count, col;
    column_t *column;
    byte *desttop, *dest, *source;
    int w;

    y -= SHORT(patch->topoffset);
    x -= SHORT(patch->leftoffset);

    if(patchclip_callback)
    {
        if(!patchclip_callback(patch, x, y))
            return;
    }

    col = 0;
    desttop = dest_screen + (y - dest_origin_y) * SCREENWIDTH + x;

    w = SHORT(patch->width);
    for(; col < w; x++, col++, desttop++)
    {
        column = (column_t *) ((byte *) patch + LONG(patch->columnofs[col]));

        // step through the posts in a column

        while(column->topdelta != 0xff)
        {
            source = (byte *) column + 3;
            dest = desttop + column->topdelta * SCREENWIDTH;
            count = column->length;

            while(count--)
            {
                *dest = xlatab[*dest + ((*source) << 8)];
                source++;
                dest += SCREENWIDTH;
            }
            column = (column_t *) ((byte *) column + column->length + 4);
        }
    }
}

//
// V_DrawAltTLPatch
//
// Masks a column based translucent masked pic to the screen.
//

void V_DrawAltTLPatch(int x, int y, patch_t * patch)
{
#if SCREENWIDTH != P4_DOOM_CANONICAL_WIDTH || SCREENHEIGHT != P4_DOOM_CANONICAL_HEIGHT
    V_NativePatch(x, y, patch, false, 1, false, -1);
    return;
#endif

    int count, col;
    column_t *column;
    byte *desttop, *dest, *source;
    int w;

    y -= SHORT(patch->topoffset);
    x -= SHORT(patch->leftoffset);

    if (x < 0
     || x + SHORT(patch->width) > SCREENWIDTH
     || y < 0
     || y + SHORT(patch->height) > SCREENHEIGHT)
    {
        I_Error("Bad V_DrawAltTLPatch");
    }

    col = 0;
    desttop = dest_screen + (y - dest_origin_y) * SCREENWIDTH + x;

    w = SHORT(patch->width);
    for (; col < w; x++, col++, desttop++)
    {
        column = (column_t *) ((byte *) patch + LONG(patch->columnofs[col]));

        // step through the posts in a column

        while (column->topdelta != 0xff)
        {
            source = (byte *) column + 3;
            dest = desttop + column->topdelta * SCREENWIDTH;
            count = column->length;

            while (count--)
            {
                *dest = tinttable[((*dest) << 8) + *source++];
                dest += SCREENWIDTH;
            }
            column = (column_t *) ((byte *) column + column->length + 4);
        }
    }
}

//
// V_DrawShadowedPatch
//
// Masks a column based masked pic to the screen.
//

void V_DrawShadowedPatch(int x, int y, patch_t *patch)
{
#if SCREENWIDTH != P4_DOOM_CANONICAL_WIDTH || SCREENHEIGHT != P4_DOOM_CANONICAL_HEIGHT
    // Tint the shadow before painting the foreground so overlapping posts are
    // consistently hidden by the native asset.
    V_NativePatch(x + 2, y + 2, patch, false, 3, false, -1);
    V_NativePatch(x, y, patch, false, 0, false, -1);
    return;
#endif

    int count, col;
    column_t *column;
    byte *desttop, *dest, *source;
    byte *desttop2, *dest2;
    int w;

    y -= SHORT(patch->topoffset);
    x -= SHORT(patch->leftoffset);

    if (x < 0
     || x + SHORT(patch->width) > SCREENWIDTH
     || y < 0
     || y + SHORT(patch->height) > SCREENHEIGHT)
    {
        I_Error("Bad V_DrawShadowedPatch");
    }

    col = 0;
    desttop = dest_screen + (y - dest_origin_y) * SCREENWIDTH + x;
    desttop2 = dest_screen + (y + 2 - dest_origin_y) * SCREENWIDTH + x + 2;

    w = SHORT(patch->width);
    for (; col < w; x++, col++, desttop++, desttop2++)
    {
        column = (column_t *) ((byte *) patch + LONG(patch->columnofs[col]));

        // step through the posts in a column

        while (column->topdelta != 0xff)
        {
            source = (byte *) column + 3;
            dest = desttop + column->topdelta * SCREENWIDTH;
            dest2 = desttop2 + column->topdelta * SCREENWIDTH;
            count = column->length;

            while (count--)
            {
                *dest2 = tinttable[((*dest2) << 8)];
                dest2 += SCREENWIDTH;
                *dest = *source++;
                dest += SCREENWIDTH;

            }
            column = (column_t *) ((byte *) column + column->length + 4);
        }
    }
}

//
// Load tint table from TINTTAB lump.
//

void V_LoadTintTable(void)
{
    tinttable = W_CacheLumpName("TINTTAB", PU_STATIC);
}

//
// V_LoadXlaTable
//
// villsa [STRIFE] Load xla table from XLATAB lump.
//

void V_LoadXlaTable(void)
{
    xlatab = W_CacheLumpName("XLATAB", PU_STATIC);
}

//
// V_DrawBlock
// Draw a linear block of pixels into the view buffer.
//

void V_DrawBlock(int x, int y, int width, int height, byte *src) 
{ 
    byte *dest; 
 
#ifdef RANGECHECK 
    if (width < 0 || height < 0
     || x < 0
     || x + width >SCREENWIDTH
     || y < dest_origin_y
     || y + height > dest_origin_y + dest_height)
    {
	I_Error ("Bad V_DrawBlock");
    }
#endif 
 
    V_MarkRect (x, y, width, height); 
 
    dest = dest_screen + (y - dest_origin_y) * SCREENWIDTH + x; 

    while (height--) 
    { 
	memcpy (dest, src, width); 
	src += width; 
	dest += SCREENWIDTH; 
    } 
} 

void V_DrawFilledBox(int x, int y, int w, int h, int c)
{
    int x0, x1, y0, y1, row;
    if (w <= 0 || h <= 0) return;
    x0 = P4_DOOM_SCALE_X(x);
    x1 = P4_DOOM_SCALE_X(x + w);
    y0 = P4_DOOM_SCALE_Y(y);
    y1 = P4_DOOM_SCALE_Y(y + h);
    if (x0 < 0) x0 = 0;
    if (x1 > SCREENWIDTH) x1 = SCREENWIDTH;
    if (y0 < 0) y0 = 0;
    if (y1 > SCREENHEIGHT) y1 = SCREENHEIGHT;
    if (x0 >= x1) return;
    for (row = y0; row < y1; ++row)
        memset(I_VideoBuffer + SCREENWIDTH * row + x0, c, x1 - x0);
}

void V_DrawHorizLine(int x, int y, int w, int c)
{
    V_DrawFilledBox(x, y, w, 1, c);
}

void V_DrawVertLine(int x, int y, int h, int c)
{
    V_DrawFilledBox(x, y, 1, h, c);
}

void V_DrawBox(int x, int y, int w, int h, int c)
{
    V_DrawHorizLine(x, y, w, c);
    V_DrawHorizLine(x, y+h-1, w, c);
    V_DrawVertLine(x, y, h, c);
    V_DrawVertLine(x+w-1, y, h, c);
}

//
// Draw a "raw" screen (lump containing raw data to blit directly
// to the screen)
//
 
void V_DrawRawScreen(byte *raw)
{
    int x, y;
    // A raw screen is a static 320x200 asset, not a rendered world frame.
    for (y = dest_origin_y; y < dest_origin_y + dest_height; ++y)
    {
        const byte *source = raw + (y * P4_DOOM_CANONICAL_HEIGHT / SCREENHEIGHT)
                                  * P4_DOOM_CANONICAL_WIDTH;
        byte *dest = dest_screen + (y - dest_origin_y) * SCREENWIDTH;
        for (x = 0; x < SCREENWIDTH; ++x)
            dest[x] = source[x * P4_DOOM_CANONICAL_WIDTH / SCREENWIDTH];
    }
}

//
// V_Init
// 
void V_Init (void) 
{ 
    // no-op!
    // There used to be separate screens that could be drawn to; these are
    // now handled in the upper layers.
}

// Set the buffer that the code draws to.

void V_UseBuffer(byte *buffer)
{
    dest_screen = buffer;
    dest_origin_y = 0;
    dest_height = SCREENHEIGHT;
}

void V_UseBufferRegion(byte *buffer, int origin_y, int height)
{
    if (!buffer || origin_y < 0 || height <= 0 || origin_y + height > SCREENHEIGHT)
        I_Error("Bad V_UseBufferRegion");
    dest_screen = buffer;
    dest_origin_y = origin_y;
    dest_height = height;
}

// Restore screen buffer to the i_video screen buffer.

void V_RestoreBuffer(void)
{
    dest_screen = I_VideoBuffer;
    dest_origin_y = 0;
    dest_height = SCREENHEIGHT;
}

//
// SCREEN SHOTS
//

typedef struct
{
    char		manufacturer;
    char		version;
    char		encoding;
    char		bits_per_pixel;

    unsigned short	xmin;
    unsigned short	ymin;
    unsigned short	xmax;
    unsigned short	ymax;
    
    unsigned short	hres;
    unsigned short	vres;

    unsigned char	palette[48];
    
    char		reserved;
    char		color_planes;
    unsigned short	bytes_per_line;
    unsigned short	palette_type;
    
    char		filler[58];
    unsigned char	data;		// unbounded
} PACKEDATTR pcx_t;


//
// WritePCXfile
//

void WritePCXfile(char *filename, byte *data,
                  int width, int height,
                  byte *palette)
{
    int		i;
    int		length;
    pcx_t*	pcx;
    byte*	pack;
	
    pcx = Z_Malloc (width*height*2+1000, PU_STATIC, NULL);

    pcx->manufacturer = 0x0a;		// PCX id
    pcx->version = 5;			// 256 color
    pcx->encoding = 1;			// uncompressed
    pcx->bits_per_pixel = 8;		// 256 color
    pcx->xmin = 0;
    pcx->ymin = 0;
    pcx->xmax = SHORT(width-1);
    pcx->ymax = SHORT(height-1);
    pcx->hres = SHORT(width);
    pcx->vres = SHORT(height);
    memset (pcx->palette,0,sizeof(pcx->palette));
    pcx->color_planes = 1;		// chunky image
    pcx->bytes_per_line = SHORT(width);
    pcx->palette_type = SHORT(2);	// not a grey scale
    memset (pcx->filler,0,sizeof(pcx->filler));

    // pack the image
    pack = &pcx->data;
	
    for (i=0 ; i<width*height ; i++)
    {
	if ( (*data & 0xc0) != 0xc0)
	    *pack++ = *data++;
	else
	{
	    *pack++ = 0xc1;
	    *pack++ = *data++;
	}
    }
    
    // write the palette
    *pack++ = 0x0c;	// palette ID byte
    for (i=0 ; i<768 ; i++)
	*pack++ = *palette++;
    
    // write output file
    length = pack - (byte *)pcx;
    M_WriteFile (filename, pcx, length);

    Z_Free (pcx);
}

#ifdef HAVE_LIBPNG
//
// WritePNGfile
//

static void error_fn(png_structp p, png_const_charp s)
{
    printf("libpng error: %s\n", s);
}

static void warning_fn(png_structp p, png_const_charp s)
{
    printf("libpng warning: %s\n", s);
}

void WritePNGfile(char *filename, byte *data,
                  int width, int height,
                  byte *palette)
{
    png_structp ppng;
    png_infop pinfo;
    png_colorp pcolor;
    FILE *handle;
    int i;

    handle = fopen(filename, "wb");
    if (!handle)
    {
        return;
    }

    ppng = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL,
                                   error_fn, warning_fn);
    if (!ppng)
    {
        return;
    }

    pinfo = png_create_info_struct(ppng);
    if (!pinfo)
    {
        png_destroy_write_struct(&ppng, NULL);
        return;
    }

    png_init_io(ppng, handle);

    png_set_IHDR(ppng, pinfo, width, height,
                 8, PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    pcolor = malloc(sizeof(*pcolor) * 256);
    if (!pcolor)
    {
        png_destroy_write_struct(&ppng, &pinfo);
        return;
    }

    for (i = 0; i < 256; i++)
    {
        pcolor[i].red   = *(palette + 3 * i);
        pcolor[i].green = *(palette + 3 * i + 1);
        pcolor[i].blue  = *(palette + 3 * i + 2);
    }

    png_set_PLTE(ppng, pinfo, pcolor, 256);
    free(pcolor);

    png_write_info(ppng, pinfo);

    for (i = 0; i < SCREENHEIGHT; i++)
    {
        png_write_row(ppng, data + i*SCREENWIDTH);
    }

    png_write_end(ppng, pinfo);
    png_destroy_write_struct(&ppng, &pinfo);
    fclose(handle);
}
#endif

//
// V_ScreenShot
//

void V_ScreenShot(char *format)
{
    int i;
    char lbmname[16]; // haleyjd 20110213: BUG FIX - 12 is too small!
    char *ext;
    
    // find a file name to save it to

#ifdef HAVE_LIBPNG
    extern int png_screenshots;
    if (png_screenshots)
    {
        ext = "png";
    }
    else
#endif
    {
        ext = "pcx";
    }

    for (i=0; i<=99; i++)
    {
        M_snprintf(lbmname, sizeof(lbmname), format, i, ext);

        if (!M_FileExists(lbmname))
        {
            break;      // file doesn't exist
        }
    }

    if (i == 100)
    {
        I_Error ("V_ScreenShot: Couldn't create a PCX");
    }

#ifdef HAVE_LIBPNG
    if (png_screenshots)
    {
    WritePNGfile(lbmname, I_VideoBuffer,
                 SCREENWIDTH, SCREENHEIGHT,
                 W_CacheLumpName (DEH_String("PLAYPAL"), PU_CACHE));
    }
    else
#endif
    {
    // save the pcx file
    WritePCXfile(lbmname, I_VideoBuffer,
                 SCREENWIDTH, SCREENHEIGHT,
                 W_CacheLumpName (DEH_String("PLAYPAL"), PU_CACHE));
    }
}

#define MOUSE_SPEED_BOX_WIDTH  120
#define MOUSE_SPEED_BOX_HEIGHT 9

void V_DrawMouseSpeedBox(int speed)
{
    extern int usemouse;
    int bgcolor, bordercolor, red, black, white, yellow;
    int box_x, box_y;
    int original_speed;
    int redline_x;
    int linelen;

    // Get palette indices for colors for widget. These depend on the
    // palette of the game being played.

    bgcolor = I_GetPaletteIndex(0x77, 0x77, 0x77);
    bordercolor = I_GetPaletteIndex(0x55, 0x55, 0x55);
    red = I_GetPaletteIndex(0xff, 0x00, 0x00);
    black = I_GetPaletteIndex(0x00, 0x00, 0x00);
    yellow = I_GetPaletteIndex(0xff, 0xff, 0x00);
    white = I_GetPaletteIndex(0xff, 0xff, 0xff);

    // If the mouse is turned off or acceleration is turned off, don't
    // draw the box at all.

    if (!usemouse || fabs(mouse_acceleration - 1) < 0.01)
    {
        return;
    }

    // Calculate box position

    box_x = P4_DOOM_CANONICAL_WIDTH - MOUSE_SPEED_BOX_WIDTH - 10;
    box_y = 15;

    V_DrawFilledBox(box_x, box_y,
                    MOUSE_SPEED_BOX_WIDTH, MOUSE_SPEED_BOX_HEIGHT, bgcolor);
    V_DrawBox(box_x, box_y,
              MOUSE_SPEED_BOX_WIDTH, MOUSE_SPEED_BOX_HEIGHT, bordercolor);

    // Calculate the position of the red line.  This is 1/3 of the way
    // along the box.

    redline_x = MOUSE_SPEED_BOX_WIDTH / 3;

    // Undo acceleration and get back the original mouse speed

    if (speed < mouse_threshold)
    {
        original_speed = speed;
    }
    else
    {
        original_speed = speed - mouse_threshold;
        original_speed = (int) (original_speed / mouse_acceleration);
        original_speed += mouse_threshold;
    }

    // Calculate line length

    linelen = (original_speed * redline_x) / mouse_threshold;

    // Draw horizontal "thermometer" 

    if (linelen > MOUSE_SPEED_BOX_WIDTH - 1)
    {
        linelen = MOUSE_SPEED_BOX_WIDTH - 1;
    }

    V_DrawHorizLine(box_x + 1, box_y + 4, MOUSE_SPEED_BOX_WIDTH - 2, black);

    if (linelen < redline_x)
    {
        V_DrawHorizLine(box_x + 1, box_y + MOUSE_SPEED_BOX_HEIGHT / 2,
                      linelen, white);
    }
    else
    {
        V_DrawHorizLine(box_x + 1, box_y + MOUSE_SPEED_BOX_HEIGHT / 2,
                        redline_x, white);
        V_DrawHorizLine(box_x + redline_x, box_y + MOUSE_SPEED_BOX_HEIGHT / 2,
                        linelen - redline_x, yellow);
    }

    // Draw red line

    V_DrawVertLine(box_x + redline_x, box_y + 1,
                 MOUSE_SPEED_BOX_HEIGHT - 2, red);
}

