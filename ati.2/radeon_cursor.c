/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/ati/radeon_cursor.c,v 1.17 2002/10/31 05:49:58 keithp Exp $ */
/*
 * Copyright 2000 ATI Technologies Inc., Markham, Ontario, and
 *                VA Linux Systems Inc., Fremont, California.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL ATI, VA LINUX SYSTEMS AND/OR
 * THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * Authors:
 *   Kevin E. Martin <martin@xfree86.org>
 *   Rickard E. Faith <faith@valinux.com>
 *
 * References:
 *
 * !!!! FIXME !!!!
 *   RAGE 128 VR/ RAGE 128 GL Register Reference Manual (Technical
 *   Reference Manual P/N RRG-G04100-C Rev. 0.04), ATI Technologies: April
 *   1999.
 *
 *   RAGE 128 Software Development Manual (Technical Reference Manual P/N
 *   SDK-G04000 Rev. 0.01), ATI Technologies: June 1999.
 *
 */

				/* Driver data structures */
#include "radeon.h"
#include "radeon_reg.h"

				/* X and server generic header files */
#include "xf86.h"

#if X_BYTE_ORDER == X_BIG_ENDIAN
#define P_SWAP32(a, b)							\
do {									\
    ((char *)a)[0] = ((char *)b)[3];					\
    ((char *)a)[1] = ((char *)b)[2];					\
    ((char *)a)[2] = ((char *)b)[1];					\
    ((char *)a)[3] = ((char *)b)[0];					\
} while (0)

#define P_SWAP16(a, b)							\
do {									\
    ((char *)a)[0] = ((char *)b)[1];					\
    ((char *)a)[1] = ((char *)b)[0];					\
    ((char *)a)[2] = ((char *)b)[3];					\
    ((char *)a)[3] = ((char *)b)[2];					\
} while (0)
#endif


/* Set cursor foreground and background colors */
static void RADEONSetCursorColors(ScrnInfoPtr pScrn, int bg, int fg)
{
    RADEONInfoPtr  info       = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;

    if (info->IsSecondary || info->Clone) {
	OUTREG(RADEON_CUR2_CLR0, bg);
	OUTREG(RADEON_CUR2_CLR1, fg);
    }

    if (!info->IsSecondary) {
	OUTREG(RADEON_CUR_CLR0, bg);
	OUTREG(RADEON_CUR_CLR1, fg);
    }
}


/* Set cursor position to (x,y) with offset into cursor bitmap at
 * (xorigin,yorigin)
 */
static void RADEONSetCursorPosition(ScrnInfoPtr pScrn, int x, int y)
{
    RADEONInfoPtr      info       = RADEONPTR(pScrn);
    unsigned char     *RADEONMMIO = info->MMIO;
    xf86CursorInfoPtr  cursor     = info->cursor;
    int                xorigin    = 0;
    int                yorigin    = 0;
    int                total_y    = pScrn->frameY1 - pScrn->frameY0;
    int                X2         = pScrn->frameX0 + x;
    int                Y2         = pScrn->frameY0 + y;
    int		       stride     = 16;

#ifdef ARGB_CURSOR
    if (info->cursor_argb) stride = 256;
#endif
    if (x < 0)                        xorigin = -x+1;
    if (y < 0)                        yorigin = -y+1;
    if (y > total_y)                  y       = total_y;
    if (info->Flags & V_DBLSCAN)      y       *= 2;
    if (xorigin >= cursor->MaxWidth)  xorigin = cursor->MaxWidth - 1;
    if (yorigin >= cursor->MaxHeight) yorigin = cursor->MaxHeight - 1;

    if (info->Clone) {
	int X0 = 0;
	int Y0 = 0;

	if ((info->CurCloneMode->VDisplay == pScrn->currentMode->VDisplay) &&
	    (info->CurCloneMode->HDisplay == pScrn->currentMode->HDisplay)) {
	    Y2 = y;
	    X2 = x;
	    X0 = pScrn->frameX0;
	    Y0 = pScrn->frameY0;
	} else {
	    if (y < 0)
		Y2 = pScrn->frameY0;

	    if (x < 0)
		X2 = pScrn->frameX0;

	    if (Y2 >= info->CurCloneMode->VDisplay + info->CloneFrameY0) {
		Y0 = Y2 - info->CurCloneMode->VDisplay;
		Y2 = info->CurCloneMode->VDisplay - 1;
	    } else if (Y2 < info->CloneFrameY0) {
		Y0 = Y2;
		Y2 = 0;
	    } else {
		Y2 -= info->CloneFrameY0;
		Y0 = info->CloneFrameY0;
	    }

	    if (X2 >= info->CurCloneMode->HDisplay + info->CloneFrameX0) {
		X0 = X2 - info->CurCloneMode->HDisplay;
		X2 = info->CurCloneMode->HDisplay - 1;
	    } else if (X2 < info->CloneFrameX0) {
		X0 = X2;
		X2 = 0;
	    } else {
		X2 -= info->CloneFrameX0;
		X0 = info->CloneFrameX0;
	    }

	    if (info->CurCloneMode->Flags & V_DBLSCAN)
		Y2 *= 2;
	}

	if ((X0 >= 0 || Y0 >= 0) &&
	    ((info->CloneFrameX0 != X0) || (info->CloneFrameY0 != Y0))) {
	    RADEONDoAdjustFrame(pScrn, X0, Y0, TRUE);
	    info->CloneFrameX0 = X0;
	    info->CloneFrameY0 = Y0;
	}
    }

    if (!info->IsSecondary) {
	OUTREG(RADEON_CUR_HORZ_VERT_OFF,  (RADEON_CUR_LOCK
					   | (xorigin << 16)
					   | yorigin));
	OUTREG(RADEON_CUR_HORZ_VERT_POSN, (RADEON_CUR_LOCK
					   | ((xorigin ? 0 : x) << 16)
					   | (yorigin ? 0 : y)));
	OUTREG(RADEON_CUR_OFFSET, info->cursor_start + info->cursor_buffer + yorigin * stride);
    } else {
	OUTREG(RADEON_CUR2_HORZ_VERT_OFF,  (RADEON_CUR2_LOCK
					    | (xorigin << 16)
					    | yorigin));
	OUTREG(RADEON_CUR2_HORZ_VERT_POSN, (RADEON_CUR2_LOCK
					    | ((xorigin ? 0 : x) << 16)
					    | (yorigin ? 0 : y)));
	OUTREG(RADEON_CUR2_OFFSET,
	       info->cursor_start + info->cursor_buffer + pScrn->fbOffset + yorigin * stride);
    }

    if (info->Clone) {
	xorigin = 0;
	yorigin = 0;
	if (X2 < 0) xorigin = -X2 + 1;
	if (Y2 < 0) yorigin = -Y2 + 1;
	if (xorigin >= cursor->MaxWidth)  xorigin = cursor->MaxWidth - 1;
	if (yorigin >= cursor->MaxHeight) yorigin = cursor->MaxHeight - 1;

	OUTREG(RADEON_CUR2_HORZ_VERT_OFF,  (RADEON_CUR2_LOCK
					    | (xorigin << 16)
					    | yorigin));
	OUTREG(RADEON_CUR2_HORZ_VERT_POSN, (RADEON_CUR2_LOCK
					    | ((xorigin ? 0 : X2) << 16)
					    | (yorigin ? 0 : Y2)));
	OUTREG(RADEON_CUR2_OFFSET,
	       info->cursor_start + info->cursor_buffer + pScrn->fbOffset + yorigin * stride);
    }
}

/* Copy cursor image from `image' to video memory.  RADEONSetCursorPosition
 * will be called after this, so we can ignore xorigin and yorigin.
 */
static void RADEONLoadCursorImage(ScrnInfoPtr pScrn, unsigned char *image)
{
    RADEONInfoPtr  info       = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    CARD32        *s          = (CARD32 *)(pointer)image;
    CARD32        *d          = (CARD32 *)(pointer)(info->FB + info->cursor_start);
    int            y;
    CARD32         save1      = 0;
    CARD32         save2      = 0;

    if(info->cursor_buffer==0){
    	info->cursor_buffer=(info->cursor_end-info->cursor_start+0x03)&~0x3;
	} else {
    	info->cursor_buffer=0;
	}
    d+=info->cursor_buffer/4;

#ifdef ARGB_CURSOR
    info->cursor_argb = FALSE;
#endif
#if X_BYTE_ORDER == X_BIG_ENDIAN
    switch(info->CurrentLayout.pixel_bytes) {
    case 4:
    case 3:
	for (y = 0; y < 64; y++) {
	    P_SWAP32(d,s);
	    d++; s++;
	    P_SWAP32(d,s);
	    d++; s++;
	    P_SWAP32(d,s);
	    d++; s++;
	    P_SWAP32(d,s);
	    d++; s++;
	}
	break;
    case 2:
	for (y = 0; y < 64; y++) {
	    P_SWAP16(d,s);
	    d++; s++;
	    P_SWAP16(d,s);
	    d++; s++;
	    P_SWAP16(d,s);
	    d++; s++;
	    P_SWAP16(d,s);
	    d++; s++;
	}
	break;
    default:
	for (y = 0; y < 64; y++) {
	    *d++ = *s++;
	    *d++ = *s++;
	    *d++ = *s++;
	    *d++ = *s++;
	}
    }
#else
    for (y = 0; y < 64; y++) {
	*d++ = *s++;
	*d++ = *s++;
	*d++ = *s++;
	*d++ = *s++;
    }
#endif

    /* Set the area after the cursor to be all transparent so that we
       won't display corrupted cursors on the screen */
    for (y = 0; y < 64; y++) {
	*d++ = 0xffffffff; /* The AND bits */
	*d++ = 0xffffffff;
	*d++ = 0x00000000; /* The XOR bits */
	*d++ = 0x00000000;
    }

    write_mem_barrier();
    
    if (!info->IsSecondary) {
	save1 = INREG(RADEON_CRTC_GEN_CNTL) & ~(CARD32) (3 << 20);
	#if 0
	OUTREG(RADEON_CRTC_GEN_CNTL, save1 & (CARD32)~RADEON_CRTC_CUR_EN);
	#endif
        OUTREG(RADEON_CUR_OFFSET, INREG(RADEON_CUR_OFFSET)+(info->cursor_buffer-(info->cursor_end-info->cursor_start)));
	OUTREG(RADEON_CRTC_GEN_CNTL, save1);
    }

    if (info->IsSecondary || info->Clone) {
	save2 = INREG(RADEON_CRTC2_GEN_CNTL) & ~(CARD32) (3 << 20);
	#if 0
	OUTREG(RADEON_CRTC2_GEN_CNTL, save2 & (CARD32)~RADEON_CRTC2_CUR_EN);
	#endif
        OUTREG(RADEON_CUR2_OFFSET, INREG(RADEON_CUR_OFFSET)+(info->cursor_buffer-(info->cursor_end-info->cursor_start)));
	OUTREG(RADEON_CRTC2_GEN_CNTL, save2);
    }

}

/* Hide hardware cursor. */
static void RADEONHideCursor(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr  info       = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;

    if (info->IsSecondary || info->Clone)
	OUTREGP(RADEON_CRTC2_GEN_CNTL, 0, ~RADEON_CRTC2_CUR_EN);

    if (!info->IsSecondary)
	OUTREGP(RADEON_CRTC_GEN_CNTL, 0, ~RADEON_CRTC_CUR_EN);
}

/* Show hardware cursor. */
static void RADEONShowCursor(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr  info       = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;

    if (info->IsSecondary || info->Clone)
	OUTREGP(RADEON_CRTC2_GEN_CNTL, RADEON_CRTC2_CUR_EN,
		~RADEON_CRTC2_CUR_EN);

    if (!info->IsSecondary)
	OUTREGP(RADEON_CRTC_GEN_CNTL, RADEON_CRTC_CUR_EN,
		~RADEON_CRTC_CUR_EN);
}

/* Determine if hardware cursor is in use. */
static Bool RADEONUseHWCursor(ScreenPtr pScreen, CursorPtr pCurs)
{
    ScrnInfoPtr    pScrn = xf86Screens[pScreen->myNum];
    RADEONInfoPtr  info  = RADEONPTR(pScrn);

    return info->cursor_start ? TRUE : FALSE;
}

#ifdef ARGB_CURSOR
#include "cursorstr.h"

static Bool RADEONUseHWCursorARGB (ScreenPtr pScreen, CursorPtr pCurs)
{
    ScrnInfoPtr    pScrn = xf86Screens[pScreen->myNum];
    RADEONInfoPtr  info  = RADEONPTR(pScrn);

    if (info->cursor_start &&
	pCurs->bits->height <= 64 && pCurs->bits->width <= 64)
	return TRUE;
    return FALSE;
}

static void RADEONLoadCursorARGB (ScrnInfoPtr pScrn, CursorPtr pCurs)
{
    RADEONInfoPtr  info       = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    CARD32        *d          = (CARD32 *)(pointer)(info->FB + info->cursor_start);
    int            x, y, w, h;
    CARD32         save1      = 0;
    CARD32         save2      = 0;
    CARD32	  *image = pCurs->bits->argb;
    CARD32	  *i;

    if (!image)
	return;	/* XXX can't happen */
    
    if(info->cursor_buffer==0){
    	info->cursor_buffer=(info->cursor_end-info->cursor_start+0x3)&~0x3;
	} else {
    	info->cursor_buffer=0;
	}
    d+=info->cursor_buffer/4;
    
#ifdef ARGB_CURSOR
    info->cursor_argb = TRUE;
#endif
    
    w = pCurs->bits->width;
    if (w > 64)
	w = 64;
    h = pCurs->bits->height;
    if (h > 64)
	h = 64;
    for (y = 0; y < h; y++)
    {
	i = image;
	image += pCurs->bits->width;
	for (x = 0; x < w; x++)
	    *d++ = *i++;
	/* pad to the right with transparent */
	for (; x < 64; x++)
	    *d++ = 0;
    }
    /* pad below with transparent */
    for (; y < 64; y++)
	for (x = 0; x < 64; x++)
	    *d++ = 0;
    
    write_mem_barrier();

    if (!info->IsSecondary) {
	save1 = INREG(RADEON_CRTC_GEN_CNTL) & ~(CARD32) (3 << 20);
	save1 |= (CARD32) 2 << 20;
	#if 0
	OUTREG(RADEON_CRTC_GEN_CNTL, save1 & (CARD32)~RADEON_CRTC_CUR_EN);
	#endif
        OUTREG(RADEON_CUR_OFFSET, INREG(RADEON_CUR_OFFSET)+(info->cursor_buffer-(info->cursor_end-info->cursor_start)));
	OUTREG(RADEON_CRTC_GEN_CNTL, save1);
    }

    if (info->IsSecondary || info->Clone) {
	save2 = INREG(RADEON_CRTC2_GEN_CNTL) & ~(CARD32) (3 << 20);
	save2 |= (CARD32) 2 << 20;
	#if 0
	OUTREG(RADEON_CRTC2_GEN_CNTL, save2 & (CARD32)~RADEON_CRTC2_CUR_EN);
	#endif
        OUTREG(RADEON_CUR2_OFFSET, INREG(RADEON_CUR_OFFSET)+(info->cursor_buffer-(info->cursor_end-info->cursor_start)));
	OUTREG(RADEON_CRTC2_GEN_CNTL, save2);
    }


}

#endif
    

/* Initialize hardware cursor support. */
Bool RADEONCursorInit(ScreenPtr pScreen)
{
    ScrnInfoPtr        pScrn   = xf86Screens[pScreen->myNum];
    RADEONInfoPtr      info    = RADEONPTR(pScrn);
    xf86CursorInfoPtr  cursor;
    FBAreaPtr          fbarea;
    int                width;
    int                height;
    int                size;
    int		       stride = 16;

    if (!(cursor = info->cursor = xf86CreateCursorInfoRec())) return FALSE;

    cursor->MaxWidth          = 64;
    cursor->MaxHeight         = 64;
    cursor->Flags             = (HARDWARE_CURSOR_TRUECOLOR_AT_8BPP

#if X_BYTE_ORDER == X_LITTLE_ENDIAN
				 | HARDWARE_CURSOR_BIT_ORDER_MSBFIRST
#endif
				 | HARDWARE_CURSOR_INVERT_MASK
				 | HARDWARE_CURSOR_AND_SOURCE_WITH_MASK
				 | HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_64
				 | HARDWARE_CURSOR_SWAP_SOURCE_AND_MASK);

    cursor->SetCursorColors   = RADEONSetCursorColors;
    cursor->SetCursorPosition = RADEONSetCursorPosition;
    cursor->LoadCursorImage   = RADEONLoadCursorImage;
    cursor->HideCursor        = RADEONHideCursor;
    cursor->ShowCursor        = RADEONShowCursor;
    cursor->UseHWCursor       = RADEONUseHWCursor;

    size                      = (cursor->MaxWidth/4) * cursor->MaxHeight;
#ifdef ARGB_CURSOR
    cursor->UseHWCursorARGB   = RADEONUseHWCursorARGB;
    cursor->LoadCursorARGB    = RADEONLoadCursorARGB;
    size                      = (cursor->MaxWidth * 4) * cursor->MaxHeight;
#endif
    width                     = pScrn->displayWidth;
    height                    = (size*2 + 1023) / pScrn->displayWidth;
    fbarea                    = xf86AllocateOffscreenArea(pScreen,
							  width,
							  height,
							  stride,
							  NULL,
							  NULL,
							  NULL);

    if (!fbarea) {
	info->cursor_start    = 0;
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "Hardware cursor disabled"
		   " due to insufficient offscreen memory\n");
    } else {
	info->cursor_start    = RADEON_ALIGN((fbarea->box.x1
					      + width * fbarea->box.y1)
					     * info->CurrentLayout.pixel_bytes,
					     stride);
	info->cursor_end      = info->cursor_start + size;
	info->cursor_buffer   = 0;
    }

    RADEONTRACE(("RADEONCursorInit (0x%08x-0x%08x)\n",
		 info->cursor_start, info->cursor_end));

    return xf86InitCursor(pScreen, cursor);
}
