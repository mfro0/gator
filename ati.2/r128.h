/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/ati/r128.h,v 1.17 2001/10/02 19:44:01 herrb Exp $ */
/*
 * Copyright 1999, 2000 ATI Technologies Inc., Markham, Ontario,
 *                      Precision Insight, Inc., Cedar Park, Texas, and
 *                      VA Linux Systems Inc., Fremont, California.
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
 * NON-INFRINGEMENT.  IN NO EVENT SHALL ATI, PRECISION INSIGHT, VA LINUX
 * SYSTEMS AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Authors:
 *   Rickard E. Faith <faith@valinux.com>
 *   Kevin E. Martin <martin@valinux.com>
 *
 */

#ifndef _R128_H_
#define _R128_H_

#include "xf86str.h"

				/* PCI support */
#include "xf86Pci.h"

				/* XAA and Cursor Support */
#include "xaa.h"
#include "xf86Cursor.h"

				/* DDC support */
#include "xf86DDC.h"

				/* Xv support */
#include "xf86xv.h"

				/* DRI support */
#ifdef XF86DRI
#define _XF86DRI_SERVER_
#include "r128_dripriv.h"
#include "dri.h"
#include "GL/glxint.h"
#endif

#define R128_DEBUG    0         /* Turn off debugging output                */
#define R128_TIMEOUT  2000000   /* Fall out of wait loops after this count */
/* bogus value.. 
#define R128_MMIOSIZE 0x80000
*/
#define R128_MMIOSIZE 0x4000

#define R128_VBIOS_SIZE 0x00010000

#if R128_DEBUG
#define R128TRACE(x)                                          \
    do {                                                      \
	ErrorF("(**) %s(%d): ", R128_NAME, pScrn->scrnIndex); \
	ErrorF x;                                             \
    } while (0);
#else
#define R128TRACE(x)
#endif


/* Other macros */
#define R128_ARRAY_SIZE(x)  (sizeof(x)/sizeof(x[0]))
#define R128_ALIGN(x,bytes) (((x) + ((bytes) - 1)) & ~((bytes) - 1))
#define R128PTR(pScrn) ((R128InfoPtr)(pScrn)->driverPrivate)

typedef struct {        /* All values in XCLKS    */
    int  ML;            /* Memory Read Latency    */
    int  MB;            /* Memory Burst Length    */
    int  Trcd;          /* RAS to CAS delay       */
    int  Trp;           /* RAS percentage         */
    int  Twr;           /* Write Recovery         */
    int  CL;            /* CAS Latency            */
    int  Tr2w;          /* Read to Write Delay    */
    int  Rloop;         /* Loop Latency           */
    int  Rloop_fudge;   /* Add to ML to get Rloop */
    char *name;
} R128RAMRec, *R128RAMPtr;

typedef struct {
				/* Common registers */
    CARD32     ovr_clr;
    CARD32     ovr_wid_left_right;
    CARD32     ovr_wid_top_bottom;
    CARD32     ov0_scale_cntl;
    CARD32     mpp_tb_config;
    CARD32     mpp_gp_config;
    CARD32     subpic_cntl;
    CARD32     viph_control;
    CARD32     i2c_cntl_1;
    CARD32     gen_int_cntl;
    CARD32     cap0_trig_cntl;
    CARD32     cap1_trig_cntl;
    CARD32     bus_cntl;
    CARD32     config_cntl;

				/* Other registers to save for VT switches */
    CARD32     dp_datatype;
    CARD32     gen_reset_cntl;
    CARD32     clock_cntl_index;
    CARD32     amcgpio_en_reg;
    CARD32     amcgpio_mask;

				/* CRTC registers */
    CARD32     crtc_gen_cntl;
    CARD32     crtc_ext_cntl;
    CARD32     dac_cntl;
    CARD32     crtc_h_total_disp;
    CARD32     crtc_h_sync_strt_wid;
    CARD32     crtc_v_total_disp;
    CARD32     crtc_v_sync_strt_wid;
    CARD32     crtc_offset;
    CARD32     crtc_offset_cntl;
    CARD32     crtc_pitch;

				/* CRTC2 registers */
    CARD32     crtc2_gen_cntl;

				/* Flat panel registers */
    CARD32     fp_crtc_h_total_disp;
    CARD32     fp_crtc_v_total_disp;
    CARD32     fp_gen_cntl;
    CARD32     fp_h_sync_strt_wid;
    CARD32     fp_horz_stretch;
    CARD32     fp_panel_cntl;
    CARD32     fp_v_sync_strt_wid;
    CARD32     fp_vert_stretch;
    CARD32     lvds_gen_cntl;
    CARD32     tmds_crc;
    CARD32     tmds_transmitter_cntl;

				/* Computed values for PLL */
    CARD32     dot_clock_freq;
    CARD32     pll_output_freq;
    int        feedback_div;
    int        post_div;

				/* PLL registers */
    CARD32     ppll_ref_div;
    CARD32     ppll_div_3;
    CARD32     htotal_cntl;

				/* DDA register */
    CARD32     dda_config;
    CARD32     dda_on_off;

				/* Pallet */
    Bool       palette_valid;
    CARD32     palette[256];
} R128SaveRec, *R128SavePtr;

typedef struct {
    CARD16        reference_freq;
    CARD16        reference_div;
    CARD32        min_pll_freq;
    CARD32        max_pll_freq;
    CARD16        xclk;
} R128PLLRec, *R128PLLPtr;

typedef struct {
    int                bitsPerPixel;
    int                depth;
    int                displayWidth;
    int                pixel_code;
    int                pixel_bytes;
    DisplayModePtr     mode;
} R128FBLayout;

typedef struct {
    EntityInfoPtr     pEnt;
    pciVideoPtr       PciInfo;
    PCITAG            PciTag;
    int               Chipset;
    Bool              Primary;

    Bool              FBDev;

    unsigned long     LinearAddr;   /* Frame buffer physical address         */
    unsigned long     MMIOAddr;     /* MMIO region physical address          */
    unsigned long     BIOSAddr;     /* BIOS physical address                 */

    unsigned char     *MMIO;        /* Map of MMIO region                    */
    unsigned char     *FB;          /* Map of frame buffer                   */

    CARD32            MemCntl;
    CARD32            BusCntl;
    unsigned long     FbMapSize;    /* Size of frame buffer, in bytes        */
    int               Flags;        /* Saved copy of mode flags              */

    CARD8             BIOSDisplay;  /* Device the BIOS is set to display to  */

    Bool              HasPanelRegs; /* Current chip can connect to a FP      */
    CARD8             *VBIOS;       /* Video BIOS for mode validation on FPs */
    int               FPBIOSstart;  /* Start of the flat panel info          */

				/* Computed values for FPs */
    int               PanelXRes;
    int               PanelYRes;
    int               HOverPlus;
    int               HSyncWidth;
    int               HBlank;
    int               VOverPlus;
    int               VSyncWidth;
    int               VBlank;
    int               PanelPwrDly;

    R128PLLRec        pll;
    R128RAMPtr        ram;

    R128SaveRec       SavedReg;     /* Original (text) mode                  */
    R128SaveRec       ModeReg;      /* Current mode                          */
    Bool              (*CloseScreen)(int, ScreenPtr);
    void              (*BlockHandler)(int, pointer, pointer, pointer);

    Bool              PaletteSavedOnVT; /* Palette saved on last VT switch   */

    XAAInfoRecPtr     accel;
    Bool              accelOn;
    xf86CursorInfoPtr cursor;
    unsigned long     cursor_start;
    unsigned long     cursor_end;

    /*
     * XAAForceTransBlit is used to change the behavior of the XAA
     * SetupForScreenToScreenCopy function, to make it DGA-friendly.
     */
    Bool              XAAForceTransBlit;

    int               fifo_slots;   /* Free slots in the FIFO (64 max)       */
    int               pix24bpp;     /* Depth of pixmap for 24bpp framebuffer */
    Bool              dac6bits;     /* Use 6 bit DAC?                        */

				/* Computed values for Rage 128 */
    int               pitch;
    int               datatype;
    CARD32            dp_gui_master_cntl;

				/* Saved values for ScreenToScreenCopy */
    int               xdir;
    int               ydir;

				/* ScanlineScreenToScreenColorExpand support */
    unsigned char     *scratch_buffer[1];
    unsigned char     *scratch_save;
    int               scanline_x;
    int               scanline_y;
    int               scanline_h;
    int               scanline_h_w;
    int               scanline_words;
    int               scanline_direct;
    int               scanline_bpp; /* Only used for ImageWrite */

    DGAModePtr        DGAModes;
    int               numDGAModes;
    Bool              DGAactive;
    int               DGAViewportStatus;
    DGAFunctionRec    DGAFuncs;

    R128FBLayout      CurrentLayout;
#ifdef XF86DRI
    Bool              directRenderingEnabled;
    DRIInfoPtr        pDRIInfo;
    int               drmFD;
    drmContext        drmCtx;
    int               numVisualConfigs;
    __GLXvisualConfig *pVisualConfigs;
    R128ConfigPrivPtr pVisualConfigsPriv;

    drmHandle         fbHandle;

    drmSize           registerSize;
    drmHandle         registerHandle;

    Bool              IsPCI;            /* Current card is a PCI card */
    drmSize           pciSize;
    drmHandle         pciMemHandle;
    unsigned char     *PCI;             /* Map */

    drmSize           agpSize;
    drmHandle         agpMemHandle;     /* Handle from drmAgpAlloc */
    unsigned long     agpOffset;
    unsigned char     *AGP;             /* Map */
    int               agpMode;

    Bool              CCEInUse;         /* CCE is currently active */
    int               CCEMode;          /* CCE mode that server/clients use */
    int               CCEFifoSize;      /* Size of the CCE command FIFO */
    Bool              CCESecure;        /* CCE security enabled */
    int               CCEusecTimeout;   /* CCE timeout in usecs */

				/* CCE ring buffer data */
    unsigned long     ringStart;        /* Offset into AGP space */
    drmHandle         ringHandle;       /* Handle from drmAddMap */
    drmSize           ringMapSize;      /* Size of map */
    int               ringSize;         /* Size of ring (in MB) */
    unsigned char     *ring;            /* Map */
    int               ringSizeLog2QW;

    unsigned long     ringReadOffset;   /* Offset into AGP space */
    drmHandle         ringReadPtrHandle; /* Handle from drmAddMap */
    drmSize           ringReadMapSize;  /* Size of map */
    unsigned char     *ringReadPtr;     /* Map */

				/* CCE vertex/indirect buffer data */
    unsigned long     bufStart;        /* Offset into AGP space */
    drmHandle         bufHandle;       /* Handle from drmAddMap */
    drmSize           bufMapSize;      /* Size of map */
    int               bufSize;         /* Size of buffers (in MB) */
    unsigned char     *buf;            /* Map */
    int               bufNumBufs;      /* Number of buffers */
    drmBufMapPtr      buffers;         /* Buffer map */

				/* CCE AGP Texture data */
    unsigned long     agpTexStart;      /* Offset into AGP space */
    drmHandle         agpTexHandle;     /* Handle from drmAddMap */
    drmSize           agpTexMapSize;    /* Size of map */
    int               agpTexSize;       /* Size of AGP tex space (in MB) */
    unsigned char     *agpTex;          /* Map */
    int               log2AGPTexGran;

				/* CCE 2D accleration */
    drmBufPtr         indirectBuffer;
    int               indirectStart;

				/* DRI screen private data */
    int               fbX;
    int               fbY;
    int               backX;
    int               backY;
    int               depthX;
    int               depthY;

    int               frontOffset;
    int               frontPitch;
    int               backOffset;
    int               backPitch;
    int               depthOffset;
    int               depthPitch;
    int               spanOffset;
    int               textureOffset;
    int               textureSize;
    int               log2TexGran;

				/* Saved scissor values */
    CARD32            sc_left;
    CARD32            sc_right;
    CARD32            sc_top;
    CARD32            sc_bottom;

    CARD32            re_top_left;
    CARD32            re_width_height;

    CARD32            aux_sc_cntl;
#endif

    
    XF86VideoAdaptorPtr adaptor;
    void              (*VideoTimerCallback)(ScrnInfoPtr, Time);
    int               videoKey;
    Bool              forceXvProbing;
    int		      RageTheatreCrystal;
    int               RageTheatreTunerPort;
    int               RageTheatreCompositePort;
    int               RageTheatreSVideoPort;
    int               tunerType;
    Bool              showCache;
    OptionInfoPtr     Options;

    Bool              isDFP;
    Bool              isPro2;
    I2CBusPtr         pI2CBus;
    CARD32            DDCReg;

} R128InfoRec, *R128InfoPtr;

#define R128WaitForFifo(pScrn, entries)                                      \
do {                                                                         \
    if (info->fifo_slots < entries) R128WaitForFifoFunction(pScrn, entries); \
    info->fifo_slots -= entries;                                             \
} while (0)

extern void        R128WaitForFifoFunction(ScrnInfoPtr pScrn, int entries);
extern void        R128WaitForIdle(ScrnInfoPtr pScrn);
extern void        R128EngineReset(ScrnInfoPtr pScrn);
extern void        R128EngineFlush(ScrnInfoPtr pScrn);

extern unsigned    R128INPLL(ScrnInfoPtr pScrn, int addr);
extern void        R128WaitForVerticalSync(ScrnInfoPtr pScrn);

extern Bool        R128AccelInit(ScreenPtr pScreen);
extern void        R128EngineInit(ScrnInfoPtr pScrn);
extern Bool        R128CursorInit(ScreenPtr pScreen);
extern Bool        R128DGAInit(ScreenPtr pScreen);

extern int         R128MinBits(int val);

extern void        R128InitVideo(ScreenPtr pScreen);
#ifdef XvExtension
extern void	   R128ShutdownVideo(ScrnInfoPtr pScrn);
extern void        R128LeaveVT_Video(ScrnInfoPtr pScrn);
extern void        R128EnterVT_Video(ScrnInfoPtr pScrn);
#endif

#ifdef XF86DRI
extern Bool        R128DRIScreenInit(ScreenPtr pScreen);
extern void        R128DRICloseScreen(ScreenPtr pScreen);
extern Bool        R128DRIFinishScreenInit(ScreenPtr pScreen);

#define R128CCE_START(pScrn, info)					\
do {									\
    int _ret = drmR128StartCCE(info->drmFD);				\
    if (_ret) {								\
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,				\
		   "%s: CCE start %d\n", __FUNCTION__, _ret);		\
    }									\
} while (0)

#define R128CCE_STOP(pScrn, info)					\
do {									\
    int _ret = drmR128StopCCE(info->drmFD);				\
    if (_ret) {								\
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,				\
		   "%s: CCE stop %d\n", __FUNCTION__, _ret);		\
    }									\
} while (0)

#define R128CCE_RESET(pScrn, info)					\
do {									\
    if (info->directRenderingEnabled					\
	&& R128CCE_USE_RING_BUFFER(info->CCEMode)) {			\
	int _ret = drmR128ResetCCE(info->drmFD);			\
	if (_ret) {							\
	    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,			\
		       "%s: CCE reset %d\n", __FUNCTION__, _ret);	\
	}								\
    }									\
} while (0)

#endif

#ifdef XF86DRI
extern drmBufPtr   R128CCEGetBuffer(ScrnInfoPtr pScrn);
#endif
extern void        R128CCEFlushIndirect(ScrnInfoPtr pScrn);
extern void        R128CCEReleaseIndirect(ScrnInfoPtr pScrn);
extern void        R128CCEWaitForIdle(ScrnInfoPtr pScrn);


#define CCE_PACKET0( reg, n )						\
	(R128_CCE_PACKET0 | ((n) << 16) | ((reg) >> 2))
#define CCE_PACKET1( reg0, reg1 )					\
	(R128_CCE_PACKET1 | (((reg1) >> 2) << 11) | ((reg0) >> 2))
#define CCE_PACKET2()							\
	(R128_CCE_PACKET2)
#define CCE_PACKET3( pkt, n )						\
	(R128_CCE_PACKET3 | (pkt) | ((n) << 16))


#define R128_VERBOSE	0

#define RING_LOCALS	CARD32 *__head; int __count;
#define RING_THRESHOLD	256

#define R128CCE_REFRESH(pScrn, info)					\
do {									\
   if ( R128_VERBOSE ) {						\
      xf86DrvMsg( pScrn->scrnIndex, X_INFO, "REFRESH( %d ) in %s\n",	\
		  !info->CCEInUse , __FUNCTION__ );			\
   }									\
   if ( !info->CCEInUse ) {						\
      R128CCEWaitForIdle(pScrn);       					\
      BEGIN_RING( 6 );							\
      OUT_RING_REG( R128_RE_TOP_LEFT,     info->re_top_left );		\
      OUT_RING_REG( R128_RE_WIDTH_HEIGHT, info->re_width_height );	\
      OUT_RING_REG( R128_AUX_SC_CNTL,     info->aux_sc_cntl );		\
      ADVANCE_RING();							\
      info->CCEInUse = TRUE;						\
   }									\
} while (0)

#define BEGIN_RING( n ) do {						\
   if ( R128_VERBOSE ) {						\
      xf86DrvMsg( pScrn->scrnIndex, X_INFO,				\
		  "BEGIN_RING( %d ) in %s\n", n, __FUNCTION__ );	\
   }									\
   if ( !info->indirectBuffer ) {					\
      info->indirectBuffer = R128CCEGetBuffer( pScrn );		\
      info->indirectStart = 0;						\
   } else if ( info->indirectBuffer->used - info->indirectStart +	\
	       (n) * (int)sizeof(CARD32) > RING_THRESHOLD ) {		\
      R128CCEFlushIndirect( pScrn );					\
   }									\
   __head = (pointer)((char *)info->indirectBuffer->address +		\
		       info->indirectBuffer->used);			\
   __count = 0;								\
} while (0)

#define ADVANCE_RING() do {						\
   if ( R128_VERBOSE ) {						\
      xf86DrvMsg( pScrn->scrnIndex, X_INFO,				\
		  "ADVANCE_RING() used: %d+%d=%d/%d\n",			\
		  info->indirectBuffer->used - info->indirectStart,	\
		  __count * sizeof(CARD32),				\
		  info->indirectBuffer->used - info->indirectStart +	\
		  __count * sizeof(CARD32),				\
		  RING_THRESHOLD );					\
   }									\
   info->indirectBuffer->used += __count * (int)sizeof(CARD32);		\
} while (0)

#define OUT_RING( x ) do {						\
   if ( R128_VERBOSE ) {						\
      xf86DrvMsg( pScrn->scrnIndex, X_INFO,				\
		  "   OUT_RING( 0x%08x )\n", (unsigned int)(x) );	\
   }									\
   MMIO_OUT32(&__head[__count++], 0, (x));				\
} while (0)

#define OUT_RING_REG( reg, val )					\
do {									\
   OUT_RING( CCE_PACKET0( reg, 0 ) );					\
   OUT_RING( val );							\
} while (0)

#define FLUSH_RING()							\
do {									\
   if ( R128_VERBOSE )							\
      xf86DrvMsg( pScrn->scrnIndex, X_INFO,				\
		  "FLUSH_RING in %s\n", __FUNCTION__ );			\
   if ( info->indirectBuffer ) {					\
      R128CCEFlushIndirect( pScrn );					\
   }									\
} while (0)

#endif
