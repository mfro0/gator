/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/ati/radeon_driver.c,v 1.42 2001/11/14 16:50:44 alanh Exp $ */
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
 *   Kevin E. Martin <martin@valinux.com>
 *   Rickard E. Faith <faith@valinux.com>
 *   Alan Hourihane <ahourihane@valinux.com>
 *
 * Credits:
 *
 *   Thanks to Ani Joshi <ajoshi@shell.unixbox.com> for providing source
 *   code to his Radeon driver.  Portions of this file are based on the
 *   initialization code for that driver.
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
 * This server does not yet support these XFree86 4.0 features:
 * !!!! FIXME !!!!
 *   DDC1 & DDC2
 *   shadowfb
 *   overlay planes
 *
 * Modified by Marc Aurele La France (tsi@xfree86.org) for ATI driver merge.
 */

				/* Driver data structures */
#include "radeon.h"
#include "radeon_probe.h"
#include "radeon_reg.h"
#include "radeon_version.h"

#ifdef XF86DRI
#define _XF86DRI_SERVER_
#include "radeon_dri.h"
#include "radeon_sarea.h"
#endif

#define USE_FB                  /* not until overlays */
#ifdef USE_FB
#include "fb.h"
#else

				/* CFB support */
#define PSZ 8
#include "cfb.h"
#undef PSZ
#include "cfb16.h"
#include "cfb24.h"
#include "cfb32.h"
#include "cfb24_32.h"
#endif

				/* colormap initialization */
#include "micmap.h"
#include "dixstruct.h"

				/* X and server generic header files */
#include "xf86.h"
#include "xf86_OSproc.h"
#include "xf86PciInfo.h"
#include "xf86RAC.h"
#include "xf86cmap.h"
#include "vbe.h"

				/* fbdevhw * vgaHW definitions */
#include "fbdevhw.h"
#include "vgaHW.h"

#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif

				/* Forward definitions for driver functions */
static Bool RADEONCloseScreen(int scrnIndex, ScreenPtr pScreen);
static Bool RADEONSaveScreen(ScreenPtr pScreen, int mode);
static void RADEONSave(ScrnInfoPtr pScrn);
static void RADEONRestore(ScrnInfoPtr pScrn);
static Bool RADEONModeInit(ScrnInfoPtr pScrn, DisplayModePtr mode);
static void RADEONDisplayPowerManagementSet(ScrnInfoPtr pScrn,
					    int PowerManagementMode,
					    int flags);
static Bool RADEONEnterVTFBDev(int scrnIndex, int flags);
static void RADEONLeaveVTFBDev(int scrnIndex, int flags);

typedef enum {
    OPTION_NOACCEL,
    OPTION_SW_CURSOR,
    OPTION_DAC_6BIT,
    OPTION_DAC_8BIT,
#ifdef XF86DRI
    OPTION_IS_PCI,
    OPTION_CP_PIO,
    OPTION_NO_SECURITY,
    OPTION_USEC_TIMEOUT,
    OPTION_AGP_MODE,
    OPTION_AGP_SIZE,
    OPTION_RING_SIZE,
    OPTION_BUFFER_SIZE,
    OPTION_DEPTH_MOVE,
#endif
    OPTION_CRT_SCREEN,
    OPTION_PANEL_SIZE,
#ifdef XvExtension
    OPTION_VIDEO_KEY,
#endif
    OPTION_FBDEV
} RADEONOpts;

const OptionInfoRec RADEONOptions[] = {
    { OPTION_NOACCEL,      "NoAccel",          OPTV_BOOLEAN, {0}, FALSE },
    { OPTION_SW_CURSOR,    "SWcursor",         OPTV_BOOLEAN, {0}, FALSE },
    { OPTION_DAC_6BIT,     "Dac6Bit",          OPTV_BOOLEAN, {0}, FALSE },
    { OPTION_DAC_8BIT,     "Dac8Bit",          OPTV_BOOLEAN, {0}, TRUE  },
#ifdef XF86DRI
    { OPTION_IS_PCI,       "ForcePCIMode",     OPTV_BOOLEAN, {0}, FALSE },
    { OPTION_CP_PIO,       "CPPIOMode",        OPTV_BOOLEAN, {0}, FALSE },
    { OPTION_USEC_TIMEOUT, "CPusecTimeout",    OPTV_INTEGER, {0}, FALSE },
    { OPTION_AGP_MODE,     "AGPMode",          OPTV_INTEGER, {0}, FALSE },
    { OPTION_AGP_SIZE,     "AGPSize",          OPTV_INTEGER, {0}, FALSE },
    { OPTION_RING_SIZE,    "RingSize",         OPTV_INTEGER, {0}, FALSE },
    { OPTION_BUFFER_SIZE,  "BufferSize",       OPTV_INTEGER, {0}, FALSE },
    { OPTION_DEPTH_MOVE,   "EnableDepthMoves", OPTV_BOOLEAN, {0}, FALSE },
#endif
    { OPTION_CRT_SCREEN,   "crt_screen",       OPTV_BOOLEAN, {0}, FALSE},
    { OPTION_PANEL_SIZE,   "PanelSize",        OPTV_ANYSTR,  {0}, FALSE },
#ifdef XvExtension
    { OPTION_VIDEO_KEY, "VideoKey",      OPTV_INTEGER, {0}, FALSE },
#endif
    { OPTION_FBDEV,        "UseFBDev",         OPTV_BOOLEAN, {0}, FALSE },
    { -1,                  NULL,               OPTV_NONE,    {0}, FALSE }
};

RADEONRAMRec RADEONRAM[] = {    /* Memory Specifications
				   From Radeon Manual */
    { 4, 4, 1, 2, 1, 2, 1, 16, 12, "64-bit SDR SDRAM" },
    { 4, 4, 3, 3, 2, 3, 1, 16, 12, "64-bit DDR SDRAM" },
};

static const char *vgahwSymbols[] = {
    "vgaHWFreeHWRec",
    "vgaHWGetHWRec",
    "vgaHWGetIndex",
    "vgaHWLock",
    "vgaHWRestore",
    "vgaHWSave",
    "vgaHWUnlock",
    "vgaHWGetIOBase",
    NULL
};

static const char *fbdevHWSymbols[] = {
    "fbdevHWInit",
    "fbdevHWUseBuildinMode",

    "fbdevHWGetVidmem",

    /* colormap */
    "fbdevHWLoadPalette",
    /* ScrnInfo hooks */
    "fbdevHWAdjustFrame",
    "fbdevHWEnterVT",
    "fbdevHWLeaveVT",
    "fbdevHWModeInit",
    "fbdevHWRestore",
    "fbdevHWSave",
    "fbdevHWSwitchMode",
    "fbdevHWValidMode",

    "fbdevHWMapMMIO",
    "fbdevHWMapVidmem",
    "fbdevHWUnmapMMIO",
    "fbdevHWUnmapVidmem",

    NULL
};

static const char *ddcSymbols[] = {
    "xf86PrintEDID",
    "xf86DoEDID_DDC1",
    "xf86DoEDID_DDC2",
    NULL
};

#ifdef USE_FB
static const char *fbSymbols[] = {
    "fbScreenInit",
    "fbPictureInit",
    NULL
};
#else
static const char *cfbSymbols[] = {
    "cfbScreenInit",
    "cfb16ScreenInit",
    "cfb24ScreenInit",
    "cfb32ScreenInit",
    "cfb24_32ScreenInit",
    NULL
};
#endif

static const char *xaaSymbols[] = {
    "XAACreateInfoRec",
    "XAADestroyInfoRec",
    "XAAInit",
    NULL
};

#if 0
static const char *xf8_32bppSymbols[] = {
    "xf86Overlay8Plus32Init",
    NULL
};
#endif

static const char *ramdacSymbols[] = {
    "xf86CreateCursorInfoRec",
    "xf86DestroyCursorInfoRec",
    "xf86InitCursor",
    NULL
};

#ifdef XF86DRI
static const char *drmSymbols[] = {
    "drmAddBufs",
    "drmAddMap",
    "drmAgpAcquire",
    "drmAgpAlloc",
    "drmAgpBind",
    "drmAgpDeviceId",
    "drmAgpEnable",
    "drmAgpFree",
    "drmAgpGetMode",
    "drmAgpRelease",
    "drmAgpUnbind",
    "drmAgpVendorId",
    "drmDMA",
    "drmFreeVersion",
    "drmGetVersion",
    "drmMap",
    "drmMapBufs",
    "drmRadeonCleanupCP",
    "drmRadeonClear",
    "drmRadeonFlushIndirectBuffer",
    "drmRadeonInitCP",
    "drmRadeonResetCP",
    "drmRadeonStartCP",
    "drmRadeonStopCP",
    "drmRadeonWaitForIdleCP",
    "drmScatterGatherFree"
    "drmUnmap",
    "drmUnmapBufs",
    NULL
};

static const char *driSymbols[] = {
    "DRICloseScreen",
    "DRICreateInfoRec",
    "DRIDestroyInfoRec",
    "DRIFinishScreenInit",
    "DRIGetContext",
    "DRIGetSAREAPrivate",
    "DRILock",
    "DRIQueryVersion",
    "DRIScreenInit",
    "DRIUnlock",
    "GlxSetVisualConfigs",
    NULL
};
#endif

static const char *vbeSymbols[] = {
    "VBEInit",
    "vbeDoEDID",
    NULL
};

static const char *int10Symbols[] = {
    "xf86InitInt10",
    "xf86FreeInt10",
    "xf86int10Addr",
    NULL
};

extern int gRADEONEntityIndex;

#if !defined(__alpha__)
# define RADEONPreInt10Save(s, r1, r2)
# define RADEONPostInt10Check(s, r1, r2)
#else /* __alpha__ */
static void
RADEONSaveRegsZapMemCntl(ScrnInfoPtr pScrn, CARD32 *MEM_CNTL, CARD32 *MEMSIZE)
{
    RADEONInfoPtr info = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO;
    int mapped = 0;

    /*
     * First make sure we have the pci and mmio info and that mmio is mapped
     */
    if (!info->PciInfo)
	info->PciInfo = xf86GetPciInfoForEntity(info->pEnt->index);
    if (!info->PciTag)
	info->PciTag = pciTag(info->PciInfo->bus, info->PciInfo->device,
			      info->PciInfo->func);
    if (!info->MMIOAddr) 
	info->MMIOAddr = info->PciInfo->memBase[2] & 0xffffff00;
    if (!info->MMIO) {
	RADEONMapMMIO(pScrn);
	mapped = 1;
    }
    RADEONMMIO = info->MMIO;

    /*
     * Save the values and zap MEM_CNTL
     */
    *MEM_CNTL = INREG(RADEON_MEM_CNTL);
    *MEMSIZE = INREG(RADEON_CONFIG_MEMSIZE);
    OUTREG(RADEON_MEM_CNTL, 0);

    /*
     * Unmap mmio space if we mapped it
     */
    if (mapped) 
	RADEONUnmapMMIO(pScrn);
}

static void
RADEONCheckRegs(ScrnInfoPtr pScrn, CARD32 Saved_MEM_CNTL, CARD32 Saved_MEMSIZE)
{
    RADEONInfoPtr info = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO;
    CARD32 MEM_CNTL;
    int mapped = 0;

    /*
     * If we don't have a valid (non-zero) saved MEM_CNTL, get out now
     */
    if (!Saved_MEM_CNTL)
	return;

    /*
     * First make sure that mmio is mapped
     */
    if (!info->MMIO) {
	RADEONMapMMIO(pScrn);
	mapped = 1;
    }
    RADEONMMIO = info->MMIO;

    /*
     * If either MEM_CNTL is currently zero or inconistent (configured for 
     * two channels with the two channels configured differently), restore
     * the saved registers.
     */
    MEM_CNTL = INREG(RADEON_MEM_CNTL);
    if (!MEM_CNTL || 
	((MEM_CNTL & 1) && 
	 (((MEM_CNTL >> 8) & 0xff) != ((MEM_CNTL >> 24) & 0xff)))) {
	/*
	 * Restore the saved registers
	 */
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "Restoring MEM_CNTL (%08x), setting to %08x\n",
		   MEM_CNTL, Saved_MEM_CNTL);
	OUTREG(RADEON_MEM_CNTL, Saved_MEM_CNTL);

	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "Restoring CONFIG_MEMSIZE (%08x), setting to %08x\n",
		   INREG(RADEON_CONFIG_MEMSIZE), Saved_MEMSIZE);
	OUTREG(RADEON_CONFIG_MEMSIZE, Saved_MEMSIZE);
    }

    /*
     * Unmap mmio space if we mapped it
     */
    if (mapped) 
	RADEONUnmapMMIO(pScrn);
}

# define RADEONPreInt10Save(s, r1, r2) 		\
    RADEONSaveRegsZapMemCntl((s), (r1), (r2))
# define RADEONPostInt10Check(s, r1, r2)	\
    RADEONCheckRegs((s), (r1), (r2))
#endif /* __alpha__ */

static const char *i2cSymbols[] = {
    "xf86CreateI2CBusRec",
    "xf86I2CBusInit",
    "xf86DestroyI2CBus",
    "xf86I2CWriteRead",
    NULL
};

/* Allocate our private RADEONInfoRec. */
static Bool RADEONGetRec(ScrnInfoPtr pScrn)
{
    if (pScrn->driverPrivate) return TRUE;

    pScrn->driverPrivate = xnfcalloc(sizeof(RADEONInfoRec), 1);
    return TRUE;
}

/* Free our private RADEONInfoRec. */
static void RADEONFreeRec(ScrnInfoPtr pScrn)
{
    if (!pScrn || !pScrn->driverPrivate) return;
    xfree(pScrn->driverPrivate);
    pScrn->driverPrivate = NULL;
}

/* Memory map the MMIO region.  Used during pre-init and by RADEONMapMem,
   below. */
static Bool RADEONMapMMIO(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info          = RADEONPTR(pScrn);

    if (info->FBDev) {
	info->MMIO = fbdevHWMapMMIO(pScrn);
    } else {
	info->MMIO = xf86MapPciMem(pScrn->scrnIndex,
				   VIDMEM_MMIO | VIDMEM_READSIDEEFFECT,
				   info->PciTag,
				   info->MMIOAddr,
				   RADEON_MMIOSIZE);
    }

    if (!info->MMIO) return FALSE;
    return TRUE;
}

/* Unmap the MMIO region.  Used during pre-init and by RADEONUnmapMem,
   below. */
static Bool RADEONUnmapMMIO(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info          = RADEONPTR(pScrn);

    if (info->FBDev)
	fbdevHWUnmapMMIO(pScrn);
    else {
	xf86UnMapVidMem(pScrn->scrnIndex, info->MMIO, RADEON_MMIOSIZE);
    }
    info->MMIO = NULL;
    return TRUE;
}

/* Memory map the frame buffer.  Used by RADEONMapMem, below. */
static Bool RADEONMapFB(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info          = RADEONPTR(pScrn);

    if (info->FBDev) {
	info->FB = fbdevHWMapVidmem(pScrn);
    } else {
	info->FB = xf86MapPciMem(pScrn->scrnIndex,
				 VIDMEM_FRAMEBUFFER,
				 info->PciTag,
				 info->LinearAddr,
				 info->FbMapSize);
    }

    if (!info->FB) return FALSE;
    return TRUE;
}

/* Unmap the frame buffer.  Used by RADEONUnmapMem, below. */
static Bool RADEONUnmapFB(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info          = RADEONPTR(pScrn);

    if (info->FBDev)
	fbdevHWUnmapVidmem(pScrn);
    else
	xf86UnMapVidMem(pScrn->scrnIndex, info->FB, info->FbMapSize);
    info->FB = NULL;
    return TRUE;
}

/* Memory map the MMIO region and the frame buffer. */
static Bool RADEONMapMem(ScrnInfoPtr pScrn)
{
    if (!RADEONMapMMIO(pScrn)) return FALSE;
    if (!RADEONMapFB(pScrn)) {
	RADEONUnmapMMIO(pScrn);
	return FALSE;
    }
    return TRUE;
}

/* Unmap the MMIO region and the frame buffer. */
static Bool RADEONUnmapMem(ScrnInfoPtr pScrn)
{
    if (!RADEONUnmapMMIO(pScrn) || !RADEONUnmapFB(pScrn)) return FALSE;
    return TRUE;
}

/* Read PLL information */
unsigned RADEONINPLL(ScrnInfoPtr pScrn, int addr)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;

#if !RADEON_ATOMIC_UPDATE
    while ( (INREG8(RADEON_CLOCK_CNTL_INDEX) & 0xbf) != addr) {
#endif
	OUTREG8(RADEON_CLOCK_CNTL_INDEX, addr & 0x3f);
#if !RADEON_ATOMIC_UPDATE
    }
#endif
    return INREG(RADEON_CLOCK_CNTL_DATA);
}

#if 0
/* Read PAL information (only used for debugging). */
static int RADEONINPAL(int idx)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;

    OUTREG(RADEON_PALETTE_INDEX, idx << 16);
    return INREG(RADEON_PALETTE_DATA);
}
#endif

/* Wait for vertical sync. */
void RADEONWaitForVerticalSync(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    int           i;

    OUTREG(RADEON_GEN_INT_STATUS, RADEON_VSYNC_INT_AK);
    for (i = 0; i < RADEON_TIMEOUT; i++) {
	if (INREG(RADEON_GEN_INT_STATUS) & RADEON_VSYNC_INT) break;
    }
}

/* Blank screen. */
static void RADEONBlank(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;

    if(!info->IsSecondary)
    {
        switch(info->DisplayType)
        {
        case MT_LCD:
            OUTREGP(RADEON_LVDS_GEN_CNTL, RADEON_LVDS_DISPLAY_DIS,
                 ~RADEON_LVDS_DISPLAY_DIS);
        case MT_CRT:
        case MT_DFP:
    OUTREGP(RADEON_CRTC_EXT_CNTL,
	    RADEON_CRTC_DISPLAY_DIS |
	    RADEON_CRTC_VSYNC_DIS |
	    RADEON_CRTC_HSYNC_DIS,
	  ~(RADEON_CRTC_DISPLAY_DIS |
	    RADEON_CRTC_VSYNC_DIS |
	    RADEON_CRTC_HSYNC_DIS));
            break;
        case MT_NONE:
        default:
           break;
        }
    }
    else
    {
            OUTREGP(RADEON_CRTC2_GEN_CNTL, 
                 RADEON_CRTC2_DISP_DIS |
	         RADEON_CRTC2_VSYNC_DIS |
	         RADEON_CRTC2_HSYNC_DIS,
	       ~(RADEON_CRTC2_DISP_DIS |
	         RADEON_CRTC2_VSYNC_DIS |
	         RADEON_CRTC2_HSYNC_DIS));
    }
}

/* Unblank screen. */
static void RADEONUnblank(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    if(!info->IsSecondary)
    {
        switch(info->DisplayType)
        {
        case MT_LCD:
            OUTREGP(RADEON_LVDS_GEN_CNTL, 0,
                 ~RADEON_LVDS_DISPLAY_DIS);
        case MT_CRT:
        case MT_DFP:
            OUTREGP(RADEON_CRTC_EXT_CNTL, 
               RADEON_CRTC_CRT_ON,
	  ~(RADEON_CRTC_DISPLAY_DIS |
	    RADEON_CRTC_VSYNC_DIS |
	    RADEON_CRTC_HSYNC_DIS));
            break;
        case MT_NONE:
        default:
            break;
        }
    }
    else
    {
        switch(info->DisplayType)
        {
        case MT_LCD:
        case MT_DFP:
        case MT_CRT:
            OUTREGP(RADEON_CRTC2_GEN_CNTL,
                0,
	       ~(RADEON_CRTC2_DISP_DIS |
	         RADEON_CRTC2_VSYNC_DIS |
	         RADEON_CRTC2_HSYNC_DIS));
            break;
        case MT_NONE:
        default:
            break;
        }
    }
}

/***Used to turn particular crtc or dac off 
    Not tested, comment out for now*/
#if 0
static void RADEONSetDisplayOff(ScrnInfoPtr pScrn, int crtc, int dac)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    CARD32 tmp;

    if(crtc == 0)   /* primary crtc*/
    {
        tmp = INREG(RADEON_CRTC_EXT_CNTL);
        tmp |= RADEON_CRTC_DISPLAY_DIS | RADEON_CRTC_SYNC_TRISTAT;
        OUTREG(RADEON_CRTC_EXT_CNTL, tmp);
        tmp &= ~(RADEON_CRTC_DISPLAY_DIS | RADEON_CRTC_CRT_ON);
        OUTREG(RADEON_CRTC_EXT_CNTL, tmp);
        /*TODO update BIOS scratch register*/   
    }
    else            /* secondary crtc*/
    { 
        tmp = INREG(RADEON_CRTC2_GEN_CNTL);
        tmp |= RADEON_CRTC2_SYNC_TRISTAT | RADEON_CRTC2_DISP_DIS;
        OUTREG(RADEON_CRTC2_GEN_CNTL, tmp);
        tmp &= ~(RADEON_CRTC2_DISP_DIS | RADEON_CRTC2_CRT2_ON);
        OUTREG(RADEON_CRTC2_GEN_CNTL, tmp);
        /*TODO update BIOS scratch register*/   
    }
    if(dac == 0)
    {
        tmp = INREG(RADEON_DAC_CNTL);          
        tmp |= RADEON_DAC_PDWN;
        OUTREG(RADEON_DAC_CNTL, tmp);
    }
    else
    {
        tmp = INREG(RADEON_TV_DAC_CNTL);
        if(RADEON_TV_DAC_STD_MASK & tmp)
        {
            tmp |= RADEON_TV_DAC_RDACPD | RADEON_TV_DAC_GDACPD
                | RADEON_TV_DAC_BDACPD;    
            OUTREG(RADEON_TV_DAC_CNTL, tmp);
        }
    }
}
#endif

/* Compute log base 2 of val. */
int RADEONMinBits(int val)
{
    int bits;

    if (!val) return 1;
    for (bits = 0; val; val >>= 1, ++bits);
    return bits;
}

/* Compute n/d with rounding. */
static int RADEONDiv(int n, int d)
{
    return (n + (d / 2)) / d;
}

/* Read the Video BIOS block and the FP registers (if applicable). */
static Bool RADEONGetBIOSParameters(ScrnInfoPtr pScrn, xf86Int10InfoPtr pInt10)
{
    RADEONInfoPtr info     = RADEONPTR(pScrn);
    unsigned long tmp, i;
    unsigned char   *RADEONMMIO;

#define RADEON_BIOS8(v)  (info->VBIOS[v])
#define RADEON_BIOS16(v) (info->VBIOS[v] | \
			  (info->VBIOS[(v) + 1] << 8))
#define RADEON_BIOS32(v) (info->VBIOS[v] | \
			  (info->VBIOS[(v) + 1] << 8) | \
			  (info->VBIOS[(v) + 2] << 16) | \
			  (info->VBIOS[(v) + 3] << 24))

    if (!(info->VBIOS = xalloc(RADEON_VBIOS_SIZE)))
    {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "Cannot allocate space for hold Video BIOS!\n");
	return FALSE;
    }


    if (pInt10)
    {
	info->BIOSAddr = pInt10->BIOSseg << 4;
	(void)memcpy(info->VBIOS, xf86int10Addr(pInt10, info->BIOSAddr),
		     RADEON_VBIOS_SIZE);
    }
    else
    {
	xf86ReadPciBIOS(0, info->PciTag, 0, info->VBIOS, RADEON_VBIOS_SIZE);
	      if (info->VBIOS[0] != 0x55 || info->VBIOS[1] != 0xaa)
	      {
	    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		       "Video BIOS not detected in PCI space!\n");
	    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		       "Attempting to read Video BIOS from legacy ISA space!\n");
	    info->BIOSAddr = 0x000c0000;
	    xf86ReadBIOS(info->BIOSAddr, 0, info->VBIOS, RADEON_VBIOS_SIZE);
	}
    }
    if (info->VBIOS[0] != 0x55 || info->VBIOS[1] != 0xaa)
    {
	info->BIOSAddr = 0x00000000;
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "Video BIOS not found!\n");
    }

    info->FPBIOSstart = RADEON_BIOS16(0x48);

    {
        BOOL BypassSecondary = FALSE;
        RADEONMapMMIO(pScrn);
        RADEONMMIO               = info->MMIO;
        /*FIXME: using BIOS scratch registers to detect connected monitors 
             may not be a reliable way.... should use EDID data. 
             Also it only works with for VE/M6, no
             such registers in regular RADEON!!!*/
        /*****
          VE and M6 have both DVI and CRT ports (for M6 DVI port can be switch to
          DFP port). The DVI port can also be conneted to a CRT with an adapter.
          Here is the definition of ports for this driver---
          (1) If both port are connected, DVI port will be treated as the Primary 
              port (first screen in XF86Config, uses CRTC1) and CRT port will be 
              treated as the Secondary port (second screen in XF86Config, uses CRTC2)
          (2) If only one screen specified in XF86Config, it will be used for DVI port
              if a monitor is connected to DVI port, otherwise (only one monitor is 
              connected the CRT port) it will be used for CRT port.
        *****/
        if(info->HasCRTC2)
        {                    
             /*FIXME: this may not be reliable*/
             tmp = INREG(RADEON_BIOS_4_SCRATCH);
             if(info->IsSecondary)
             {  
                 /*check Port2 (CRT port, for the existing boards 
                   (VE & M6),this port can only be connected to a CRT*/
                 if(tmp & 0x02) info->DisplayType = MT_CRT;
                 else if(tmp & 0x800) info->DisplayType = MT_DFP;
                 else if(tmp & 0x400) info->DisplayType = MT_LCD;
                 else if(tmp & 0x1000) info->DisplayType = MT_CTV;
                 else if(tmp & 0x2000) info->DisplayType = MT_STV;
                 else info->DisplayType = MT_CRT;

                 if(info->DisplayType > MT_NONE)
                 {
                     DevUnion* pPriv;
                     RADEONEntPtr pRADEONEnt;
                     pPriv = xf86GetEntityPrivate(pScrn->entityList[0], 
                         gRADEONEntityIndex);
                     pRADEONEnt = pPriv->ptr;
                     pRADEONEnt->HasSecondary = TRUE;

                 }
                 else return FALSE;
                     
             }
             else
             {
                 /*check Primary (DVI/DFP port)*/
                 if(tmp & 0x08) info->DisplayType = MT_DFP;
                 else if(tmp & 0x04) info->DisplayType = MT_LCD;
                 else if(tmp & 0x0200) info->DisplayType = MT_CRT;
                 else if(tmp & 0x10) info->DisplayType = MT_CTV;
                 else if(tmp & 0x20) info->DisplayType = MT_STV;
                 else 
                 {
                     /*DVI port has no monitor connected, try CRT port.
                     If something on CRT port, treat it as primary*/
                     if(xf86IsEntityShared(pScrn->entityList[0]))
                     {
                         DevUnion* pPriv;
                         RADEONEntPtr pRADEONEnt;
                         pPriv = xf86GetEntityPrivate(pScrn->entityList[0], 
                             gRADEONEntityIndex);
                         pRADEONEnt = pPriv->ptr;
                         pRADEONEnt->BypassSecondary = TRUE;
                     }

                     if(tmp & 0x02) info->DisplayType = MT_CRT;
                     else if(tmp & 0x800) info->DisplayType = MT_DFP;
                     else if(tmp & 0x400) info->DisplayType = MT_LCD;
                     else if(tmp & 0x1000) info->DisplayType = MT_CTV;
                     else if(tmp & 0x2000) info->DisplayType = MT_STV;
                     else
                     {
                         xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                             "No monitor detected!!!\n");
                         return FALSE;
                     }
                     BypassSecondary = TRUE;
                 }
             }
         }
         else
         {
             /*Regular Radeon ASIC, only one CRTC, but it could be
               used for DFP with a DVI output, like AIW board*/
             tmp = INREG(RADEON_FP_GEN_CNTL);
             if(tmp & RADEON_FP_EN_TMDS) info->DisplayType = MT_DFP;
             else info->DisplayType = MT_CRT;
         }

         xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s Display == Type %d\n",
              (info->IsSecondary ? "Secondary" : "Primary"), 
               info->DisplayType);
        
        RADEONMMIO               = NULL;
        RADEONUnmapMMIO(pScrn);

        info->HBlank = 0;
        info->HOverPlus = 0;
        info->HSyncWidth = 0;
        info->VBlank = 0;
        info->VOverPlus = 0;
        info->VSyncWidth = 0;          
        info->DotClock = 0;

		 if(info->DisplayType == MT_LCD) {
             tmp = RADEON_BIOS16(info->FPBIOSstart + 0x40);
             if(!tmp) {
                 info->PanelPwrDly = 200;
                 xf86DrvMsg(pScrn->scrnIndex, X_INFO, "No Panel Info Table found in BIOS!\n");
             } else {
                 char stmp[30];
                 int tmp0;
                 for(i=0; i<24; i++)
					 stmp[i] = RADEON_BIOS8(tmp+i+1);
				 stmp[24] = 0; 
                 xf86DrvMsg(pScrn->scrnIndex, X_INFO, 
                     "Panel ID string: %s\n", stmp);
                 info->PanelXRes = RADEON_BIOS16(tmp+25);
                 info->PanelYRes = RADEON_BIOS16(tmp+27);
                 xf86DrvMsg(0, X_INFO, "Panel Size from BIOS: %dx%d\n", 
							info->PanelXRes, info->PanelYRes);
                 info->PanelPwrDly = RADEON_BIOS16(tmp+44);
                 if(info->PanelPwrDly > 2000 || info->PanelPwrDly < 0)
                      info->PanelPwrDly = 2000;
                 for(i=0; i<20; i++) {
                     tmp0 = RADEON_BIOS16(tmp+64+i*2);
                     if(tmp0 == 0) break;
                     if((RADEON_BIOS16(tmp0) == info->PanelXRes) &&
						(RADEON_BIOS16(tmp0+2) == info->PanelYRes)) {

                         info->HBlank = (RADEON_BIOS16(tmp0+17) - RADEON_BIOS16(tmp0+19)) * 8;
                         info->HOverPlus = (RADEON_BIOS16(tmp0+21) - RADEON_BIOS16(tmp0+19) - 1) * 8;
                         info->HSyncWidth = RADEON_BIOS8(tmp0+23) * 8;
                         info->VBlank = RADEON_BIOS16(tmp0+24) - RADEON_BIOS16(tmp0+26);
                         info->VOverPlus = (RADEON_BIOS16(tmp0+28) & 0x7ff) - RADEON_BIOS16(tmp0+26);
                         info->VSyncWidth = (RADEON_BIOS16(tmp0+28) & 0xf800) >> 11;          
                         info->DotClock = RADEON_BIOS16(tmp0+9) * 10;
                     }
                 }
             } 
         }

        /* Detect connector type from BIOS, used for
           I2C/DDC qeurying EDID, Only available for VE or newer cards*/
        tmp = RADEON_BIOS16(info->FPBIOSstart + 0x50);
        if(tmp)
	    {
            for(i=1; i<4; i++) {
                if(!RADEON_BIOS8(tmp + i*2) && i>1) break;
           
                /*Note: our Secondary port (CRT port) 
                  actually uses primary DAC*/
                if(RADEON_BIOS16(tmp + i*2) & 0x01) {
                    if(!info->IsSecondary) {
                        info->DDCType = 
                            (RADEON_BIOS16(tmp + i*2) & 0x0f00) >> 8;
                        break;
                    }
                } else {/*Primary DAC*/
            
                    if(info->IsSecondary || BypassSecondary || !info->HasCRTC2) {
                        info->DDCType = 
                            (RADEON_BIOS16(tmp + i*2) & 0x0f00) >> 8;
                        break;
                    }
                }
            }
        } else {
            /* orignal radeon cards, set it to DDC_VGA, 
               this will not work with AIW, it should be DDC_DVI,
               let it fall back to VBE calls for AIW */
            info->DDCType = DDC_VGA;
        }
    }

    return TRUE;
}

/* Read PLL parameters from BIOS block.  Default to typical values if there
   is no BIOS. */
static Bool RADEONGetPLLParameters(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr   info = RADEONPTR(pScrn);
    RADEONPLLPtr    pll  = &info->pll;
    CARD16          bios_header;
    CARD16          pll_info_block;

    if (!info->VBIOS) {
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "Video BIOS not detected, using default PLL parameters!\n");
				/* These probably aren't going to work for
				   the card you are using.  Specifically,
				   reference freq can be 29.50MHz,
				   28.63MHz, or 14.32MHz.  YMMV. */
 	/*
	 * these are somewhat sane defaults for Mac boards, we will
	 * need to find a good way of getting these from OpenFirmware
	 */
	pll->reference_freq = 2700;
	pll->reference_div  = 67;
	pll->min_pll_freq   = 12500;
	pll->max_pll_freq   = 35000;
	pll->xclk           = 16615;
    } else {
	bios_header    = RADEON_BIOS16(0x48);
	pll_info_block = RADEON_BIOS16(bios_header + 0x30);
	RADEONTRACE(("Header at 0x%04x; PLL Information at 0x%04x\n",
		     bios_header, pll_info_block));

	pll->reference_freq = RADEON_BIOS16(pll_info_block + 0x0e);
	pll->reference_div  = RADEON_BIOS16(pll_info_block + 0x10);
	pll->min_pll_freq   = RADEON_BIOS32(pll_info_block + 0x12);
	pll->max_pll_freq   = RADEON_BIOS32(pll_info_block + 0x16);
	pll->xclk           = RADEON_BIOS16(pll_info_block + 0x08);
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	       "PLL parameters: rf=%d rd=%d min=%d max=%d; xclk=%d\n",
	       pll->reference_freq,
	       pll->reference_div,
	       pll->min_pll_freq,
	       pll->max_pll_freq,
	       pll->xclk);

    return TRUE;
}

/* This is called by RADEONPreInit to set up the default visual. */
static Bool RADEONPreInitVisual(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info          = RADEONPTR(pScrn);

    if(!xf86SetDepthBpp(pScrn, 8, 8, 8, Support32bppFb))
	return FALSE;

    switch (pScrn->depth) {
    case 8:
    case 15:
    case 16:
    case 24:
	break;
    default:
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "Given depth (%d) is not supported by %s driver\n",
		   pScrn->depth, RADEON_DRIVER_NAME);
	return FALSE;
    }

    xf86PrintDepthBpp(pScrn);

    info->fifo_slots  = 0;
    info->pix24bpp    = xf86GetBppFromDepth(pScrn, pScrn->depth);
    info->CurrentLayout.bitsPerPixel = pScrn->bitsPerPixel;
    info->CurrentLayout.depth        = pScrn->depth;
    info->CurrentLayout.pixel_bytes  = pScrn->bitsPerPixel / 8;
    info->CurrentLayout.pixel_code   = (pScrn->bitsPerPixel != 16
				       ? pScrn->bitsPerPixel
				       : pScrn->depth);

    if (info->pix24bpp == 24) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
	       "Radeon does NOT support 24bpp\n");
	return FALSE;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	       "Pixel depth = %d bits stored in %d byte%s (%d bpp pixmaps)\n",
	       pScrn->depth,
	       info->CurrentLayout.pixel_bytes,
	       info->CurrentLayout.pixel_bytes > 1 ? "s" : "",
	       info->pix24bpp);


    if (!xf86SetDefaultVisual(pScrn, -1)) return FALSE;

    if (pScrn->depth > 8 && pScrn->defaultVisual != TrueColor) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "Default visual (%s) is not supported at depth %d\n",
		   xf86GetVisualName(pScrn->defaultVisual), pScrn->depth);
	return FALSE;
    }
    return TRUE;

}

/* This is called by RADEONPreInit to handle all color weight issues. */
static Bool RADEONPreInitWeight(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info          = RADEONPTR(pScrn);

				/* Save flag for 6 bit DAC to use for
				   setting CRTC registers.  Otherwise use
				   an 8 bit DAC, even if xf86SetWeight sets
				   pScrn->rgbBits to some value other than
				   8. */
    info->dac6bits = FALSE;
    if (pScrn->depth > 8) {
	rgb defaultWeight = { 0, 0, 0 };
	if (!xf86SetWeight(pScrn, defaultWeight, defaultWeight)) return FALSE;
    } else {
	pScrn->rgbBits = 8;
	if (xf86ReturnOptValBool(info->Options, OPTION_DAC_6BIT, FALSE)) {
	    pScrn->rgbBits = 6;
	    info->dac6bits = TRUE;
	}
    }
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	       "Using %d bits per RGB (%d bit DAC)\n",
	       pScrn->rgbBits, info->dac6bits ? 6 : 8);

    return TRUE;

}

/* This is called by RADEONPreInit to handle config file overrides for things
   like chipset and memory regions.  Also determine memory size and type.
   If memory type ever needs an override, put it in this routine. */
static Bool RADEONPreInitConfig(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr   info   = RADEONPTR(pScrn);
    EntityInfoPtr   pEnt   = info->pEnt;
    GDevPtr         dev    = pEnt->device;
    int             offset = 0; /* RAM Type */
    MessageType     from;
    unsigned char   *RADEONMMIO;

				/* Chipset */
    from = X_PROBED;
    if (dev->chipset && *dev->chipset) {
	info->Chipset  = xf86StringToToken(RADEONChipsets, dev->chipset);
	from           = X_CONFIG;
    } else if (dev->chipID >= 0) {
	info->Chipset  = dev->chipID;
	from           = X_CONFIG;
    } else {
	info->Chipset = info->PciInfo->chipType;
    }
    pScrn->chipset = (char *)xf86TokenToString(RADEONChipsets, info->Chipset);

    if (!pScrn->chipset) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "ChipID 0x%04x is not recognized\n", info->Chipset);
	return FALSE;
    }

    if (info->Chipset < 0) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "Chipset \"%s\" is not recognized\n", pScrn->chipset);
	return FALSE;
    }

    xf86DrvMsg(pScrn->scrnIndex, from,
	       "Chipset: \"%s\" (ChipID = 0x%04x)\n",
	       pScrn->chipset,
	       info->Chipset);


    info->IsM6 = FALSE;
    switch (info->Chipset) 
    {
   	case PCI_CHIP_RADEON_LY:
   	case PCI_CHIP_RADEON_LZ:
            info->IsM6 = TRUE;
	case PCI_CHIP_RADEON_QY:
   	case PCI_CHIP_RADEON_QZ:
            /*VE or M6 has secondary CRTC*/
            info->HasCRTC2 = TRUE;  
            break;
   	case PCI_CHIP_R200_QL:
            /*R200 has secondary CRTC*/
            info->HasCRTC2 = TRUE;  
            info->IsR200 = TRUE;
            break;
	case PCI_CHIP_RV200_BB:
   	case PCI_CHIP_RV200_QW:   /* RV200 desktop */
   	case PCI_CHIP_RADEON_LW:  /* M7 */
            info->HasCRTC2 = TRUE;  
            info->IsRV200 = TRUE;
            break;
        default: 
            info->HasCRTC2 = FALSE;  
    }

				/* Framebuffer */

    from             = X_PROBED;
    info->LinearAddr = info->PciInfo->memBase[0] & 0xfc000000;
    pScrn->memPhysBase = info->LinearAddr;
    if (dev->MemBase) {
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "Linear address override, using 0x%08x instead of 0x%08x\n",
		   dev->MemBase,
		   info->LinearAddr);
	info->LinearAddr = dev->MemBase;
	from             = X_CONFIG;
    } else if (!info->LinearAddr) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "No valid linear framebuffer address\n");
	return FALSE;
    }
    xf86DrvMsg(pScrn->scrnIndex, from,
	       "Linear framebuffer at 0x%08lx\n", info->LinearAddr);

				/* MMIO registers */
    from             = X_PROBED;
    info->MMIOAddr   = info->PciInfo->memBase[2] & 0xffffff00;
    if (dev->IOBase) {
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "MMIO address override, using 0x%08x instead of 0x%08x\n",
		   dev->IOBase,
		   info->MMIOAddr);
	info->MMIOAddr = dev->IOBase;
	from           = X_CONFIG;
    } else if (!info->MMIOAddr) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No valid MMIO address\n");
	return FALSE;
    }
    xf86DrvMsg(pScrn->scrnIndex, from,
	       "MMIO registers at 0x%08lx\n", info->MMIOAddr);

				/* BIOS */
    from              = X_PROBED;
    info->BIOSAddr    = info->PciInfo->biosBase & 0xfffe0000;
    if (dev->BiosBase) {
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "BIOS address override, using 0x%08x instead of 0x%08x\n",
		   dev->BiosBase,
		   info->BIOSAddr);
	info->BIOSAddr = dev->BiosBase;
	from           = X_CONFIG;
    }
    if (info->BIOSAddr) {
	xf86DrvMsg(pScrn->scrnIndex, from,
		   "BIOS at 0x%08lx\n", info->BIOSAddr);
    }

    RADEONMapMMIO(pScrn);
    RADEONMMIO               = info->MMIO;

				/* Read registers used to determine options */
    from                     = X_PROBED;
    if (info->FBDev)
	pScrn->videoRam      = fbdevHWGetVidmem(pScrn) / 1024;
    else
	pScrn->videoRam      = INREG(RADEON_CONFIG_MEMSIZE) / 1024;
    
    /* some production boards of m6 will return 0 if it's 8 MB */
    if(pScrn->videoRam == 0) pScrn->videoRam = 8192;
        
    if(info->IsSecondary)
    {  
	/*FIXME: For now, split FB into two equal sections. This should
          be able to be adjusted by user with a config option*/
        DevUnion* pPriv;
        RADEONEntPtr pRADEONEnt;
        RADEONInfoPtr   info1;
        pPriv = xf86GetEntityPrivate(pScrn->entityList[0], 
              gRADEONEntityIndex);
        pRADEONEnt = pPriv->ptr;
        pScrn->videoRam /= 2;
        pRADEONEnt->pPrimaryScrn->videoRam = pScrn->videoRam;
        info1 = RADEONPTR(pRADEONEnt->pPrimaryScrn);
        info1->FbMapSize  = pScrn->videoRam * 1024;
        info->LinearAddr += pScrn->videoRam * 1024;
    }

    info->MemCntl            = INREG(RADEON_SDRAM_MODE_REG);
    info->BusCntl            = INREG(RADEON_BUS_CNTL);
    RADEONMMIO               = NULL;
    RADEONUnmapMMIO(pScrn);

				/* RAM */
    switch (info->MemCntl >> 30) {
    case 0:            offset = 0; break; /*  64-bit SDR SDRAM */
    case 1:            offset = 1; break; /*  64-bit DDR SDRAM */
    default:           offset = 0;
    }
    info->ram = &RADEONRAM[offset];

    if (dev->videoRam)
    {
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "Video RAM override, using %d kB instead of %d kB\n",
		   dev->videoRam,
		   pScrn->videoRam);
	from             = X_CONFIG;
	pScrn->videoRam  = dev->videoRam;
    }
    pScrn->videoRam  &= ~1023;
    info->FbMapSize  = pScrn->videoRam * 1024;
    xf86DrvMsg(pScrn->scrnIndex, from,
	       "VideoRAM: %d kByte (%s)\n", pScrn->videoRam, info->ram->name);

#ifdef XF86DRI
				/* AGP/PCI */
    if (xf86ReturnOptValBool(info->Options, OPTION_IS_PCI, FALSE)) {
	info->IsPCI = TRUE;
	xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "Forced into PCI-only mode\n");
    } else {
	switch (info->Chipset) {
#if 0
	case PCI_CHIP_RADEON_XX: info->IsPCI = TRUE;  break;
#endif
	case PCI_CHIP_RADEON_QY:
	case PCI_CHIP_RADEON_QZ:
	case PCI_CHIP_RADEON_LW:
	case PCI_CHIP_RADEON_LY:
	case PCI_CHIP_RADEON_LZ:
	case PCI_CHIP_RADEON_QD:
	case PCI_CHIP_RADEON_QE:
	case PCI_CHIP_RADEON_QF:
	case PCI_CHIP_RADEON_QG:
	case PCI_CHIP_R200_QL:
	case PCI_CHIP_RV200_QW:
	case PCI_CHIP_RV200_BB:
	default:                 info->IsPCI = FALSE; break;
	}
    }
#endif

    return TRUE;
}

static void
RADEONI2CGetBits(I2CBusPtr b, int *Clock, int *data)
{
    ScrnInfoPtr   pScrn       = xf86Screens[b->scrnIndex];
    RADEONInfoPtr info = RADEONPTR(pScrn);
    unsigned long val;
    unsigned char *RADEONMMIO = info->MMIO;

    /* Get the result. */
    val = INREG(info->DDCReg);

    *Clock = (val & RADEON_GPIO_Y_1) != 0;
    *data  = (val & RADEON_GPIO_Y_0) != 0;

}

static void
RADEONI2CPutBits(I2CBusPtr b, int Clock, int data)
{
    ScrnInfoPtr   pScrn       = xf86Screens[b->scrnIndex];
    RADEONInfoPtr info = RADEONPTR(pScrn);
    unsigned long val;
    unsigned char *RADEONMMIO = info->MMIO;

    val = INREG(info->DDCReg) & 
              (CARD32)~(RADEON_GPIO_EN_0 | RADEON_GPIO_EN_1);
    val |= (Clock ? 0:RADEON_GPIO_EN_1);
    val |= (data ? 0:RADEON_GPIO_EN_0);
    OUTREG(info->DDCReg, val);
}


static Bool
RADEONI2cInit(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info = RADEONPTR(pScrn);

    info->pI2CBus = xf86CreateI2CBusRec();
    if(!info->pI2CBus) return FALSE;

    info->pI2CBus->BusName    = "DDC";
    info->pI2CBus->scrnIndex  = pScrn->scrnIndex;
    info->pI2CBus->I2CPutBits = RADEONI2CPutBits;
    info->pI2CBus->I2CGetBits = RADEONI2CGetBits;
    info->pI2CBus->AcknTimeout = 5;

    switch(info->DDCType)
    {
        case DDC_MONID:
           info->DDCReg = RADEON_GPIO_MONID;
           break;
        case DDC_DVI:
           info->DDCReg = RADEON_GPIO_DVI_DDC;
           break;
        case DDC_VGA:
           info->DDCReg = RADEON_GPIO_VGA_DDC;
           break;
        case DDC_CRT2:
           info->DDCReg = RADEON_GPIO_CRT2_DDC;
           break;
        default:
           return FALSE;
    }

    if (!xf86I2CBusInit(info->pI2CBus)) return FALSE;
    return TRUE;
}

static void RADEONPreInitDDC(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info = RADEONPTR(pScrn);
    /*vbeInfoPtr pVbe;*/

    info->ddc1 = FALSE;
    info->ddc_bios = FALSE;
    if (!xf86LoadSubModule(pScrn, "ddc"))
    {
        info->ddc2 = FALSE;
    }
    else
    {
    xf86LoaderReqSymLists(ddcSymbols, NULL);
        info->ddc2 = TRUE;
    }

    /*info->ddc1 = TRUE;*/

    /* - DDC can use I2C bus */
    /* Load I2C if we have the code to use it */
    if(info->ddc2)
    {
        if ( xf86LoadSubModule(pScrn, "i2c") )
        {
            xf86LoaderReqSymLists(i2cSymbols,NULL);
            info->ddc2 = RADEONI2cInit(pScrn);
        }
        else info->ddc2 = FALSE;
    }
}

static xf86MonPtr
RADEONDoDDC(ScrnInfoPtr pScrn, xf86Int10InfoPtr pInt10)
{
    RADEONInfoPtr info = RADEONPTR(pScrn);
    xf86MonPtr MonInfo = NULL;
    unsigned char *RADEONMMIO;


    /**
       We'll use DDC2, BIOS EDID can only detect the monitor 
       connected to one port. For VE, BIOS EDID detects the
       monitor connected to DVI port by default. If no monitor 
       their, it will try CRT port   
    */
 
    /* Read and output monitor info using DDC2 over I2C bus */
    if (info->pI2CBus && info->ddc2)
    {
        if (!RADEONMapMMIO(pScrn)) return NULL;
        RADEONMMIO = info->MMIO;
        /*OUTREG(RADEON_I2C_CNTL_1, 0);
        OUTREG(RADEON_DVI_I2C_CNTL_1, 0);*/
        OUTREG(info->DDCReg, INREG(info->DDCReg) &
                 (CARD32)~(RADEON_GPIO_A_0 | RADEON_GPIO_A_1));

        MonInfo = xf86DoEDID_DDC2(pScrn->scrnIndex, info->pI2CBus);
        if(!MonInfo) info->ddc2 = FALSE;
        RADEONUnmapMMIO(pScrn);
    }

    if(!MonInfo)
    {
        if (xf86LoadSubModule(pScrn, "vbe"))
        {
            vbeInfoPtr pVbe;
  	pVbe = VBEInit(pInt10, info->pEnt->index);
            if (pVbe)
            { 
                MonInfo = vbeDoEDID(pVbe,NULL);
                info->ddc_bios = TRUE;
            }
            else
                info->ddc_bios = FALSE;
        }
    }
  
/***Not used for now
    if(!MonInfo && info->ddc1)
    {
        if (info->ddc1Read && info->DDC1SetSpeed)
        {
            MonInfo = xf86DoEDID_DDC1(pScrn->scrnIndex,
	                              info->DDC1SetSpeed,
				      info->ddc1Read);
        }
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "DDC Monitor info: %p\n", MonInfo);
        xf86PrintEDID( MonInfo );
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "end of DDC Monitor info\n\n");
    }
***/

    if(MonInfo)
    {
	if(info->ddc2)
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "I2C EDID Info:\n");
        else if(info->ddc_bios)
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "BIOS  EDID Info:\n");
        else return NULL;

        xf86PrintEDID(MonInfo);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "End of DDC Monitor info\n\n");

        xf86SetDDCproperties(pScrn, MonInfo);
        return MonInfo;
    }
    else return NULL;
}

/*********** 
   xfree's xf86ValidateModes routine deosn't work well with DFPs
   here is our own validation routine. All modes between 
   640<=XRes<=MaxRes and 480<=YRes<=MaxYRes will be permitted. 
************/ 
static int RADEONValidateFPModes(ScrnInfoPtr pScrn)
{
    int i, j, count=0, width, height;
    RADEONInfoPtr info = RADEONPTR(pScrn);
    DisplayModePtr last = NULL, new = NULL, first = NULL;

    /* Free any allocated modes during configuration. We don't need them*/
    while (pScrn->modes)
    {
	    xf86DeleteMode(&pScrn->modes, pScrn->modes);
    }
    while (pScrn->modePool)
    {
	    xf86DeleteMode(&pScrn->modePool, pScrn->modePool);
    }

    pScrn->virtualX = pScrn->display->virtualX;
    pScrn->virtualY = pScrn->display->virtualY;

    /* If no mode specified in config, we use native resolution*/
    if(!pScrn->display->modes[0])
    {
        pScrn->display->modes[0] = xnfalloc(16);
        sprintf(pScrn->display->modes[0], "%dx%d",
               info->PanelXRes, info->PanelYRes);
    }

    for(i=0; pScrn->display->modes[i] != NULL; i++)
    {
        if (sscanf(pScrn->display->modes[i], "%dx%d", &width, &height) == 2)
        {

            if(width < 640 || width > info->PanelXRes || 
               height < 480 || height > info->PanelYRes)
            {
                xf86DrvMsg(pScrn->scrnIndex, X_WARNING, 
                    "Mode %s is out of range.\n"
                    "Valid mode should be between 640x480-%dx%d\n",
                    pScrn->display->modes[i], info->PanelXRes, info->PanelYRes);
                continue;
            }           

            new = xnfcalloc(1, sizeof(DisplayModeRec));
            new->prev = last;
            new->name = xnfalloc(strlen(pScrn->display->modes[i]) + 1);
            strcpy(new->name, pScrn->display->modes[i]);
            new->HDisplay = new->CrtcHDisplay = width;
            new->VDisplay = new->CrtcVDisplay = height;

            if(info->HasEDID)
            {
                xf86MonPtr ddc = pScrn->monitor->DDC;
                for(j=0; j<DET_TIMINGS; j++)
                {
                    /*We use native mode clock only*/
                    if(ddc->det_mon[j].type == 0)
                        new->Clock = ddc->det_mon[j].section.d_timings.clock / 1000;
                }
            } else
                new->Clock = info->DotClock;

            if(new->prev) new->prev->next = new;
            last = new;
            if(!first) first = new;
            pScrn->display->virtualX =
            pScrn->virtualX = MAX(pScrn->virtualX, width);
            pScrn->display->virtualY =
            pScrn->virtualY = MAX(pScrn->virtualY, height);
            count++;
        }
        else
        {
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING, 
                "Mode name %s is invalid\n", pScrn->display->modes[i]); 
            continue;
        }
   }

   if(last)
   {
       last->next = first;
       first->prev = last;
       pScrn->modes = first;

       /*FIXME: May need to validate line pitch here*/
       {
           int dummy = 0;
           switch(pScrn->depth / 8)
           {
              case 1:
                  dummy = 128 - pScrn->virtualX % 128;
                  break;
              case 2:
                  dummy = 32 - pScrn->virtualX % 32;
                  break;
              case 3:
              case 4:
                  dummy = 16 - pScrn->virtualX % 16;
           }
           pScrn->displayWidth = pScrn->virtualX + dummy;
       }

   }

    return count;
}

/* This is called by RADEONPreInit to initialize gamma correction. */
static Bool RADEONPreInitGamma(ScrnInfoPtr pScrn)
{
    Gamma zeros = { 0.0, 0.0, 0.0 };

    if (!xf86SetGamma(pScrn, zeros)) return FALSE;
    return TRUE;
}

static void RADEONSetSyncRangeFromEdid(ScrnInfoPtr pScrn, int flag)
{
    int i;
    xf86MonPtr ddc = pScrn->monitor->DDC;
    if(flag)  /*HSync*/
    {
        for(i=0; i<4; i++)
        {
            if(ddc->det_mon[i].type == DS_RANGES)
            {
                pScrn->monitor->nHsync = 1;
                pScrn->monitor->hsync[0].lo = 
                    ddc->det_mon[i].section.ranges.min_h;
                pScrn->monitor->hsync[0].hi = 
                    ddc->det_mon[i].section.ranges.max_h;
                return;
            }
        }
        /*if no sync ranges detected in detailed timing table,
          let's try to derive them from supported VESA modes
          Are we doing too much here!!!? 
        **/
        i = 0;
        if(ddc->timings1.t1 & 0x02) /*800x600@56*/
        {
            pScrn->monitor->hsync[i].lo = 
                pScrn->monitor->hsync[i].hi = 35.2;
            i++;
        }       
        if(ddc->timings1.t1 & 0x04) /*640x480@75*/
        {
            pScrn->monitor->hsync[i].lo = 
                pScrn->monitor->hsync[i].hi = 37.5;
            i++;
        }       
        if((ddc->timings1.t1 & 0x08) || (ddc->timings1.t1 & 0x01))
        {
            pScrn->monitor->hsync[i].lo = 
                pScrn->monitor->hsync[i].hi = 37.9;
            i++;
        }       
        if(ddc->timings1.t2 & 0x40)
        {
            pScrn->monitor->hsync[i].lo = 
                pScrn->monitor->hsync[i].hi = 46.9;
            i++;
        }
        if((ddc->timings1.t2 & 0x80) || (ddc->timings1.t2 & 0x08))
        {
            pScrn->monitor->hsync[i].lo = 
                pScrn->monitor->hsync[i].hi = 48.1;
            i++;
        }       
        if(ddc->timings1.t2 & 0x04)
        {
            pScrn->monitor->hsync[i].lo = 
                pScrn->monitor->hsync[i].hi = 56.5;
            i++;
        }       
        if(ddc->timings1.t2 & 0x02)
        {
            pScrn->monitor->hsync[i].lo = 
                pScrn->monitor->hsync[i].hi = 60.0;
            i++;
        }       
        if(ddc->timings1.t2 & 0x01)
        {
            pScrn->monitor->hsync[i].lo = 
                pScrn->monitor->hsync[i].hi = 64.0;
            i++;
        }
        pScrn->monitor->nHsync = i;
    }
    else      /*Vrefresh*/
    {
        for(i=0; i<4; i++)
        {
            if(ddc->det_mon[i].type == DS_RANGES)
            {
                pScrn->monitor->nVrefresh = 1;
                pScrn->monitor->vrefresh[0].lo = 
                    ddc->det_mon[i].section.ranges.min_v;
                pScrn->monitor->vrefresh[0].hi = 
                    ddc->det_mon[i].section.ranges.max_v;
                return;
            }
        }
        i = 0;
        if(ddc->timings1.t1 & 0x02) /*800x600@56*/
        {        
            pScrn->monitor->vrefresh[i].lo = 
                pScrn->monitor->vrefresh[i].hi = 56;
            i++;
        }
        if((ddc->timings1.t1 & 0x01) || (ddc->timings1.t2 & 0x08))
        {        
            pScrn->monitor->vrefresh[i].lo = 
                pScrn->monitor->vrefresh[i].hi = 60;
            i++;
        }
        if(ddc->timings1.t2 & 0x04)
        {        
            pScrn->monitor->vrefresh[i].lo = 
                pScrn->monitor->vrefresh[i].hi = 70;
            i++;
        }
        if((ddc->timings1.t1 & 0x08) || (ddc->timings1.t2 & 0x80))
        {        
            pScrn->monitor->vrefresh[i].lo = 
                pScrn->monitor->vrefresh[i].hi = 72;
            i++;
        }
        if((ddc->timings1.t1 & 0x04) || (ddc->timings1.t2 & 0x40)
           || (ddc->timings1.t2 & 0x02) || (ddc->timings1.t2 & 0x01))
        {        
            pScrn->monitor->vrefresh[i].lo = 
                pScrn->monitor->vrefresh[i].hi = 75;
            i++;
        }
        pScrn->monitor->nVrefresh = i;
    }
}

/* This is called by RADEONPreInit to validate modes and compute parameters
   for all of the valid modes. */
static Bool RADEONPreInitModes(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info = RADEONPTR(pScrn);
    ClockRangePtr clockRanges;
    int           modesFound;
    char          *mod = NULL;
#ifndef USE_FB
    const char    *Sym = NULL;
#endif

    /*We'll use our own mode validation routine for DFP/LCD, since
      xf86ValidateModes is not working well with the DFP/LCD modes 
      'stretched' from their native mode.*/
    if(info->DisplayType == MT_CRT || info->UseCRT)
    {
				/* Get mode information */
    pScrn->progClock                   = TRUE;
    clockRanges                        = xnfcalloc(sizeof(*clockRanges), 1);
    clockRanges->next                  = NULL;
    clockRanges->minClock              = info->pll.min_pll_freq;
    clockRanges->maxClock              = info->pll.max_pll_freq * 10;
    clockRanges->clockIndex            = -1;
       
	clockRanges->interlaceAllowed  = TRUE;
	clockRanges->doubleScanAllowed = TRUE;
        
        if(info->HasEDID)
        {
            /*if we still don't know sync range yet, let's try EDID.
              Note that, since we can have dual heads, the Xconfigurator
              may not be able to probe both monitors correctly through
              vbe probe function (RADEONProbeDDC). Here we provide an
              additional way to auto-detect sync ranges if they haven't
              been added to XF86Config manually.
            **/
            if(pScrn->monitor->nHsync <= 0)
                 RADEONSetSyncRangeFromEdid(pScrn, 1);
            if(pScrn->monitor->nVrefresh <= 0)
                 RADEONSetSyncRangeFromEdid(pScrn, 0);
    }


    modesFound = xf86ValidateModes(pScrn,
				   pScrn->monitor->Modes,
				   pScrn->display->modes,
				   clockRanges,
				   NULL,        /* linePitches */
				   8 * 64,      /* minPitch */
				   8 * 1024,    /* maxPitch */
				   64 * pScrn->bitsPerPixel, /* pitchInc */
				   128,         /* minHeight */
				   2048,        /* maxHeight */
				   pScrn->display->virtualX,
				   pScrn->display->virtualY,
				   info->FbMapSize,
				   LOOKUP_BEST_REFRESH);

        if(modesFound < 1 && info->FBDev)
        {
	fbdevHWUseBuildinMode(pScrn);
	pScrn->displayWidth = pScrn->virtualX; /* FIXME: might be wrong */
	modesFound = 1;
    }

       if(modesFound == -1) return FALSE;

    xf86PruneDriverModes(pScrn);
       if(!modesFound || !pScrn->modes) 
       {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No valid modes found\n");
	return FALSE;
    }
    xf86SetCrtcForModes(pScrn, 0);
    }
    else
    {

        /*DFP/LCD mode validation routine*/
        modesFound = RADEONValidateFPModes(pScrn);
        if(modesFound < 1) 
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR, 
                 "No valid mode found for this DFP/LCD\n");
            return FALSE;
        }
    }

    pScrn->currentMode = pScrn->modes;
    xf86PrintModes(pScrn);

				/* Set DPI */
    xf86SetDpi(pScrn, 0, 0);

				/* Get ScreenInit function */
#ifdef USE_FB
    mod = "fb";
#else
    switch (pScrn->bitsPerPixel) {
    case  8: mod = "cfb";   Sym = "cfbScreenInit";   break;
    case 16: mod = "cfb16"; Sym = "cfb16ScreenInit"; break;
    case 32: mod = "cfb32"; Sym = "cfb32ScreenInit"; break;
    }
#endif

    if (mod && !xf86LoadSubModule(pScrn, mod)) return FALSE;

#ifdef USE_FB
    xf86LoaderReqSymLists(fbSymbols, NULL);
#else
    xf86LoaderReqSymbols(Sym, NULL);
#endif

    info->CurrentLayout.displayWidth = pScrn->displayWidth;
    info->CurrentLayout.mode = pScrn->currentMode;

    return TRUE;
}

/* This is called by RADEONPreInit to initialize the hardware cursor. */
static Bool RADEONPreInitCursor(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info = RADEONPTR(pScrn);

    if (!xf86ReturnOptValBool(info->Options, OPTION_SW_CURSOR, FALSE)) {
	if (!xf86LoadSubModule(pScrn, "ramdac")) return FALSE;
	xf86LoaderReqSymLists(ramdacSymbols, NULL);
    }
    return TRUE;
}

/* This is called by RADEONPreInit to initialize hardware acceleration. */
static Bool RADEONPreInitAccel(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info = RADEONPTR(pScrn);

    if (!xf86ReturnOptValBool(info->Options, OPTION_NOACCEL, FALSE)) {
	if (!xf86LoadSubModule(pScrn, "xaa")) return FALSE;
	xf86LoaderReqSymLists(xaaSymbols, NULL);
    }
    return TRUE;
}

static Bool RADEONPreInitInt10(ScrnInfoPtr pScrn, xf86Int10InfoPtr *ppInt10)
{
    RADEONInfoPtr   info = RADEONPTR(pScrn);

#if !defined(__powerpc__)
    if (xf86LoadSubModule(pScrn, "int10")) {
	xf86LoaderReqSymLists(int10Symbols, NULL);
	xf86DrvMsg(pScrn->scrnIndex,X_INFO,"initializing int10\n");
	*ppInt10 = xf86InitInt10(info->pEnt->index);
    }
#endif
    return TRUE;
}


#ifdef XF86DRI
static Bool RADEONPreInitDRI(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr   info = RADEONPTR(pScrn);

    if (xf86ReturnOptValBool(info->Options, OPTION_CP_PIO, FALSE)) {
	xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "Forcing CP into PIO mode\n");
	info->CPMode = RADEON_DEFAULT_CP_PIO_MODE;
    } else {
	info->CPMode = RADEON_DEFAULT_CP_BM_MODE;
    }

    info->agpMode       = RADEON_DEFAULT_AGP_MODE;
    info->agpSize       = RADEON_DEFAULT_AGP_SIZE;
    info->ringSize      = RADEON_DEFAULT_RING_SIZE;
    info->bufSize       = RADEON_DEFAULT_BUFFER_SIZE;
    info->agpTexSize    = RADEON_DEFAULT_AGP_TEX_SIZE;

    info->CPusecTimeout = RADEON_DEFAULT_CP_TIMEOUT;

    if (!info->IsPCI) {
	if (xf86GetOptValInteger(info->Options,
				 OPTION_AGP_MODE, &(info->agpMode))) {
	    if (info->agpMode < 1 || info->agpMode > RADEON_AGP_MAX_MODE) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "Illegal AGP Mode: %d\n", info->agpMode);
		return FALSE;
	    }
	    xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
		       "Using AGP %dx mode\n", info->agpMode);
	}

	if (xf86GetOptValInteger(info->Options,
				 OPTION_AGP_SIZE, (int *)&(info->agpSize))) {
	    switch (info->agpSize) {
	    case 4:
	    case 8:
	    case 16:
	    case 32:
	    case 64:
	    case 128:
	    case 256:
		break;
	    default:
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "Illegal AGP size: %d MB\n", info->agpSize);
		return FALSE;
	    }
	}

	if (xf86GetOptValInteger(info->Options,
				 OPTION_RING_SIZE, &(info->ringSize))) {
	    if (info->ringSize < 1 || info->ringSize >= (int)info->agpSize) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "Illegal ring buffer size: %d MB\n",
			   info->ringSize);
		return FALSE;
	    }
	}

	if (xf86GetOptValInteger(info->Options,
				 OPTION_BUFFER_SIZE, &(info->bufSize))) {
	    if (info->bufSize < 1 || info->bufSize >= (int)info->agpSize) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "Illegal vertex/indirect buffers size: %d MB\n",
			   info->bufSize);
		return FALSE;
	    }
	    if (info->bufSize > 2) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "Illegal vertex/indirect buffers size: %d MB\n",
			   info->bufSize);
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "Clamping vertex/indirect buffers size to 2 MB\n");
		info->bufSize = 2;
	    }
	}

	if (info->ringSize + info->bufSize + info->agpTexSize >
	    (int)info->agpSize) {
	    xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		       "Buffers are too big for requested AGP space\n");
	    return FALSE;
	}

	info->agpTexSize = info->agpSize - (info->ringSize + info->bufSize);
    }

    if (xf86GetOptValInteger(info->Options, OPTION_USEC_TIMEOUT,
			     &(info->CPusecTimeout))) {
	/* This option checked by the RADEON DRM kernel module */
    }

    /* Depth moves are disabled by default since they are extremely slow */
    if ((info->depthMoves = xf86ReturnOptValBool(info->Options,
						 OPTION_DEPTH_MOVE, FALSE))) {
	xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "Enabling depth moves\n");
    } else {
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "Depth moves disabled by default\n");
    }

    return TRUE;
}
#endif

static void
RADEONProbeDDC(ScrnInfoPtr pScrn, int indx)
{
    vbeInfoPtr pVbe;
    if (xf86LoadSubModule(pScrn, "vbe"))
    {
	pVbe = VBEInit(NULL,indx);
	ConfiguredMonitor = vbeDoEDID(pVbe, NULL);
    }
}

/*This funtion is used to reverse calculate 
  panel information from register settings in VGA mode.
  More graceful way is to use EDID information... if it can be detected.
  This way may be better than directly probing BIOS image. Because
  BIOS image could change from version to version, while the 
  registers should always(?) contain right information, otherwise
  the VGA mode display will not be correct. Well, if someone  
  messes up these registers before our driver is loaded, we'll be in 
  trouble...*/
static Bool RadeonGetDFPInfo(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info  = RADEONPTR(pScrn);

    char *s;
    unsigned long r;
    unsigned short a, b;	
    unsigned char* RADEONMMIO;
    unsigned long mapped;

    if(info->HasEDID)
    {
        int i;
        xf86MonPtr ddc = pScrn->monitor->DDC;
        for(i=0; i<4; i++)
        {
            if(ddc->det_mon[i].type == 0)
            {
                info->PanelXRes =
                    ddc->det_mon[i].section.d_timings.h_active;
                info->PanelYRes =
                    ddc->det_mon[i].section.d_timings.v_active;

                info->HOverPlus =
                    ddc->det_mon[i].section.d_timings.h_sync_off;
                info->HSyncWidth = 
                    ddc->det_mon[i].section.d_timings.h_sync_width;
                info->HBlank =
                    ddc->det_mon[i].section.d_timings.h_blanking;
                info->VOverPlus =
                    ddc->det_mon[i].section.d_timings.v_sync_off;
                info->VSyncWidth = 
                    ddc->det_mon[i].section.d_timings.v_sync_width;
                info->VBlank =
                    ddc->det_mon[i].section.d_timings.v_blanking;

                return TRUE;    
            }  
        }
    }

    /* in case both EDID and BIOS probings failed, we'll try to get
       panel information from the registers. This will depends on
       how the registers are set up by bios, not very reliable*/
    mapped = RADEONMapMem(pScrn);
    RADEONMMIO = info->MMIO;

    if(info->PanelXRes==0 || info->PanelYRes==0) { 
    r = INREG(RADEON_FP_VERT_STRETCH);
    if(r & 0x08000000) {
        r &= 0x00fff000;
        info->PanelYRes = (unsigned short)(r >> 0x0c) + 1;
    } else {
        info->PanelYRes = (unsigned short)(((float)(INREG(RADEON_FP_CRTC_V_TOTAL_DISP >> 16) + 1.0) 
                           * 4096.0 / (float) (r & 0x00000fff)) + 0.5);
    }

    r = INREG(RADEON_FP_HORZ_STRETCH);
    if(r & 0x08000000) {
        r &= 0x01ff0000;
        info->PanelXRes = (unsigned short)(r >> 0x10) + 1;
		info->PanelXRes *= 8;
    } else {
        info->PanelXRes = (unsigned short)(((float)(INREG(RADEON_FP_CRTC_H_TOTAL_DISP >> 16) + 1.0) * 8.0
                           * 4096.0 / (float) (r & 0x0000ffff)) + 0.5);
    }
    
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
        "Detected panel size from registers: %dx%d\n", info->PanelXRes, info->PanelYRes);

    if ((s = xf86GetOptValString(info->Options, OPTION_PANEL_SIZE))) {
        if (sscanf(s, "%dx%d", &info->PanelXRes, &info->PanelYRes) == 2) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "Panel size: %dx%d defined in config file is used\n", 
                info->PanelXRes, info->PanelYRes);
        }
	}
    }

    if (info->PanelXRes == 0 || info->PanelYRes == 0) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "Failed to detect the panel size.\n");
		return FALSE;
    }

    if(info->HBlank == 0 || info->VBlank == 0) {
    r = INREG(RADEON_FP_CRTC_H_TOTAL_DISP);
    a = (r & RADEON_FP_CRTC_H_TOTAL_MASK) + 4;
    b = (r & 0x01FF0000) >> RADEON_FP_CRTC_H_DISP_SHIFT;
    info->HBlank = (a - b + 1) * 8;

    r = INREG(RADEON_FP_H_SYNC_STRT_WID);
    info->HOverPlus = 
        (unsigned short)((r & RADEON_FP_H_SYNC_STRT_CHAR_MASK)
        >> RADEON_FP_H_SYNC_STRT_CHAR_SHIFT) - b - 1;
    info->HOverPlus *= 8;
    info->HSyncWidth =    
        (unsigned short)((r & RADEON_FP_H_SYNC_WID_MASK)
        >> RADEON_FP_H_SYNC_WID_SHIFT);
    info->HSyncWidth *= 8;
    r = INREG(RADEON_FP_CRTC_V_TOTAL_DISP);
    a = (r & RADEON_FP_CRTC_V_TOTAL_MASK) + 1;
    b = (r & RADEON_FP_CRTC_V_DISP_MASK) >> RADEON_FP_CRTC_V_DISP_SHIFT;
    info->VBlank = a - b /*+ 24*/;
    
    r = INREG(RADEON_FP_V_SYNC_STRT_WID);
    info->VOverPlus = (unsigned short)(r & RADEON_FP_V_SYNC_STRT_MASK)
                 - b + 1;
    info->VSyncWidth = (unsigned short)((r & RADEON_FP_V_SYNC_WID_MASK)
                 >> RADEON_FP_V_SYNC_WID_SHIFT);
    }

    if(mapped) RADEONUnmapMem(pScrn);

    return TRUE;
}


/* RADEONPreInit is called once at server startup. */
Bool RADEONPreInit(ScrnInfoPtr pScrn, int flags)
{
    RADEONInfoPtr    info;
    xf86Int10InfoPtr pInt10 = NULL;
#ifdef __alpha__
    CARD32 save1, save2;
#endif

    /*
     * Tell the loader about symbols from other modules that this module might
     * refer to.
     */
    xf86LoaderRefSymLists(vgahwSymbols,
#ifdef USE_FB
		      fbSymbols,
#else
		      cfbSymbols,
#endif
		      xaaSymbols,
#if 0
		      xf8_32bppSymbols,
#endif
		      ramdacSymbols,
#ifdef XF86DRI
		      drmSymbols,
		      driSymbols,
#endif
		      fbdevHWSymbols,
		      vbeSymbols,
		      int10Symbols,
		      ddcSymbols,
		      i2cSymbols,
		      /* shadowSymbols, */
		      NULL);

    RADEONTRACE(("RADEONPreInit\n"));
    if (pScrn->numEntities != 1) return FALSE;

    if (!RADEONGetRec(pScrn)) return FALSE;

    info               = RADEONPTR(pScrn);
    info->IsSecondary  = FALSE;
    info->pEnt         = xf86GetEntityInfo(pScrn->entityList[0]);
    if (info->pEnt->location.type != BUS_PCI) goto fail;

    RADEONPreInt10Save(pScrn, &save1, &save2);

    if(xf86IsEntityShared(pScrn->entityList[0]))
    {
        if(xf86IsPrimInitDone(pScrn->entityList[0]))
        {
            DevUnion* pPriv;
            RADEONEntPtr pRADEONEnt;
            info->IsSecondary = TRUE;
            pPriv = xf86GetEntityPrivate(pScrn->entityList[0], 
                    gRADEONEntityIndex);
            pRADEONEnt = pPriv->ptr;
            if(pRADEONEnt->BypassSecondary) return FALSE;
            pRADEONEnt->pSecondaryScrn = pScrn;
        }
        else
        {
            DevUnion* pPriv;
            RADEONEntPtr pRADEONEnt;
            xf86SetPrimInitDone(pScrn->entityList[0]);
            pPriv = xf86GetEntityPrivate(pScrn->entityList[0], 
                    gRADEONEntityIndex);
            pRADEONEnt = pPriv->ptr;
            pRADEONEnt->pPrimaryScrn = pScrn;
            pRADEONEnt->IsDRIEnabled = FALSE;
            pRADEONEnt->BypassSecondary = FALSE;
            pRADEONEnt->HasSecondary = FALSE;
            pRADEONEnt->RestorePrimary = FALSE;
            pRADEONEnt->IsSecondaryRestored = FALSE;
        }
    }

    if (flags & PROBE_DETECT) 
    {
	RADEONProbeDDC(pScrn, info->pEnt->index);
	RADEONPostInt10Check(pScrn, save1, save2);
	return TRUE;
    }

    if (!xf86LoadSubModule(pScrn, "vgahw")) return FALSE;
    xf86LoaderReqSymLists(vgahwSymbols, NULL);
    if (!vgaHWGetHWRec(pScrn))
    {
	RADEONFreeRec(pScrn);
	return FALSE;
    }

    vgaHWGetIOBase(VGAHWPTR(pScrn));

    info->PciInfo      = xf86GetPciInfoForEntity(info->pEnt->index);
    info->PciTag       = pciTag(info->PciInfo->bus,
				info->PciInfo->device,
				info->PciInfo->func);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	       "PCI bus %d card %d func %d\n",
	       info->PciInfo->bus,
	       info->PciInfo->device,
	       info->PciInfo->func);

    if (xf86RegisterResources(info->pEnt->index, 0, ResExclusive)) 
        goto fail;

    pScrn->racMemFlags = RAC_FB | RAC_COLORMAP | RAC_CURSOR;
    pScrn->monitor     = pScrn->confScreen->monitor;

    if (!RADEONPreInitVisual(pScrn))    goto fail;

				/* We can't do this until we have a
				   pScrn->display. */
    xf86CollectOptions(pScrn, NULL);
    if (!(info->Options = xalloc(sizeof(RADEONOptions))))     goto fail;
    memcpy(info->Options, RADEONOptions, sizeof(RADEONOptions));
    xf86ProcessOptions(pScrn->scrnIndex, pScrn->options, info->Options);

    if (!RADEONPreInitWeight(pScrn))    goto fail;

#ifdef XvExtension
    if(xf86GetOptValInteger(info->Options, OPTION_VIDEO_KEY, &(info->videoKey))) {
        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "video key set to 0x%x\n",
                                info->videoKey);
    } else {
        /* this default is very unlikely to occur (mostly a few pixels in photos) */
	/* Unlike R128, Radeon's videokey is always stored in 32 bit ARGB format */
        info->videoKey = (1<<8) | (2<<8) | (3<<8);
    }
#endif

    if (xf86ReturnOptValBool(info->Options, OPTION_FBDEV, FALSE)) {
	info->FBDev = TRUE;
	xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
		   "Using framebuffer device\n");
    }

    info->UseCRT = FALSE;
    
    /*This option is used to force the ONLY DEVICE in XFConfig to
      use CRT port, instead of default DVI port*/
    if (xf86ReturnOptValBool(info->Options, OPTION_CRT_SCREEN, FALSE)) 
    {
        if(!xf86IsEntityShared(pScrn->entityList[0]))
	    info->UseCRT = TRUE;
    }

    if (info->FBDev) 
    {
	/* check for linux framebuffer device */
	if (!xf86LoadSubModule(pScrn, "fbdevhw")) return FALSE;
	xf86LoaderReqSymLists(fbdevHWSymbols, NULL);
	if (!fbdevHWInit(pScrn, info->PciInfo, NULL)) return FALSE;
	pScrn->SwitchMode    = fbdevHWSwitchMode;
	pScrn->AdjustFrame   = fbdevHWAdjustFrame;
	pScrn->EnterVT       = RADEONEnterVTFBDev;
	pScrn->LeaveVT       = RADEONLeaveVTFBDev;
	pScrn->ValidMode     = fbdevHWValidMode;
    }

    if (!info->FBDev)
	if (!RADEONPreInitInt10(pScrn, &pInt10)) goto fail;

    RADEONPostInt10Check(pScrn, save1, save2);

    if (!RADEONPreInitConfig(pScrn))             goto fail;

#if !defined(__powerpc__)
    if (!RADEONGetBIOSParameters(pScrn, pInt10)) goto fail;
#else
    /*
     * force type to CRT since we currently can't read BIOS to
     * tell us what kind of heads we have
     */
    info->DisplayType = MT_CRT;
#endif

    RADEONPreInitDDC(pScrn);
    info->HasEDID =
        ((pScrn->monitor->DDC = RADEONDoDDC(pScrn, pInt10)) ? TRUE:FALSE);

    if((info->DisplayType == MT_DFP) || 
       (info->DisplayType == MT_LCD))
        if(!RadeonGetDFPInfo(pScrn)) goto fail;

    if (!RADEONGetPLLParameters(pScrn))  goto fail;

    if (!RADEONPreInitGamma(pScrn))              goto fail;

    if (!RADEONPreInitModes(pScrn))              goto fail;

    if (!RADEONPreInitCursor(pScrn))             goto fail;

    if (!RADEONPreInitAccel(pScrn))              goto fail;

#ifdef XF86DRI
    if (!RADEONPreInitDRI(pScrn))                goto fail;
#endif
#if 0  /* we need it later */
				/* Free the video bios (if applicable) */
    if (info->VBIOS)
    {
	xfree(info->VBIOS);
	info->VBIOS = NULL;
    }
#endif
				/* Free int10 info */
    if (pInt10)
	xf86FreeInt10(pInt10);

    return TRUE;

fail:
				/* Pre-init failed. */

				/* Free the video bios (if applicable) */
    if (info->VBIOS)
    {
	xfree(info->VBIOS);
	info->VBIOS = NULL;
    }

				/* Free int10 info */
    if (pInt10)
	xf86FreeInt10(pInt10);

    vgaHWFreeHWRec(pScrn);
    RADEONFreeRec(pScrn);
    return FALSE;
}

/* Load a palette. */
static void RADEONLoadPalette(ScrnInfoPtr pScrn, int numColors,
			      int *indices, LOCO *colors, VisualPtr pVisual)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    int           i;
    int           idx, j;
    unsigned char r, g, b;

    /* If the second monitor is connected, we also 
       need to deal with the secondary palette*/
    if (info->IsSecondary) j = 1;
    else j = 0;
    
    PAL_SELECT(j);

    if (info->CurrentLayout.depth == 15) {
	/* 15bpp mode.  This sends 32 values. */
	for (i = 0; i < numColors; i++) {
	    idx = indices[i];
	    r   = colors[idx].red;
	    g   = colors[idx].green;
	    b   = colors[idx].blue;
	    RADEONWaitForFifo(pScrn, 32); /* delay */
	    OUTPAL(idx * 8, r, g, b);
	}
    }
    else if (info->CurrentLayout.depth == 16) {
	/* 16bpp mode.  This sends 64 values. */
				/* There are twice as many green values as
				   there are values for red and blue.  So,
				   we take each red and blue pair, and
				   combine it with each of the two green
				   values. */
	for (i = 0; i < numColors; i++) {
	    idx = indices[i];
	    r   = colors[idx / 2].red;
	    g   = colors[idx].green;
	    b   = colors[idx / 2].blue;
	    RADEONWaitForFifo(pScrn, 32); /* delay */
	    OUTPAL(idx * 4, r, g, b);

	    /* AH - Added to write extra green data - How come this isn't
	     * needed on R128 ? We didn't load the extra green data in the
	     * other routine */
	    if (idx <= 31) {
		r   = colors[idx].red;
		g   = colors[(idx * 2) + 1].green;
		b   = colors[idx].blue;
		RADEONWaitForFifo(pScrn, 32); /* delay */
		OUTPAL(idx * 8, r, g, b);
	    }
	}
    }
    else {
	/* 8bpp mode.  This sends 256 values. */
	for (i = 0; i < numColors; i++) {
	    idx = indices[i];
	    r   = colors[idx].red;
	    b   = colors[idx].blue;
	    g   = colors[idx].green;
	    RADEONWaitForFifo(pScrn, 32); /* delay */
	    OUTPAL(idx, r, g, b);
	}
    }
}

static void
RADEONBlockHandler(int i, pointer blockData, pointer pTimeout, pointer pReadmask)
{
    ScreenPtr     pScreen = screenInfo.screens[i];
    ScrnInfoPtr   pScrn   = xf86Screens[i];
    RADEONInfoPtr info    = RADEONPTR(pScrn);

#ifdef XF86DRI
    if (info->directRenderingEnabled)
        FLUSH_RING();
#endif

    pScreen->BlockHandler = info->BlockHandler;
    (*pScreen->BlockHandler) (i, blockData, pTimeout, pReadmask);
    pScreen->BlockHandler = RADEONBlockHandler;

    if(info->VideoTimerCallback) {
        (*info->VideoTimerCallback)(pScrn, currentTime.milliseconds);
    }
}

/* Called at the start of each server generation. */
Bool RADEONScreenInit(int scrnIndex, ScreenPtr pScreen, int argc, char **argv)
{
    ScrnInfoPtr   pScrn = xf86Screens[pScreen->myNum];
    RADEONInfoPtr info  = RADEONPTR(pScrn);
    BoxRec        MemBox;
    int           y2;

    RADEONTRACE(("RADEONScreenInit %x %d\n",
		 pScrn->memPhysBase, pScrn->fbOffset));

#ifdef XF86DRI
				/* Turn off the CP for now. */
    info->CPInUse      = FALSE;
#endif
    pScrn->fbOffset    = 0;
    if(info->IsSecondary) pScrn->fbOffset = pScrn->videoRam * 1024;
    if (!RADEONMapMem(pScrn)) return FALSE;

#ifdef XF86DRI
    info->fbX          = 0;
    info->fbY          = 0;
#endif

    info->PaletteSavedOnVT = FALSE;
    info->SwitchingMode = FALSE;

    RADEONSave(pScrn);
    if (info->FBDev) {
	if (!fbdevHWModeInit(pScrn, pScrn->currentMode)) return FALSE;
    } else {
	if (!RADEONModeInit(pScrn, pScrn->currentMode)) return FALSE;
    }

    RADEONSaveScreen(pScreen, SCREEN_SAVER_ON);

    pScrn->AdjustFrame(scrnIndex, pScrn->frameX0, pScrn->frameY0, 0);
				/* Visual setup */
    miClearVisualTypes();
    if (!miSetVisualTypes(pScrn->depth,
			  miGetDefaultVisualMask(pScrn->depth),
			  pScrn->rgbBits,
			  pScrn->defaultVisual)) return FALSE;
    miSetPixmapDepths ();

#ifdef XF86DRI
				/* Setup DRI after visuals have been
				   established, but before cfbScreenInit is
				   called.  cfbScreenInit will eventually
				   call the driver's InitGLXVisuals call
				   back. */
    {
	/* FIXME: When we move to dynamic allocation of back and depth
	   buffers, we will want to revisit the following check for 3
	   times the virtual size of the screen below. */
	int width_bytes = (pScrn->displayWidth *
			   info->CurrentLayout.pixel_bytes);
	int maxy        = info->FbMapSize / width_bytes;

	if (xf86ReturnOptValBool(info->Options, OPTION_NOACCEL, FALSE)) {
	    xf86DrvMsg(scrnIndex, X_WARNING,
		       "Acceleration disabled, not initializing the DRI\n");
	    info->directRenderingEnabled = FALSE;
	} else if (maxy <= pScrn->virtualY * 3) {
	    xf86DrvMsg(scrnIndex, X_WARNING,
		       "Static buffer allocation failed -- "
		       "need at least %d kB video memory\n",
		       (pScrn->displayWidth * pScrn->virtualY *
			info->CurrentLayout.pixel_bytes * 3 + 1023) / 1024);
	    info->directRenderingEnabled = FALSE;
	} else {
            if(info->IsSecondary)
                info->directRenderingEnabled = FALSE;
            else 
            {
                /* Xinerama has sync problem with DRI, disable it for now */
                if(xf86IsEntityShared(pScrn->entityList[0]))
                {
                    info->directRenderingEnabled = FALSE;
 	            xf86DrvMsg(scrnIndex, X_WARNING,
                        "Direct Rendering Disabled -- "
                        "Dual-head configuration is not working with DRI "
                        "at present.\nPlease use only one Device/Screen "
                        "section in your XFConfig file.\n");
                }
                else
                info->directRenderingEnabled =
                    RADEONDRIScreenInit(pScreen);
                if(xf86IsEntityShared(pScrn->entityList[0]))
                {
                    DevUnion* pPriv;
                    RADEONEntPtr pRADEONEnt;
                    pPriv = xf86GetEntityPrivate(pScrn->entityList[0], 
                        gRADEONEntityIndex);
                    pRADEONEnt = pPriv->ptr;
                    pRADEONEnt->IsDRIEnabled = info->directRenderingEnabled;
                }
            }
	}
    }
#endif

#ifdef USE_FB
    if (!fbScreenInit (pScreen, info->FB,
		       pScrn->virtualX, pScrn->virtualY,
		       pScrn->xDpi, pScrn->yDpi, pScrn->displayWidth,
		       pScrn->bitsPerPixel))
	return FALSE;
#else
    switch (pScrn->bitsPerPixel) {
    case 8:
	if (!cfbScreenInit(pScreen, info->FB,
			   pScrn->virtualX, pScrn->virtualY,
			   pScrn->xDpi, pScrn->yDpi, pScrn->displayWidth))
	    return FALSE;
	break;
    case 16:
	if (!cfb16ScreenInit(pScreen, info->FB,
			     pScrn->virtualX, pScrn->virtualY,
			     pScrn->xDpi, pScrn->yDpi, pScrn->displayWidth))
	    return FALSE;
	break;
    case 32:
	if (!cfb32ScreenInit(pScreen, info->FB,
			     pScrn->virtualX, pScrn->virtualY,
			     pScrn->xDpi, pScrn->yDpi, pScrn->displayWidth))
	    return FALSE;
	break;
    default:
	xf86DrvMsg(scrnIndex, X_ERROR,
		   "Invalid bpp (%d)\n", pScrn->bitsPerPixel);
	return FALSE;
    }
#endif
    xf86SetBlackWhitePixels(pScreen);

    if (pScrn->bitsPerPixel > 8) {
	VisualPtr visual;

	visual = pScreen->visuals + pScreen->numVisuals;
	while (--visual >= pScreen->visuals) {
	    if ((visual->class | DynamicClass) == DirectColor) {
		visual->offsetRed   = pScrn->offset.red;
		visual->offsetGreen = pScrn->offset.green;
		visual->offsetBlue  = pScrn->offset.blue;
		visual->redMask     = pScrn->mask.red;
		visual->greenMask   = pScrn->mask.green;
		visual->blueMask    = pScrn->mask.blue;
	    }
	}
    }
#ifdef USE_FB
    /* must be after RGB order fixed */
    fbPictureInit (pScreen, 0, 0);
#endif

				/* Memory manager setup */
#ifdef XF86DRI
    if (info->directRenderingEnabled) {
	FBAreaPtr fbarea;
	int width_bytes = (pScrn->displayWidth *
			   info->CurrentLayout.pixel_bytes);
	int cpp = info->CurrentLayout.pixel_bytes;
	int bufferSize = ((pScrn->virtualY * width_bytes + RADEON_BUFFER_ALIGN)
			  & ~RADEON_BUFFER_ALIGN);
	int l;
	int scanlines;

	info->frontOffset = 0;
	info->frontPitch = pScrn->displayWidth;

	switch (info->CPMode) {
	case RADEON_DEFAULT_CP_PIO_MODE:
	    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CP in PIO mode\n");
	    break;
	case RADEON_DEFAULT_CP_BM_MODE:
	    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CP in BM mode\n");
	    break;
	default:
	    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CP in UNKNOWN mode\n");
	    break;
	}

	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "Using %d MB AGP aperture\n", info->agpSize);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "Using %d MB for the ring buffer\n", info->ringSize);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "Using %d MB for vertex/indirect buffers\n", info->bufSize);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "Using %d MB for AGP textures\n", info->agpTexSize);

	/* Try for front, back, depth, and three framebuffers worth of
	 * pixmap cache.  Should be enough for a fullscreen background
	 * image plus some leftovers.
	 */
	info->textureSize = info->FbMapSize - 6 * bufferSize;

	/* If that gives us less than half the available memory, let's
	 * be greedy and grab some more.  Sorry, I care more about 3D
	 * performance than playing nicely, and you'll get around a full
	 * framebuffer's worth of pixmap cache anyway.
	 */
	if (info->textureSize < (int)info->FbMapSize / 2) {
	    info->textureSize = info->FbMapSize - 5 * bufferSize;
	}
	if (info->textureSize < (int)info->FbMapSize / 2) {
	    info->textureSize = info->FbMapSize - 4 * bufferSize;
	}

	/* Check to see if there is more room available after the 8192nd
	   scanline for textures */
	if ((int)info->FbMapSize - 8192*width_bytes - bufferSize*2
	    > info->textureSize) {
	    info->textureSize =
		info->FbMapSize - 8192*width_bytes - bufferSize*2;
	}

	if (info->textureSize > 0) {
	    l = RADEONMinBits((info->textureSize-1) / RADEON_NR_TEX_REGIONS);
	    if (l < RADEON_LOG_TEX_GRANULARITY) l = RADEON_LOG_TEX_GRANULARITY;

	    /* Round the texture size up to the nearest whole number of
	     * texture regions.  Again, be greedy about this, don't
	     * round down.
	     */
	    info->log2TexGran = l;
	    info->textureSize = (info->textureSize >> l) << l;
	} else {
	    info->textureSize = 0;
	}

	/* Set a minimum usable local texture heap size.  This will fit
	 * two 256x256x32bpp textures.
	 */
	if (info->textureSize < 512 * 1024) {
	    info->textureOffset = 0;
	    info->textureSize = 0;
	}

				/* Reserve space for textures */
	info->textureOffset = (info->FbMapSize - info->textureSize +
			       RADEON_BUFFER_ALIGN) &
			      ~(CARD32)RADEON_BUFFER_ALIGN;

				/* Reserve space for the shared depth buffer */
	info->depthOffset = (info->textureOffset - bufferSize +
			     RADEON_BUFFER_ALIGN) &
			    ~(CARD32)RADEON_BUFFER_ALIGN;
	info->depthPitch = pScrn->displayWidth;

				/* Reserve space for the shared back buffer */
	info->backOffset = (info->depthOffset - bufferSize +
			    RADEON_BUFFER_ALIGN) &
			   ~(CARD32)RADEON_BUFFER_ALIGN;
	info->backPitch = pScrn->displayWidth;

	scanlines = info->backOffset / width_bytes - 1;
	if (scanlines > 8191) scanlines = 8191;

	MemBox.x1 = 0;
	MemBox.y1 = 0;
	MemBox.x2 = pScrn->displayWidth;
	MemBox.y2 = scanlines;

	if (!xf86InitFBManager(pScreen, &MemBox)) {
	    xf86DrvMsg(scrnIndex, X_ERROR,
		       "Memory manager initialization to (%d,%d) (%d,%d) failed\n",
		       MemBox.x1, MemBox.y1, MemBox.x2, MemBox.y2);
	    return FALSE;
	} else {
	    int width, height;

	    xf86DrvMsg(scrnIndex, X_INFO,
		       "Memory manager initialized to (%d,%d) (%d,%d)\n",
		       MemBox.x1, MemBox.y1, MemBox.x2, MemBox.y2);
	    if ((fbarea = xf86AllocateOffscreenArea(pScreen,
						    pScrn->displayWidth,
						    2, 0, NULL, NULL, NULL))) {
		xf86DrvMsg(scrnIndex, X_INFO,
			   "Reserved area from (%d,%d) to (%d,%d)\n",
			   fbarea->box.x1, fbarea->box.y1,
			   fbarea->box.x2, fbarea->box.y2);
	    } else {
		xf86DrvMsg(scrnIndex, X_ERROR, "Unable to reserve area\n");
	    }
	    if (xf86QueryLargestOffscreenArea(pScreen, &width,
					      &height, 0, 0, 0)) {
		xf86DrvMsg(scrnIndex, X_INFO,
			   "Largest offscreen area available: %d x %d\n",
			   width, height);
	    }
	}

	xf86DrvMsg(scrnIndex, X_INFO,
		   "Reserved back buffer at offset 0x%x\n",
		   info->backOffset);
	xf86DrvMsg(scrnIndex, X_INFO,
		   "Reserved depth buffer at offset 0x%x\n",
		   info->depthOffset);
	xf86DrvMsg(scrnIndex, X_INFO,
		   "Reserved %d kb for textures at offset 0x%x\n",
		   info->textureSize/1024, info->textureOffset);

	info->frontPitchOffset = (((info->frontPitch * cpp / 64) << 22) |
				  (info->frontOffset >> 10));

	info->backPitchOffset = (((info->backPitch * cpp / 64) << 22) |
				 (info->backOffset >> 10));

	info->depthPitchOffset = (((info->depthPitch * cpp / 64) << 22) |
				  (info->depthOffset >> 10));
    }
    else
#endif
    {
	MemBox.x1 = 0;
	MemBox.y1 = 0;
	MemBox.x2 = pScrn->displayWidth;
	y2        = (info->FbMapSize
		     / (pScrn->displayWidth *
			info->CurrentLayout.pixel_bytes));
	if (y2 >= 32768) y2 = 32767; /* because MemBox.y2 is signed short */
	MemBox.y2 = y2;

				/* The acceleration engine uses 14 bit
				   signed coordinates, so we can't have any
				   drawable caches beyond this region. */
	if (MemBox.y2 > 8191) MemBox.y2 = 8191;

	if (!xf86InitFBManager(pScreen, &MemBox)) {
	    xf86DrvMsg(scrnIndex, X_ERROR,
		       "Memory manager initialization to (%d,%d) (%d,%d) failed\n",
		       MemBox.x1, MemBox.y1, MemBox.x2, MemBox.y2);
	    return FALSE;
	} else {
	    int       width, height;
	    FBAreaPtr fbarea;

	    xf86DrvMsg(scrnIndex, X_INFO,
		       "Memory manager initialized to (%d,%d) (%d,%d)\n",
		       MemBox.x1, MemBox.y1, MemBox.x2, MemBox.y2);
	    if ((fbarea = xf86AllocateOffscreenArea(pScreen, pScrn->displayWidth,
						    2, 0, NULL, NULL, NULL))) {
		xf86DrvMsg(scrnIndex, X_INFO,
			   "Reserved area from (%d,%d) to (%d,%d)\n",
			   fbarea->box.x1, fbarea->box.y1,
			   fbarea->box.x2, fbarea->box.y2);
	    } else {
		xf86DrvMsg(scrnIndex, X_ERROR, "Unable to reserve area\n");
	    }
	    if (xf86QueryLargestOffscreenArea(pScreen, &width, &height,
					      0, 0, 0)) {
		xf86DrvMsg(scrnIndex, X_INFO,
			   "Largest offscreen area available: %d x %d\n",
			   width, height);
	    }
	}
    }
				/* Backing store setup */
    miInitializeBackingStore(pScreen);
    xf86SetBackingStore(pScreen);

				/* Set Silken Mouse */
    xf86SetSilkenMouse(pScreen);

				/* Acceleration setup */
    if (!xf86ReturnOptValBool(info->Options, OPTION_NOACCEL, FALSE)) {
	if (RADEONAccelInit(pScreen)) {
	    xf86DrvMsg(scrnIndex, X_INFO, "Acceleration enabled\n");
	    info->accelOn = TRUE;
	} else {
	    xf86DrvMsg(scrnIndex, X_ERROR,
		       "Acceleration initialization failed\n");
	    xf86DrvMsg(scrnIndex, X_INFO, "Acceleration disabled\n");
	    info->accelOn = FALSE;
	}
    } else {
	xf86DrvMsg(scrnIndex, X_INFO, "Acceleration disabled\n");
	info->accelOn = FALSE;
    }

				/* DGA setup */
    RADEONDGAInit(pScreen);

				/* Cursor setup */
    miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

				/* Hardware cursor setup */
    if (!xf86ReturnOptValBool(info->Options, OPTION_SW_CURSOR, FALSE)) {
	if (RADEONCursorInit(pScreen)) {
	    int width, height;

	    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		       "Using hardware cursor (scanline %d)\n",
		       info->cursor_start / pScrn->displayWidth);
	    if (xf86QueryLargestOffscreenArea(pScreen, &width, &height,
					      0, 0, 0)) {
		xf86DrvMsg(scrnIndex, X_INFO,
			   "Largest offscreen area available: %d x %d\n",
			   width, height);
	    }
	} else {
	    xf86DrvMsg(scrnIndex, X_ERROR,
		       "Hardware cursor initialization failed\n");
	    xf86DrvMsg(scrnIndex, X_INFO, "Using software cursor\n");
	}
    } else {
	xf86DrvMsg(scrnIndex, X_INFO, "Using software cursor\n");
    }

				/* Colormap setup */
    if (!miCreateDefColormap(pScreen)) return FALSE;
    if (!xf86HandleColormaps(pScreen, 256, info->dac6bits ? 6 : 8,
			     (info->FBDev ? fbdevHWLoadPalette :
			     RADEONLoadPalette), NULL,
			     CMAP_PALETTED_TRUECOLOR
			     | CMAP_RELOAD_ON_MODE_SWITCH
#if 0 /* This option messes up text mode! (eich@suse.de) */
			     | CMAP_LOAD_EVEN_IF_OFFSCREEN
#endif
			     )) return FALSE;

				/* DPMS setup */
#ifdef DPMSExtension
    if (info->DisplayType == MT_CRT)
	xf86DPMSInit(pScreen, RADEONDisplayPowerManagementSet, 0);
#endif

    RADEONInitVideo(pScreen);
				/* Provide SaveScreen */
    pScreen->SaveScreen  = RADEONSaveScreen;

				/* Wrap CloseScreen */
    info->CloseScreen    = pScreen->CloseScreen;
    pScreen->CloseScreen = RADEONCloseScreen;

				/* Note unused options */
    if (serverGeneration == 1)
	xf86ShowUnusedOptions(pScrn->scrnIndex, pScrn->options);

#ifdef XF86DRI
				/* DRI finalization */
    if (info->directRenderingEnabled) {
				/* Now that mi, cfb, drm and others have
				   done their thing, complete the DRI
				   setup. */
	info->directRenderingEnabled = RADEONDRIFinishScreenInit(pScreen);
    }
    if (info->directRenderingEnabled) {
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Direct rendering enabled\n");
    } else {
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Direct rendering disabled\n");
    }
#endif

    info->BlockHandler = pScreen->BlockHandler;
    pScreen->BlockHandler = RADEONBlockHandler;

    return TRUE;
}

/* Write common registers (initialized to 0). */
static void RADEONRestoreCommonRegisters(ScrnInfoPtr pScrn,
					 RADEONSavePtr restore)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;

    OUTREG(RADEON_OVR_CLR,              restore->ovr_clr);
    OUTREG(RADEON_OVR_WID_LEFT_RIGHT,   restore->ovr_wid_left_right);
    OUTREG(RADEON_OVR_WID_TOP_BOTTOM,   restore->ovr_wid_top_bottom);
    OUTREG(RADEON_OV0_SCALE_CNTL,       restore->ov0_scale_cntl);
    OUTREG(RADEON_MPP_TB_CONFIG,        restore->mpp_tb_config );
    OUTREG(RADEON_MPP_GP_CONFIG,        restore->mpp_gp_config );
    OUTREG(RADEON_SUBPIC_CNTL,          restore->subpic_cntl);
    OUTREG(RADEON_VIPH_CONTROL,         restore->viph_control);
    OUTREG(RADEON_I2C_CNTL_1,           restore->i2c_cntl_1);
    OUTREG(RADEON_GEN_INT_CNTL,         restore->gen_int_cntl);
    OUTREG(RADEON_CAP0_TRIG_CNTL,       restore->cap0_trig_cntl);
    OUTREG(RADEON_CAP1_TRIG_CNTL,       restore->cap1_trig_cntl);
    OUTREG(RADEON_BUS_CNTL,             restore->bus_cntl);
    OUTREG(RADEON_SURFACE_CNTL,		restore->surface_cntl);
}

/* Write CRTC registers. */
static void RADEONRestoreCrtcRegisters(ScrnInfoPtr pScrn,
				       RADEONSavePtr restore)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;

    OUTREG(RADEON_CRTC_GEN_CNTL,        restore->crtc_gen_cntl);

    OUTREGP(RADEON_CRTC_EXT_CNTL, restore->crtc_ext_cntl,
	    RADEON_CRTC_VSYNC_DIS |
	    RADEON_CRTC_HSYNC_DIS |
	    RADEON_CRTC_DISPLAY_DIS);

    OUTREGP(RADEON_DAC_CNTL, restore->dac_cntl,
	    RADEON_DAC_RANGE_CNTL |
	    RADEON_DAC_BLANKING);

    OUTREG(RADEON_CRTC_H_TOTAL_DISP,    restore->crtc_h_total_disp);
    OUTREG(RADEON_CRTC_H_SYNC_STRT_WID, restore->crtc_h_sync_strt_wid);
    OUTREG(RADEON_CRTC_V_TOTAL_DISP,    restore->crtc_v_total_disp);
    OUTREG(RADEON_CRTC_V_SYNC_STRT_WID, restore->crtc_v_sync_strt_wid);
    OUTREG(RADEON_CRTC_OFFSET,          restore->crtc_offset);
    OUTREG(RADEON_CRTC_OFFSET_CNTL,     restore->crtc_offset_cntl);
    OUTREG(RADEON_CRTC_PITCH,           restore->crtc_pitch);
}

/* Write CRTC2 registers. */
static void RADEONRestoreCrtc2Registers(ScrnInfoPtr pScrn,
				       RADEONSavePtr restore)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;

/*    OUTREG(RADEON_CRTC2_GEN_CNTL,  restore->crtc2_gen_cntl);*/
    OUTREGP(RADEON_CRTC2_GEN_CNTL, restore->crtc2_gen_cntl,
	    RADEON_CRTC2_VSYNC_DIS |
	    RADEON_CRTC2_HSYNC_DIS |
	    RADEON_CRTC2_DISP_DIS);

    OUTREG(RADEON_DAC_CNTL2, restore->dac2_cntl);
    OUTREG(RADEON_DISP_OUTPUT_CNTL, restore->disp_output_cntl);

    OUTREG(RADEON_CRTC2_H_TOTAL_DISP,    restore->crtc2_h_total_disp);
    OUTREG(RADEON_CRTC2_H_SYNC_STRT_WID, restore->crtc2_h_sync_strt_wid);
    OUTREG(RADEON_CRTC2_V_TOTAL_DISP,    restore->crtc2_v_total_disp);
    OUTREG(RADEON_CRTC2_V_SYNC_STRT_WID, restore->crtc2_v_sync_strt_wid);
    OUTREG(RADEON_CRTC2_OFFSET,          restore->crtc2_offset);
    OUTREG(RADEON_CRTC2_OFFSET_CNTL,     restore->crtc2_offset_cntl);
    OUTREG(RADEON_CRTC2_PITCH,           restore->crtc2_pitch);

}

/* Note: Radeon flat panel support has been disabled for now */
/* Write flat panel registers */
static void RADEONRestoreFPRegisters(ScrnInfoPtr pScrn, RADEONSavePtr restore)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    unsigned long tmp;

    OUTREG(RADEON_FP_CRTC_H_TOTAL_DISP, restore->fp_crtc_h_total_disp);
    OUTREG(RADEON_FP_CRTC_V_TOTAL_DISP, restore->fp_crtc_v_total_disp);
    OUTREG(RADEON_FP_H_SYNC_STRT_WID,   restore->fp_h_sync_strt_wid);
    OUTREG(RADEON_FP_V_SYNC_STRT_WID,   restore->fp_v_sync_strt_wid);
    OUTREG(RADEON_TMDS_CRC,             restore->tmds_crc);
    OUTREG(RADEON_FP_HORZ_STRETCH,      restore->fp_horz_stretch);
    OUTREG(RADEON_FP_VERT_STRETCH,      restore->fp_vert_stretch);
    OUTREG(RADEON_FP_GEN_CNTL,          restore->fp_gen_cntl);

    if(info->DisplayType == MT_LCD)
    {
    tmp = INREG(RADEON_LVDS_GEN_CNTL);
    if ((tmp & (RADEON_LVDS_ON | RADEON_LVDS_BLON)) ==
	    (restore->lvds_gen_cntl & (RADEON_LVDS_ON | RADEON_LVDS_BLON))) 
        {
	OUTREG(RADEON_LVDS_GEN_CNTL, restore->lvds_gen_cntl);
        } 
        else 
        {
	   if (restore->lvds_gen_cntl & 
               (RADEON_LVDS_ON | RADEON_LVDS_BLON)) 
           {
	    usleep(RADEONPTR(pScrn)->PanelPwrDly * 1000);
	    OUTREG(RADEON_LVDS_GEN_CNTL, restore->lvds_gen_cntl);
            } 
            else 
            {
	    OUTREG(RADEON_LVDS_GEN_CNTL,
		   restore->lvds_gen_cntl | RADEON_LVDS_BLON);
	    usleep(RADEONPTR(pScrn)->PanelPwrDly * 1000);
	    OUTREG(RADEON_LVDS_GEN_CNTL, restore->lvds_gen_cntl);
	}
    }
    }
}

#if RADEON_ATOMIC_UPDATE
static void RADEONPLLWaitForReadUpdateComplete(ScrnInfoPtr pScrn)
{
    while (INPLL(pScrn, RADEON_PPLL_REF_DIV) & RADEON_PPLL_ATOMIC_UPDATE_R);
}

static void RADEONPLLWriteUpdate(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;

    OUTPLLP(pScrn, RADEON_PPLL_REF_DIV, RADEON_PPLL_ATOMIC_UPDATE_W, 0xffff);
}
#endif

/* Write PLL registers. */
static void RADEONRestorePLLRegisters(ScrnInfoPtr pScrn, RADEONSavePtr restore)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;

    OUTPLLP(pScrn, 0x08, 0x00, ~(0x03));

#if !RADEON_ATOMIC_UPDATE
    while ( (INREG(RADEON_CLOCK_CNTL_INDEX) & RADEON_PLL_DIV_SEL) !=
						RADEON_PLL_DIV_SEL) {
#endif
	OUTREGP(RADEON_CLOCK_CNTL_INDEX, RADEON_PLL_DIV_SEL, 0xffff);
#if !RADEON_ATOMIC_UPDATE
    }
#endif

#if RADEON_ATOMIC_UPDATE
    OUTPLLP(pScrn,
	    RADEON_PPLL_CNTL,
	    RADEON_PPLL_RESET
	    | RADEON_PPLL_ATOMIC_UPDATE_EN
	    | RADEON_PPLL_VGA_ATOMIC_UPDATE_EN,
	    0xffff);
#else
    OUTPLLP(pScrn,
	    RADEON_PPLL_CNTL,
	    RADEON_PPLL_RESET,
	    0xffff);
#endif

#if RADEON_ATOMIC_UPDATE
    RADEONPLLWaitForReadUpdateComplete(pScrn);
#endif
    while ( (INPLL(pScrn, RADEON_PPLL_REF_DIV) & RADEON_PPLL_REF_DIV_MASK) !=
			(restore->ppll_ref_div & RADEON_PPLL_REF_DIV_MASK)) {
	OUTPLLP(pScrn, RADEON_PPLL_REF_DIV,
			restore->ppll_ref_div, ~RADEON_PPLL_REF_DIV_MASK);
    }
#if RADEON_ATOMIC_UPDATE
    RADEONPLLWriteUpdate(pScrn);
#endif

#if RADEON_ATOMIC_UPDATE
    RADEONPLLWaitForReadUpdateComplete(pScrn);
#endif
    while ( (INPLL(pScrn, RADEON_PPLL_DIV_3) & RADEON_PPLL_FB3_DIV_MASK) !=
			(restore->ppll_div_3 & RADEON_PPLL_FB3_DIV_MASK)) {
	OUTPLLP(pScrn, RADEON_PPLL_DIV_3,
			restore->ppll_div_3, ~RADEON_PPLL_FB3_DIV_MASK);
    }
#if RADEON_ATOMIC_UPDATE
    RADEONPLLWriteUpdate(pScrn);
#endif

#if RADEON_ATOMIC_UPDATE
    RADEONPLLWaitForReadUpdateComplete(pScrn);
#endif
    while ( (INPLL(pScrn, RADEON_PPLL_DIV_3) & RADEON_PPLL_POST3_DIV_MASK) !=
			(restore->ppll_div_3 & RADEON_PPLL_POST3_DIV_MASK)) {
	OUTPLLP(pScrn, RADEON_PPLL_DIV_3,
			restore->ppll_div_3, ~RADEON_PPLL_POST3_DIV_MASK);
    }
#if RADEON_ATOMIC_UPDATE
    RADEONPLLWriteUpdate(pScrn);
#endif

#if RADEON_ATOMIC_UPDATE
    RADEONPLLWaitForReadUpdateComplete(pScrn);
#endif
    OUTPLL(RADEON_HTOTAL_CNTL, restore->htotal_cntl);
#if RADEON_ATOMIC_UPDATE
    RADEONPLLWriteUpdate(pScrn);
#endif

    OUTPLLP(pScrn, RADEON_PPLL_CNTL, 0, ~RADEON_PPLL_RESET);

    RADEONTRACE(("Wrote: 0x%08x 0x%08x 0x%08x (0x%08x)\n",
	       restore->ppll_ref_div,
	       restore->ppll_div_3,
	       restore->htotal_cntl,
	       INPLL(pScrn, RADEON_PPLL_CNTL)));
    RADEONTRACE(("Wrote: rd=%d, fd=%d, pd=%d\n",
	       restore->ppll_ref_div & RADEON_PPLL_REF_DIV_MASK,
	       restore->ppll_div_3 & RADEON_PPLL_FB3_DIV_MASK,
	       (restore->ppll_div_3 & RADEON_PPLL_POST3_DIV_MASK) >> 16));

    OUTPLLP(pScrn, 0x08, 0x03, ~(0x03));

}


/* Write PLL2 registers. */
static void RADEONRestorePLL2Registers(ScrnInfoPtr pScrn, RADEONSavePtr restore)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;

    OUTPLLP(pScrn, 0x2d, 0x00, ~(0x03));


#if !RADEON_ATOMIC_UPDATE
    while (INREG(RADEON_CLOCK_CNTL_INDEX) & ~(RADEON_PLL2_DIV_SEL_MASK)) {
#endif
	OUTREGP(RADEON_CLOCK_CNTL_INDEX, 0, RADEON_PLL2_DIV_SEL_MASK);
#if !RADEON_ATOMIC_UPDATE
    }
#endif


#if RADEON_ATOMIC_UPDATE
    OUTPLLP(pScrn,
	    RADEON_P2PLL_CNTL,
	    RADEON_P2PLL_RESET
	    | RADEON_P2PLL_ATOMIC_UPDATE_EN
	    | RADEON_P2PLL_VGA_ATOMIC_UPDATE_EN,
	    0xffff);
#else
    OUTPLLP(pScrn,
	    RADEON_P2PLL_CNTL,
	    RADEON_P2PLL_RESET,
	    0xffff);
#endif

#if RADEON_ATOMIC_UPDATE
    RADEONPLLWaitForReadUpdateComplete(pScrn);
#endif
    while ( (INPLL(pScrn, RADEON_P2PLL_REF_DIV) & RADEON_P2PLL_REF_DIV_MASK) !=
			(restore->p2pll_ref_div & RADEON_P2PLL_REF_DIV_MASK)) {
	OUTPLLP(pScrn, RADEON_P2PLL_REF_DIV, restore->p2pll_ref_div, ~RADEON_P2PLL_REF_DIV_MASK);
    }
#if RADEON_ATOMIC_UPDATE
    RADEONPLLWriteUpdate(pScrn);
#endif

#if RADEON_ATOMIC_UPDATE
    RADEONPLLWaitForReadUpdateComplete(pScrn);
#endif
    while ( (INPLL(pScrn, RADEON_P2PLL_DIV_0) & RADEON_P2PLL_FB0_DIV_MASK) !=
			(restore->p2pll_div_0 & 
RADEON_P2PLL_FB0_DIV_MASK)) {
	OUTPLLP(pScrn, RADEON_P2PLL_DIV_0,
			restore->p2pll_div_0, ~RADEON_P2PLL_FB0_DIV_MASK);
    }
#if RADEON_ATOMIC_UPDATE
    RADEONPLLWriteUpdate(pScrn);
#endif

#if RADEON_ATOMIC_UPDATE
    RADEONPLLWaitForReadUpdateComplete(pScrn);
#endif
    while ( (INPLL(pScrn, RADEON_P2PLL_DIV_0) & RADEON_P2PLL_POST0_DIV_MASK) !=
			(restore->p2pll_div_0 & RADEON_P2PLL_POST0_DIV_MASK)) {
	OUTPLLP(pScrn, RADEON_P2PLL_DIV_0,
			restore->p2pll_div_0, ~RADEON_P2PLL_POST0_DIV_MASK);
    }
#if RADEON_ATOMIC_UPDATE
    RADEONPLLWriteUpdate(pScrn);
#endif

#if RADEON_ATOMIC_UPDATE
    RADEONPLLWaitForReadUpdateComplete(pScrn);
#endif
    OUTPLL(RADEON_HTOTAL2_CNTL, restore->htotal_cntl2);
#if RADEON_ATOMIC_UPDATE
    RADEONPLLWriteUpdate(pScrn);
#endif

    OUTPLLP(pScrn, RADEON_P2PLL_CNTL, 0, 
           ~(RADEON_P2PLL_RESET | RADEON_P2PLL_SLEEP));

    RADEONTRACE(("Wrote: 0x%08x 0x%08x 0x%08x (0x%08x)\n",
	       restore->p2pll_ref_div,
	       restore->p2pll_div_0,
	       restore->htotal_cntl2,
	       INPLL(pScrn, RADEON_P2PLL_CNTL)));
    RADEONTRACE(("Wrote: rd=%d, fd=%d, pd=%d\n",
	       restore->p2pll_ref_div & RADEON_P2PLL_REF_DIV_MASK,
	       restore->p2pll_div_0 & RADEON_P2PLL_FB3_DIV_MASK,
	       (restore->p2pll_div_0 & RADEON_P2PLL_POST3_DIV_MASK) >>16));

    OUTPLLP(pScrn, 0x2d, 0x03, ~(0x03));


}

/* Write DDA registers. */
static void RADEONRestoreDDARegisters(ScrnInfoPtr pScrn, RADEONSavePtr restore)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;

    OUTREG(RADEON_DDA_CONFIG, restore->dda_config);
    OUTREG(RADEON_DDA_ON_OFF, restore->dda_on_off);
}

/* Write palette data. */
static void RADEONRestorePalette(ScrnInfoPtr pScrn, RADEONSavePtr restore)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    int           i;

    if (!restore->palette_valid) return;

    PAL_SELECT(1);
    OUTPAL_START(0);
    for (i = 0; i < 256; i++) {
	RADEONWaitForFifo(pScrn, 32); /* delay */
	OUTPAL_NEXT_CARD32(restore->palette2[i]);
    }

    PAL_SELECT(0);
    OUTPAL_START(0);
    for (i = 0; i < 256; i++) {
	RADEONWaitForFifo(pScrn, 32); /* delay */
	OUTPAL_NEXT_CARD32(restore->palette[i]);
    }
}

/* Write out state to define a new video mode.  */
static void
RADEONRestoreMode(ScrnInfoPtr pScrn, RADEONSavePtr restore) 
{
    RADEONInfoPtr info = RADEONPTR(pScrn);
    DevUnion* pPriv;
    RADEONEntPtr pRADEONEnt;
    static RADEONSaveRec restore0;

    /* for Non-dual head card, we don't have private field in the Entity*/
    if(!info->HasCRTC2)
    {
    RADEONRestoreCommonRegisters(pScrn, restore);
        RADEONRestoreDDARegisters(pScrn, restore);
    RADEONRestoreCrtcRegisters(pScrn, restore);
        if((info->DisplayType == MT_DFP) || 
           (info->DisplayType == MT_LCD))
        {
	RADEONRestoreFPRegisters(pScrn, restore);
        }
    RADEONRestorePLLRegisters(pScrn, restore);
        return;
    }       

    pPriv = xf86GetEntityPrivate(pScrn->entityList[0], 
                   gRADEONEntityIndex);
    pRADEONEnt = pPriv->ptr;
   
    RADEONTRACE(("RADEONRestoreMode(%p)\n", restore));

    /*****
      When changing mode with Dual-head card (VE/M6), care must
      be taken for the special order in setting registers. CRTC2 has
      to be set before changing CRTC_EXT register.
      In the dual-head setup, X server calls this routine twice with
      primary and secondary pScrn pointers respectively. The calls
      can come with different order. Regardless the order of X server issuing 
      the calls, we have to ensure we set registers in the right order!!! 
      Otherwise we may get a blank screen.
    *****/
    if(info->IsSecondary)
    {
        RADEONRestoreCrtc2Registers(pScrn, restore);        
        RADEONRestorePLL2Registers(pScrn, restore);
        
        if(!info->SwitchingMode)
        pRADEONEnt->IsSecondaryRestored = TRUE;

        if(pRADEONEnt->RestorePrimary)
        {
            RADEONInfoPtr info0 = RADEONPTR(pRADEONEnt->pPrimaryScrn); 
            pRADEONEnt->RestorePrimary = FALSE;

            RADEONRestoreCrtcRegisters(pScrn, &restore0);
            if((info0->DisplayType == MT_DFP) || 
               (info0->DisplayType == MT_LCD))
            {
                RADEONRestoreFPRegisters(pScrn, &restore0);
            }
            
            RADEONRestorePLLRegisters(pScrn, &restore0);   
            pRADEONEnt->IsSecondaryRestored = FALSE;

        }
    }
    else
    {
        RADEONRestoreCommonRegisters(pScrn, restore);
        RADEONRestoreDDARegisters(pScrn, restore);
        if(!pRADEONEnt->HasSecondary || pRADEONEnt->IsSecondaryRestored
            || info->SwitchingMode)
        {
	    pRADEONEnt->IsSecondaryRestored = FALSE;
            RADEONRestoreCrtcRegisters(pScrn, restore);
            if((info->DisplayType == MT_DFP) || 
               (info->DisplayType == MT_LCD))
            {
               RADEONRestoreFPRegisters(pScrn, restore);
            }
            RADEONRestorePLLRegisters(pScrn, restore);   
        }
        else
        {
            memcpy(&restore0, restore, sizeof(restore0));
            pRADEONEnt->RestorePrimary = TRUE;
        }
    }

    /* if only one screen is used, we should turn off
       the unused screen, not working for now */
    /*
    if(!xf86IsEntityShared(pScrn->entityList[0]))
    {
        RADEONSetDisplayOff(pScrn, 1, 1);
    }
    */

    /*RADEONRestorePalette(pScrn, &info->SavedReg);*/
}

/* Read common registers. */
static void RADEONSaveCommonRegisters(ScrnInfoPtr pScrn, RADEONSavePtr save)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;

    save->ovr_clr            = INREG(RADEON_OVR_CLR);
    save->ovr_wid_left_right = INREG(RADEON_OVR_WID_LEFT_RIGHT);
    save->ovr_wid_top_bottom = INREG(RADEON_OVR_WID_TOP_BOTTOM);
    save->ov0_scale_cntl     = INREG(RADEON_OV0_SCALE_CNTL);
    save->mpp_tb_config      = INREG(RADEON_MPP_TB_CONFIG);
    save->mpp_gp_config      = INREG(RADEON_MPP_GP_CONFIG);
    save->subpic_cntl        = INREG(RADEON_SUBPIC_CNTL);
    save->viph_control       = INREG(RADEON_VIPH_CONTROL);
    save->i2c_cntl_1         = INREG(RADEON_I2C_CNTL_1);
    save->gen_int_cntl       = INREG(RADEON_GEN_INT_CNTL);
    save->cap0_trig_cntl     = INREG(RADEON_CAP0_TRIG_CNTL);
    save->cap1_trig_cntl     = INREG(RADEON_CAP1_TRIG_CNTL);
    save->bus_cntl           = INREG(RADEON_BUS_CNTL);
    save->surface_cntl	     = INREG(RADEON_SURFACE_CNTL);
}

/* Read CRTC registers. */
static void RADEONSaveCrtcRegisters(ScrnInfoPtr pScrn, RADEONSavePtr save)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;

    save->crtc_gen_cntl        = INREG(RADEON_CRTC_GEN_CNTL);
    save->crtc_ext_cntl        = INREG(RADEON_CRTC_EXT_CNTL);
    save->dac_cntl             = INREG(RADEON_DAC_CNTL);
    save->crtc_h_total_disp    = INREG(RADEON_CRTC_H_TOTAL_DISP);
    save->crtc_h_sync_strt_wid = INREG(RADEON_CRTC_H_SYNC_STRT_WID);
    save->crtc_v_total_disp    = INREG(RADEON_CRTC_V_TOTAL_DISP);
    save->crtc_v_sync_strt_wid = INREG(RADEON_CRTC_V_SYNC_STRT_WID);
    save->crtc_offset          = INREG(RADEON_CRTC_OFFSET);
    save->crtc_offset_cntl     = INREG(RADEON_CRTC_OFFSET_CNTL);
    save->crtc_pitch           = INREG(RADEON_CRTC_PITCH);

}

/* Note: Radeon flat panel support has been disabled for now */
/* Read flat panel registers */
static void RADEONSaveFPRegisters(ScrnInfoPtr pScrn, RADEONSavePtr save)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;

    save->fp_crtc_h_total_disp = INREG(RADEON_FP_CRTC_H_TOTAL_DISP);
    save->fp_crtc_v_total_disp = INREG(RADEON_FP_CRTC_V_TOTAL_DISP);
    save->fp_gen_cntl          = INREG(RADEON_FP_GEN_CNTL);
    save->fp_h_sync_strt_wid   = INREG(RADEON_FP_H_SYNC_STRT_WID);
    save->fp_horz_stretch      = INREG(RADEON_FP_HORZ_STRETCH);
    save->fp_v_sync_strt_wid   = INREG(RADEON_FP_V_SYNC_STRT_WID);
    save->fp_vert_stretch      = INREG(RADEON_FP_VERT_STRETCH);
    save->lvds_gen_cntl        = INREG(RADEON_LVDS_GEN_CNTL);
    save->lvds_pll_cntl        = INREG(RADEON_LVDS_PLL_CNTL);
    save->tmds_crc             = INREG(RADEON_TMDS_CRC);
}

/* Read CRTC2 registers. */
static void RADEONSaveCrtc2Registers(ScrnInfoPtr pScrn, RADEONSavePtr
save)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;

    save->dac2_cntl             = INREG(RADEON_DAC_CNTL2);
    save->disp_output_cntl      = INREG(RADEON_DISP_OUTPUT_CNTL);

    save->crtc2_gen_cntl        = INREG(RADEON_CRTC2_GEN_CNTL);
    save->crtc2_h_total_disp    = INREG(RADEON_CRTC2_H_TOTAL_DISP);
    save->crtc2_h_sync_strt_wid = INREG(RADEON_CRTC2_H_SYNC_STRT_WID);
    save->crtc2_v_total_disp    = INREG(RADEON_CRTC2_V_TOTAL_DISP);
    save->crtc2_v_sync_strt_wid = INREG(RADEON_CRTC2_V_SYNC_STRT_WID);
    save->crtc2_offset          = INREG(RADEON_CRTC2_OFFSET);
    save->crtc2_offset_cntl     = INREG(RADEON_CRTC2_OFFSET_CNTL);
    save->crtc2_pitch           = INREG(RADEON_CRTC2_PITCH);
}

/* Read PLL registers. */
static void RADEONSavePLLRegisters(ScrnInfoPtr pScrn, RADEONSavePtr save)
{
    save->ppll_ref_div         = INPLL(pScrn, RADEON_PPLL_REF_DIV);
    save->ppll_div_3           = INPLL(pScrn, RADEON_PPLL_DIV_3);
    save->htotal_cntl          = INPLL(pScrn, RADEON_HTOTAL_CNTL);

    RADEONTRACE(("Read: 0x%08x 0x%08x 0x%08x\n",
	       save->ppll_ref_div,
	       save->ppll_div_3,
	       save->htotal_cntl));
    RADEONTRACE(("Read: rd=%d, fd=%d, pd=%d\n",
	       save->ppll_ref_div & RADEON_PPLL_REF_DIV_MASK,
	       save->ppll_div_3 & RADEON_PPLL_FB3_DIV_MASK,
	       (save->ppll_div_3 & RADEON_PPLL_POST3_DIV_MASK) >> 16));
}

/* Read PLL registers. */
static void RADEONSavePLL2Registers(ScrnInfoPtr pScrn, RADEONSavePtr save)
{
    save->p2pll_ref_div        = INPLL(pScrn, RADEON_P2PLL_REF_DIV);
    save->p2pll_div_0          = INPLL(pScrn, RADEON_P2PLL_DIV_0);
    save->htotal_cntl2         = INPLL(pScrn, RADEON_HTOTAL2_CNTL);

    RADEONTRACE(("Read: 0x%08x 0x%08x 0x%08x\n",
	       save->p2pll_ref_div,
	       save->p2pll_div_0,
	       save->htotal_cntl2));
    RADEONTRACE(("Read: rd=%d, fd=%d, pd=%d\n",
	       save->p2pll_ref_div & RADEON_P2PLL_REF_DIV_MASK,
	       save->p2pll_div_0 & RADEON_P2PLL_FB0_DIV_MASK,
	       (save->p2pll_div_0 & RADEON_P2PLL_POST0_DIV_MASK) >> 16));
}

/* Read DDA registers. */
static void RADEONSaveDDARegisters(ScrnInfoPtr pScrn, RADEONSavePtr save)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;

    save->dda_config           = INREG(RADEON_DDA_CONFIG);
    save->dda_on_off           = INREG(RADEON_DDA_ON_OFF);
}

/* Read palette data. */
static void RADEONSavePalette(ScrnInfoPtr pScrn, RADEONSavePtr save)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    int           i;

#ifdef ENABLE_FLAT_PANEL
    /* Select palette 0 (main CRTC) if using FP-enabled chip */
    /*if (info->Port1 == MT_DFP) PAL_SELECT(1);*/
#endif
    PAL_SELECT(1);
    INPAL_START(0);
    for (i = 0; i < 256; i++) save->palette2[i] = INPAL_NEXT();
    PAL_SELECT(0);
    INPAL_START(0);
    for (i = 0; i < 256; i++) save->palette[i] = INPAL_NEXT();
    save->palette_valid = TRUE;
}

/* Save state that defines current video mode. */
static void RADEONSaveMode(ScrnInfoPtr pScrn, RADEONSavePtr save)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);

    RADEONTRACE(("RADEONSaveMode(%p)\n", save));
    if(info->IsSecondary)
    {
        RADEONSaveCrtc2Registers(pScrn, save);
        RADEONSavePLL2Registers(pScrn, save);
    }
    else
    {
    RADEONSaveCommonRegisters(pScrn, save);
    RADEONSaveCrtcRegisters(pScrn, save);
        if((info->DisplayType == MT_DFP) || 
           (info->DisplayType == MT_LCD))
        {
	RADEONSaveFPRegisters(pScrn, save);
        }
    RADEONSavePLLRegisters(pScrn, save);
    RADEONSaveDDARegisters(pScrn, save);
        /*RADEONSavePalette(pScrn, save);*/
    }
    RADEONTRACE(("RADEONSaveMode returns %p\n", save));
}

/* Save everything needed to restore the original VC state. */
static void RADEONSave(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    RADEONSavePtr save        = &info->SavedReg;
    vgaHWPtr      hwp         = VGAHWPTR(pScrn);

    RADEONTRACE(("RADEONSave\n"));
    if (info->FBDev) {
	fbdevHWSave(pScrn);
	return;
    }

    if(!info->IsSecondary)
    {
    vgaHWUnlock(hwp);
#if defined(__powerpc__)
    /* temporary hack to prevent crashing on PowerMacs when trying to
     * read VGA fonts and colormap, will find a better solution
     * in the future
     */
    vgaHWSave(pScrn, &hwp->SavedReg, VGA_SR_MODE); /* save mode only */
#else
    vgaHWSave(pScrn, &hwp->SavedReg, VGA_SR_ALL); /* save mode, fonts,cmap */
#endif
    vgaHWLock(hwp);
    save->dp_datatype      = INREG(RADEON_DP_DATATYPE);
    save->rbbm_soft_reset  = INREG(RADEON_RBBM_SOFT_RESET);
    save->clock_cntl_index = INREG(RADEON_CLOCK_CNTL_INDEX);
    save->amcgpio_en_reg   = INREG(RADEON_AMCGPIO_EN_REG);
    save->amcgpio_mask     = INREG(RADEON_AMCGPIO_MASK);
    }
        
    RADEONSaveMode(pScrn, save);
}

/* Restore the original (text) mode. */
static void RADEONRestore(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    RADEONSavePtr restore     = &info->SavedReg;
    vgaHWPtr      hwp         = VGAHWPTR(pScrn);

    RADEONTRACE(("RADEONRestore\n"));
    if (info->FBDev) {
	fbdevHWRestore(pScrn);
	return;
    }
    RADEONBlank(pScrn);

    OUTREG(RADEON_AMCGPIO_MASK,     restore->amcgpio_mask);
    OUTREG(RADEON_AMCGPIO_EN_REG,   restore->amcgpio_en_reg);
    OUTREG(RADEON_CLOCK_CNTL_INDEX, restore->clock_cntl_index);
    OUTREG(RADEON_RBBM_SOFT_RESET,  restore->rbbm_soft_reset);
    OUTREG(RADEON_DP_DATATYPE,      restore->dp_datatype);

    /* M6 card has trouble restoring text mode for its CRT.
       Needs this workaround.*/
    if(xf86IsEntityShared(pScrn->entityList[0]) && info->IsM6)
        OUTREG(RADEON_DAC_CNTL2, restore->dac2_cntl);

    RADEONRestoreMode(pScrn, restore);
    if(!info->IsSecondary)
    {
    vgaHWUnlock(hwp);
#if defined(__powerpc__)
    /* temporary hack to prevent crashing on PowerMacs when trying to
     * write VGA fonts, will find a better solution in the future
     */
    vgaHWRestore(pScrn, &hwp->SavedReg, VGA_SR_MODE );
#else
    vgaHWRestore(pScrn, &hwp->SavedReg, VGA_SR_MODE | VGA_SR_FONTS );
#endif
    vgaHWLock(hwp);

    }
    else
    {
        DevUnion* pPriv;
        RADEONEntPtr pRADEONEnt;
        pPriv = xf86GetEntityPrivate(pScrn->entityList[0], 
            gRADEONEntityIndex);
        pRADEONEnt = pPriv->ptr;
        {
            ScrnInfoPtr pScrn0 = pRADEONEnt->pPrimaryScrn;
            vgaHWPtr      hwp0         = VGAHWPTR(pScrn0);
            vgaHWUnlock(hwp0);
            vgaHWRestore(pScrn0, &hwp0->SavedReg, 
                    VGA_SR_MODE | VGA_SR_FONTS );
            vgaHWLock(hwp0);
        }
    }
    RADEONUnblank(pScrn);

#if 0
    RADEONWaitForVerticalSync(pScrn);
#endif
}


/* Define common registers for requested video mode. */
static void RADEONInitCommonRegisters(RADEONSavePtr save, RADEONInfoPtr info)
{
    save->ovr_clr            = 0;
    save->ovr_wid_left_right = 0;
    save->ovr_wid_top_bottom = 0;
    save->ov0_scale_cntl     = 0;
    save->mpp_tb_config      = 0;
    save->mpp_gp_config      = 0;
    save->subpic_cntl        = 0;
    save->viph_control       = 0;
    save->i2c_cntl_1         = 0;
    save->rbbm_soft_reset    = 0;
    save->cap0_trig_cntl     = 0;
    save->cap1_trig_cntl     = 0;
    save->bus_cntl           = info->BusCntl;
    /*
     * If bursts are enabled, turn on discards
     * Radeon doesn't have write bursts
     */
    if (save->bus_cntl & (RADEON_BUS_READ_BURST))
	save->bus_cntl |= RADEON_BUS_RD_DISCARD_EN;
}

/* Define CRTC registers for requested video mode. */
static Bool RADEONInitCrtcRegisters(ScrnInfoPtr pScrn, RADEONSavePtr save,
				  DisplayModePtr mode, RADEONInfoPtr info)
{
    int    format;
    int    hsync_start;
    int    hsync_wid;
    int    hsync_fudge;
    int    vsync_wid;
    int    bytpp;
    int    hsync_fudge_default[] = { 0x00, 0x12, 0x09, 0x09, 0x06, 0x05 };
    int    hsync_fudge_fp[]      = { 0x02, 0x02, 0x00, 0x00, 0x05, 0x05 };

    switch (info->CurrentLayout.pixel_code) {
    case 4:  format = 1; bytpp = 0; break;
    case 8:  format = 2; bytpp = 1; break;
    case 15: format = 3; bytpp = 2; break;      /*  555 */
    case 16: format = 4; bytpp = 2; break;      /*  565 */
    case 24: format = 5; bytpp = 3; break;      /*  RGB */
    case 32: format = 6; bytpp = 4; break;      /* xRGB */
    default:
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "Unsupported pixel depth (%d)\n", info->CurrentLayout.bitsPerPixel);
	return FALSE;
    }
    RADEONTRACE(("Format = %d (%d bytes per pixel)\n", format, bytpp));

    if ((info->DisplayType == MT_DFP) || 
        (info->DisplayType == MT_LCD))
	hsync_fudge = hsync_fudge_fp[format-1];
    else               
    hsync_fudge = hsync_fudge_default[format-1];

    save->crtc_gen_cntl = (RADEON_CRTC_EXT_DISP_EN
			  | RADEON_CRTC_EN
			  | (format << 8)
			  | ((mode->Flags & V_DBLSCAN)
			     ? RADEON_CRTC_DBL_SCAN_EN
			     : 0)
			  | ((mode->Flags & V_INTERLACE)
			     ? RADEON_CRTC_INTERLACE_EN
			     : 0));

    if((info->DisplayType == MT_DFP) || 
       (info->DisplayType == MT_LCD))
    {
        save->crtc_ext_cntl = RADEON_VGA_ATI_LINEAR | 
        			  RADEON_XCRT_CNT_EN;
        save->crtc_gen_cntl &= ~(RADEON_CRTC_DBL_SCAN_EN | 
                                  RADEON_CRTC_INTERLACE_EN);
    }
    else
    save->crtc_ext_cntl = RADEON_VGA_ATI_LINEAR | 
			  RADEON_XCRT_CNT_EN |
			  RADEON_CRTC_CRT_ON;

    save->dac_cntl      = (RADEON_DAC_MASK_ALL
			   | RADEON_DAC_VGA_ADR_EN
			   | (info->dac6bits ? 0 : RADEON_DAC_8BIT_EN));
  
    if(((info->DisplayType == MT_DFP) || 
       (info->DisplayType == MT_LCD)) && !info->UseCRT)
    {
        if(info->PanelXRes < mode->CrtcHDisplay)
            mode->HDisplay = mode->CrtcHDisplay = info->PanelXRes;
        if(info->PanelYRes < mode->CrtcVDisplay)
            mode->VDisplay = mode->CrtcVDisplay = info->PanelYRes;
        mode->CrtcHTotal = mode->CrtcHDisplay + info->HBlank;
        mode->CrtcHSyncStart = mode->CrtcHDisplay + info->HOverPlus;
        mode->CrtcHSyncEnd = mode->CrtcHSyncStart + info->HSyncWidth;
        mode->CrtcVTotal = mode->CrtcVDisplay + info->VBlank;
        mode->CrtcVSyncStart = mode->CrtcVDisplay + info->VOverPlus;
        mode->CrtcVSyncEnd = mode->CrtcVSyncStart + info->VSyncWidth;
    }
    save->crtc_h_total_disp = ((((mode->CrtcHTotal / 8) - 1) & 0x3ff)
	   | ((((mode->CrtcHDisplay / 8) - 1) & 0x1ff) << 16));

    hsync_wid = (mode->CrtcHSyncEnd - mode->CrtcHSyncStart) / 8;
    if (!hsync_wid)       hsync_wid = 1;
    if (hsync_wid > 0x3f) hsync_wid = 0x3f;
    hsync_start = mode->CrtcHSyncStart - 8 + hsync_fudge;

    save->crtc_h_sync_strt_wid = ((hsync_start & 0x1fff)
 				 | (hsync_wid << 16)
				 | ((mode->Flags & V_NHSYNC)
				    ? RADEON_CRTC_H_SYNC_POL
				    : RADEON_CRTC_H_SYNC_POL));
  
#if 1
				/* This works for double scan mode. */
    save->crtc_v_total_disp = (((mode->CrtcVTotal - 1) & 0xffff)
			      | ((mode->CrtcVDisplay - 1) << 16));
#else
				/* This is what cce/nbmode.c example code
				   does -- is this correct? */
    save->crtc_v_total_disp = (((mode->CrtcVTotal - 1) & 0xffff)
			      | ((mode->CrtcVDisplay
				  * ((mode->Flags & V_DBLSCAN) ? 2 : 1) - 1)
				 << 16));
#endif

    vsync_wid = mode->CrtcVSyncEnd - mode->CrtcVSyncStart;
    if (!vsync_wid)       vsync_wid = 1;
    if (vsync_wid > 0x1f) vsync_wid = 0x1f;

    save->crtc_v_sync_strt_wid = (((mode->CrtcVSyncStart - 1) & 0xfff)
				 | (vsync_wid << 16)
				 | ((mode->Flags & V_NVSYNC)
				    ? RADEON_CRTC_V_SYNC_POL
				    : RADEON_CRTC_V_SYNC_POL));

    save->crtc_offset      = 0;
    save->crtc_offset_cntl = 0;

    save->crtc_pitch  = ((pScrn->displayWidth * pScrn->bitsPerPixel) +
			 ((pScrn->bitsPerPixel * 8) -1)) /
			 (pScrn->bitsPerPixel * 8);
    save->crtc_pitch |= save->crtc_pitch << 16;

    save->surface_cntl = RADEON_SURF_TRANSLATION_DIS;
#if X_BYTE_ORDER == X_BIG_ENDIAN
    switch (pScrn->bitsPerPixel) {
	case 16:
		save->surface_cntl |= RADEON_NONSURF_AP0_SWP_16BPP;
		break;
	case 32:
		save->surface_cntl |= RADEON_NONSURF_AP0_SWP_32BPP;
		break;
    }
#endif

    RADEONTRACE(("Pitch = %d bytes (virtualX = %d, displayWidth = %d)\n",
		 save->crtc_pitch, pScrn->virtualX,
		 info->CurrentLayout.displayWidth));
    return TRUE;
}

/* Define CRTC2 registers for requested video mode. */
static Bool RADEONInitCrtc2Registers(ScrnInfoPtr pScrn, RADEONSavePtr save,
				  DisplayModePtr mode, RADEONInfoPtr info)
{
    int    format;
    int    hsync_start;
    int    hsync_wid;
    int    hsync_fudge;
    int    vsync_wid;
    int    bytpp;
    int    hsync_fudge_default[] = { 0x00, 0x12, 0x09, 0x09, 0x06, 0x05 };

    switch (info->CurrentLayout.pixel_code) {
    case 4:  format = 1; bytpp = 0; break;
    case 8:  format = 2; bytpp = 1; break;
    case 15: format = 3; bytpp = 2; break;      /*  555 */
    case 16: format = 4; bytpp = 2; break;      /*  565 */
    case 24: format = 5; bytpp = 3; break;      /*  RGB */
    case 32: format = 6; bytpp = 4; break;      /* xRGB */
    default:
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "Unsupported pixel depth (%d)\n", info->CurrentLayout.bitsPerPixel);
	return FALSE;
    }
    RADEONTRACE(("Format = %d (%d bytes per pixel)\n", format, bytpp));

    hsync_fudge = hsync_fudge_default[format-1];

    save->crtc2_gen_cntl = (RADEON_CRTC2_EN
                          | RADEON_CRTC2_CRT2_ON
			  | (format << 8)
			  | ((mode->Flags & V_DBLSCAN)
			     ? RADEON_CRTC2_DBL_SCAN_EN
			     : 0)
			  | ((mode->Flags & V_INTERLACE)
			     ? RADEON_CRTC2_INTERLACE_EN
			     : 0));

    if(info->IsR200)
        save->disp_output_cntl = 
            ((info->SavedReg.disp_output_cntl & ~RADEON_DISP_DAC_SOURCE_MASK)
            | RADEON_DISP_DAC_SOURCE_CRTC2);
    else
        save->dac2_cntl = info->SavedReg.dac2_cntl 
                      /*| RADEON_DAC2_DAC2_CLK_SEL*/
                      | RADEON_DAC2_DAC_CLK_SEL;

    save->crtc2_h_total_disp = ((((mode->CrtcHTotal / 8) - 1) & 0x3ff)
	   | ((((mode->CrtcHDisplay / 8) - 1) & 0x1ff) << 16));

    hsync_wid = (mode->CrtcHSyncEnd - mode->CrtcHSyncStart) / 8;
    if (!hsync_wid)       hsync_wid = 1;
    if (hsync_wid > 0x3f) hsync_wid = 0x3f;
    hsync_start = mode->CrtcHSyncStart - 8 + hsync_fudge;

    save->crtc2_h_sync_strt_wid = ((hsync_start & 0x1fff)
				 | (hsync_wid << 16)
				 | ((mode->Flags & V_NHSYNC)
				    ? RADEON_CRTC_H_SYNC_POL
				    : RADEON_CRTC_H_SYNC_POL));

#if 1
				/* This works for double scan mode. */
    save->crtc2_v_total_disp = (((mode->CrtcVTotal - 1) & 0xffff)
			      | ((mode->CrtcVDisplay - 1) << 16));
#else
				/* This is what cce/nbmode.c example code
				   does -- is this correct? */
    save->crtc2_v_total_disp = (((mode->CrtcVTotal - 1) & 0xffff)
			      | ((mode->CrtcVDisplay
				  * ((mode->Flags & V_DBLSCAN) ? 2 : 1) - 1)
				 << 16));
#endif

    vsync_wid = mode->CrtcVSyncEnd - mode->CrtcVSyncStart;
    if (!vsync_wid)       vsync_wid = 1;
    if (vsync_wid > 0x1f) vsync_wid = 0x1f;

    save->crtc2_v_sync_strt_wid = (((mode->CrtcVSyncStart - 1) & 0xfff)
				 | (vsync_wid << 16)
				 | ((mode->Flags & V_NVSYNC)
				    ? RADEON_CRTC2_V_SYNC_POL
				    : RADEON_CRTC2_V_SYNC_POL));

    save->crtc2_offset      = 0;
    save->crtc2_offset_cntl = 0;

    save->crtc2_pitch  = ((pScrn->displayWidth * pScrn->bitsPerPixel) +
			 ((pScrn->bitsPerPixel * 8) -1)) /
			 (pScrn->bitsPerPixel * 8);
    save->crtc2_pitch |= save->crtc2_pitch << 16;
	
    RADEONTRACE(("Pitch = %d bytes (virtualX = %d, displayWidth = %d)\n",
		 save->crtc2_pitch, pScrn->virtualX,
		 info->CurrentLayout.displayWidth));
    return TRUE;
}

/* Note: Radeon flat panel support has been disabled for now */
/* Define CRTC registers for requested video mode. */
static void RADEONInitFPRegisters(ScrnInfoPtr pScrn, RADEONSavePtr orig,
				  RADEONSavePtr save, DisplayModePtr mode,
				  RADEONInfoPtr info)
{
    int   xres = mode->CrtcHDisplay;
    int   yres = mode->CrtcVDisplay;
    float Hratio, Vratio;

    if(info->PanelXRes == 0 || info->PanelYRes == 0)
    {
        Hratio = 1;
        Vratio = 1;
    }
    else
    {
    if (xres > info->PanelXRes) xres = info->PanelXRes;
    if (yres > info->PanelYRes) yres = info->PanelYRes;

    Hratio = (float)xres/(float)info->PanelXRes;
    Vratio = (float)yres/(float)info->PanelYRes;
    }

    if (Hratio == 1.0)
    {
        save->fp_horz_stretch = orig->fp_horz_stretch;
        save->fp_horz_stretch &= ~(RADEON_HORZ_STRETCH_BLEND |
	                           RADEON_HORZ_STRETCH_ENABLE);
    }
    else
    {               
    save->fp_horz_stretch =
            ((((unsigned long)(Hratio * RADEON_HORZ_STRETCH_RATIO_MAX +
            0.5)) & RADEON_HORZ_STRETCH_RATIO_MASK)) |
	 (orig->fp_horz_stretch & (RADEON_HORZ_PANEL_SIZE |
				   RADEON_HORZ_FP_LOOP_STRETCH |
                                  RADEON_HORZ_AUTO_RATIO_INC));
        save->fp_horz_stretch |=  (RADEON_HORZ_STRETCH_BLEND |
						  RADEON_HORZ_STRETCH_ENABLE);
    }
    save->fp_horz_stretch &= ~RADEON_HORZ_AUTO_RATIO;

    if (Vratio == 1.0) 
    {
        save->fp_vert_stretch = orig->fp_vert_stretch;
        save->fp_vert_stretch &= ~(RADEON_VERT_STRETCH_ENABLE|
                                   RADEON_VERT_STRETCH_BLEND);
    }   
    else
    {               
    save->fp_vert_stretch =
	    (((((unsigned long)(Vratio * RADEON_VERT_STRETCH_RATIO_MAX +
            0.5)) & RADEON_VERT_STRETCH_RATIO_MASK)) |
	 (orig->fp_vert_stretch & (RADEON_VERT_PANEL_SIZE |
				   RADEON_VERT_STRETCH_RESERVED)));
        save->fp_vert_stretch |=  (RADEON_VERT_STRETCH_ENABLE |
						  RADEON_VERT_STRETCH_BLEND);
    }
    save->fp_vert_stretch &= ~RADEON_VERT_AUTO_RATIO_EN;

    save->fp_gen_cntl = (orig->fp_gen_cntl & (CARD32)
					~(RADEON_FP_SEL_CRTC2 |
			                  RADEON_FP_RMX_HVSYNC_CONTROL_EN |
					  RADEON_FP_DFP_SYNC_SEL |	
                                          RADEON_FP_CRT_SYNC_SEL | 
					  RADEON_FP_CRTC_LOCK_8DOT |	
					  RADEON_FP_USE_SHADOW_EN |
					  RADEON_FP_CRTC_USE_SHADOW_VEND |
					  RADEON_FP_CRT_SYNC_ALT));
	save->fp_gen_cntl |= (RADEON_FP_CRTC_DONT_SHADOW_VPAR |
                          RADEON_FP_CRTC_DONT_SHADOW_HEND );

    save->lvds_gen_cntl        = orig->lvds_gen_cntl;
    save->lvds_pll_cntl        = orig->lvds_pll_cntl;
    save->tmds_crc             = orig->tmds_crc;

    /* Disable CRT output by disabling CRT output for DFP*/
    save->crtc_ext_cntl  &= ~RADEON_CRTC_CRT_ON;

    if(info->DisplayType == MT_LCD)
    {
    save->lvds_gen_cntl  |= (RADEON_LVDS_ON | RADEON_LVDS_BLON);
        save->fp_gen_cntl    &= ~(RADEON_FP_FPON | RADEON_FP_TMDS_EN);
    }
    else if(info->DisplayType == MT_DFP)
        save->fp_gen_cntl    |= (RADEON_FP_FPON | RADEON_FP_TMDS_EN);

    save->fp_crtc_h_total_disp = orig->fp_crtc_h_total_disp;
    save->fp_crtc_v_total_disp = orig->fp_crtc_v_total_disp;
    save->fp_h_sync_strt_wid   = orig->fp_h_sync_strt_wid;
    save->fp_v_sync_strt_wid   = orig->fp_v_sync_strt_wid;

}

/* Define PLL registers for requested video mode. */
static void RADEONInitPLLRegisters(RADEONSavePtr save, RADEONPLLPtr pll,
				   double dot_clock)
{
    unsigned long freq = dot_clock * 100;
    struct {
	int divider;
	int bitvalue;
    } *post_div,
      post_divs[]   = {
				/* From RAGE 128 VR/RAGE 128 GL Register
				   Reference Manual (Technical Reference
				   Manual P/N RRG-G04100-C Rev. 0.04), page
				   3-17 (PLL_DIV_[3:0]).  */
	{  1, 0 },              /* VCLK_SRC                 */
	{  2, 1 },              /* VCLK_SRC/2               */
	{  4, 2 },              /* VCLK_SRC/4               */
	{  8, 3 },              /* VCLK_SRC/8               */
	{  3, 4 },              /* VCLK_SRC/3               */
	{ 16, 5 },              /* VCLK_SRC/16              */
	{  6, 6 },              /* VCLK_SRC/6               */
	{ 12, 7 },              /* VCLK_SRC/12              */
	{  0, 0 }
    };

    if (freq > pll->max_pll_freq)      freq = pll->max_pll_freq;
    if (freq * 12 < pll->min_pll_freq) freq = pll->min_pll_freq / 12;

    for (post_div = &post_divs[0]; post_div->divider; ++post_div) {
	save->pll_output_freq = post_div->divider * freq;
	if (save->pll_output_freq >= pll->min_pll_freq
	    && save->pll_output_freq <= pll->max_pll_freq) break;
    }

    save->dot_clock_freq = freq;
    save->feedback_div   = RADEONDiv(pll->reference_div
				     * save->pll_output_freq,
				     pll->reference_freq);
    save->post_div       = post_div->divider;

    RADEONTRACE(("dc=%d, of=%d, fd=%d, pd=%d\n",
	       save->dot_clock_freq,
	       save->pll_output_freq,
	       save->feedback_div,
	       save->post_div));

    save->ppll_ref_div   = pll->reference_div;
    save->ppll_div_3     = (save->feedback_div | (post_div->bitvalue << 16));
    save->htotal_cntl    = 0;
}

/* Define PLL2 registers for requested video mode. */
static void RADEONInitPLL2Registers(RADEONSavePtr save, RADEONPLLPtr pll,
				   double dot_clock)
{
    unsigned long freq = dot_clock * 100;
    struct {
	int divider;
	int bitvalue;
    } *post_div,
      post_divs[]   = {
				/* From RAGE 128 VR/RAGE 128 GL Register
				   Reference Manual (Technical Reference
				   Manual P/N RRG-G04100-C Rev. 0.04), page
				   3-17 (PLL_DIV_[3:0]).  */
	{  1, 0 },              /* VCLK_SRC                 */
	{  2, 1 },              /* VCLK_SRC/2               */
	{  4, 2 },              /* VCLK_SRC/4               */
	{  8, 3 },              /* VCLK_SRC/8               */
	{  3, 4 },              /* VCLK_SRC/3               */
	{ 16, 5 },              /* VCLK_SRC/16              */
	{  6, 6 },              /* VCLK_SRC/6               */
	{ 12, 7 },              /* VCLK_SRC/12              */
	{  0, 0 }
    };

    if (freq > pll->max_pll_freq)      freq = pll->max_pll_freq;
    if (freq * 12 < pll->min_pll_freq) freq = pll->min_pll_freq / 12;

    for (post_div = &post_divs[0]; post_div->divider; ++post_div) {
	save->pll_output_freq_2 = post_div->divider * freq;
	if (save->pll_output_freq_2 >= pll->min_pll_freq
	    && save->pll_output_freq_2 <= pll->max_pll_freq) break;
    }

    save->dot_clock_freq_2 = freq;
    save->feedback_div_2   = RADEONDiv(pll->reference_div
				     * save->pll_output_freq_2,
				     pll->reference_freq);
    save->post_div_2       = post_div->divider;

    RADEONTRACE(("dc=%d, of=%d, fd=%d, pd=%d\n",
	       save->dot_clock_freq_2,
	       save->pll_output_freq_2,
	       save->feedback_div_2,
	       save->post_div_2));

    save->p2pll_ref_div   = pll->reference_div;
    save->p2pll_div_0    = (save->feedback_div_2 | (post_div->bitvalue<<16));
    save->htotal_cntl2    = 0;
}

/* Define DDA registers for requested video mode. */
static Bool RADEONInitDDARegisters(ScrnInfoPtr pScrn, RADEONSavePtr save,
				   RADEONPLLPtr pll, RADEONInfoPtr info)
{
    int         DisplayFifoWidth = 128;
    int         DisplayFifoDepth = 32;
    int         XclkFreq;
    int         VclkFreq;
    int         XclksPerTransfer;
    int         XclksPerTransferPrecise;
    int         UseablePrecision;
    int         Roff;
    int         Ron;

    XclkFreq = pll->xclk;

    VclkFreq = RADEONDiv(pll->reference_freq * save->feedback_div,
			 pll->reference_div * save->post_div);

    XclksPerTransfer = RADEONDiv(XclkFreq * DisplayFifoWidth,
				 VclkFreq *
				 (info->CurrentLayout.pixel_bytes * 8));

    UseablePrecision = RADEONMinBits(XclksPerTransfer) + 1;

    XclksPerTransferPrecise = RADEONDiv((XclkFreq * DisplayFifoWidth)
					<< (11 - UseablePrecision),
					VclkFreq *
					(info->CurrentLayout.pixel_bytes * 8));

    Roff  = XclksPerTransferPrecise * (DisplayFifoDepth - 4);

    Ron   = (4 * info->ram->MB
	     + 3 * MAX(info->ram->Trcd - 2, 0)
	     + 2 * info->ram->Trp
	     + info->ram->Twr
	     + info->ram->CL
	     + info->ram->Tr2w
	     + XclksPerTransfer) << (11 - UseablePrecision);

    if (Ron + info->ram->Rloop >= Roff) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "(Ron = %d) + (Rloop = %d) >= (Roff = %d)\n",
		   Ron, info->ram->Rloop, Roff);
	return FALSE;
    }

    save->dda_config = (XclksPerTransferPrecise
			| (UseablePrecision << 16)
			| (info->ram->Rloop << 20));

    save->dda_on_off = (Ron << 16) | Roff;

    RADEONTRACE(("XclkFreq = %d; VclkFreq = %d; per = %d, %d (useable = %d)\n",
		 XclkFreq,
		 VclkFreq,
		 XclksPerTransfer,
		 XclksPerTransferPrecise,
		 UseablePrecision));
    RADEONTRACE(("Roff = %d, Ron = %d, Rloop = %d\n",
		 Roff, Ron, info->ram->Rloop));

#ifdef XvExtension
    RADEONEnterVT_Video(pScrn);
#endif

    return TRUE;
}


/* Define initial palette for requested video mode.  This doesn't do
   anything for XFree86 4.0. */
/*
static void RADEONInitPalette(RADEONSavePtr save)
{
    save->palette_valid = FALSE;
}
*/

/* Define registers for a requested video mode. */
static Bool RADEONInit(ScrnInfoPtr pScrn, DisplayModePtr mode,
		       RADEONSavePtr save)
{
    RADEONInfoPtr info      = RADEONPTR(pScrn);
    double        dot_clock = mode->Clock/1000.0;

#if RADEON_DEBUG
    ErrorF("%-12.12s %7.2f  %4d %4d %4d %4d  %4d %4d %4d %4d (%d,%d)",
	   mode->name,
	   dot_clock,

	   mode->HDisplay,
	   mode->HSyncStart,
	   mode->HSyncEnd,
	   mode->HTotal,

	   mode->VDisplay,
	   mode->VSyncStart,
	   mode->VSyncEnd,
	   mode->VTotal,
	   pScrn->depth,
	   pScrn->bitsPerPixel);
    if (mode->Flags & V_DBLSCAN)   ErrorF(" D");
    if (mode->Flags & V_INTERLACE) ErrorF(" I");
    if (mode->Flags & V_PHSYNC)    ErrorF(" +H");
    if (mode->Flags & V_NHSYNC)    ErrorF(" -H");
    if (mode->Flags & V_PVSYNC)    ErrorF(" +V");
    if (mode->Flags & V_NVSYNC)    ErrorF(" -V");
    ErrorF("\n");
    ErrorF("%-12.12s %7.2f  %4d %4d %4d %4d  %4d %4d %4d %4d (%d,%d)",
	   mode->name,
	   dot_clock,

	   mode->CrtcHDisplay,
	   mode->CrtcHSyncStart,
	   mode->CrtcHSyncEnd,
	   mode->CrtcHTotal,

	   mode->CrtcVDisplay,
	   mode->CrtcVSyncStart,
	   mode->CrtcVSyncEnd,
	   mode->CrtcVTotal,
	   pScrn->depth,
	   pScrn->bitsPerPixel);
    if (mode->Flags & V_DBLSCAN)   ErrorF(" D");
    if (mode->Flags & V_INTERLACE) ErrorF(" I");
    if (mode->Flags & V_PHSYNC)    ErrorF(" +H");
    if (mode->Flags & V_NHSYNC)    ErrorF(" -H");
    if (mode->Flags & V_PVSYNC)    ErrorF(" +V");
    if (mode->Flags & V_NVSYNC)    ErrorF(" -V");
    ErrorF("\n");
#endif

    info->Flags = mode->Flags;

    if(info->IsSecondary)
    {
        if (!RADEONInitCrtc2Registers(pScrn, save, 
             pScrn->currentMode,info)) 
            return FALSE;
        RADEONInitPLL2Registers(save, &info->pll, dot_clock);
    }
    else
    {
    RADEONInitCommonRegisters(save, info);
        if(!RADEONInitCrtcRegisters(pScrn, save, mode, info)) 
            return FALSE;
        if(dot_clock) 
        {
    RADEONInitPLLRegisters(save, &info->pll, dot_clock);
    if (!RADEONInitDDARegisters(pScrn, save, &info->pll, info))
	return FALSE;
        }
        else
        {
            save->ppll_ref_div         = info->SavedReg.ppll_ref_div;
            save->ppll_div_3           = info->SavedReg.ppll_div_3;
            save->htotal_cntl          = info->SavedReg.htotal_cntl;
            save->dda_config           = info->SavedReg.dda_config;
            save->dda_on_off           = info->SavedReg.dda_on_off;

        }
        /* not used for now */
        /*if (!info->PaletteSavedOnVT) RADEONInitPalette(save);*/
    }


    if (((info->DisplayType == MT_DFP) || 
        (info->DisplayType == MT_LCD)))
    {
        RADEONInitFPRegisters(pScrn, &info->SavedReg, save, mode, info);
    }

    RADEONTRACE(("RADEONInit returns %p\n", save));
    return TRUE;
}

/* Initialize a new mode. */
static Bool RADEONModeInit(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
    RADEONInfoPtr info      = RADEONPTR(pScrn);

    if (!RADEONInit(pScrn, mode, &info->ModeReg)) return FALSE;
				/* FIXME?  DRILock/DRIUnlock here? */

    pScrn->vtSema = TRUE;
    RADEONBlank(pScrn);
    RADEONRestoreMode(pScrn, &info->ModeReg);
    RADEONUnblank(pScrn);

    info->CurrentLayout.mode = mode;
    return TRUE;
}

static Bool RADEONSaveScreen(ScreenPtr pScreen, int mode)
{
    ScrnInfoPtr   pScrn = xf86Screens[pScreen->myNum];
    Bool unblank;

    unblank = xf86IsUnblank(mode);
    if (unblank)
	SetTimeSinceLastInputEvent();

    if ((pScrn != NULL) && pScrn->vtSema) {
	if (unblank)
	    RADEONUnblank(pScrn);
	else
	    RADEONBlank(pScrn);
    }
    return TRUE;
}

Bool RADEONSwitchMode(int scrnIndex, DisplayModePtr mode, int flags)
{
    ScrnInfoPtr   pScrn       = xf86Screens[scrnIndex];
    RADEONInfoPtr info        = RADEONPTR(pScrn);

    /* when switch mode in dual-head setup, this function will be called
       separately for each screen (depending on which screen the cursor is
       in when user press ctrl-alt-+). Since this function is always
       called when screen already in extensive mode, the sequence of
       setting CRTC2 and CRT_EXT regesters doesn't matter any more, 
       So we set the flag for RADEONRestoreMode here. */
    info->SwitchingMode = TRUE;
    return RADEONModeInit(xf86Screens[scrnIndex], mode);
    info->SwitchingMode = FALSE;
}

/* Used to disallow modes that are not supported by the hardware. */
int RADEONValidMode(int scrnIndex, DisplayModePtr mode,
		    Bool verbose, int flag)
{
    
    /* Searching for native mode timing table embedded in BIOS image.
	   Not working yet. Currently we calculate from FP registers*/
    /******
    ScrnInfoPtr   pScrn = xf86Screens[scrnIndex];
    RADEONInfoPtr info  = RADEONPTR(pScrn);
    if (info->DisplayType == MT_LCD && info->VBIOS) 
    {
	int i;
	for (i = info->FPBIOSstart+0x40; RADEON_BIOS16(i) != 0; i += 2) {
	    int j = RADEON_BIOS16(i);

	    if (mode->CrtcHDisplay == RADEON_BIOS16(j) &&
		mode->CrtcVDisplay == RADEON_BIOS16(j+2)) {
		if (RADEON_BIOS16(j+5)) j  = RADEON_BIOS16(j+5);
		else                    j += 9;

		mode->Clock = (CARD32)RADEON_BIOS16(j) * 10;

		mode->HDisplay   = mode->CrtcHDisplay   =
		    ((RADEON_BIOS16(j+10) & 0x01ff)+1)*8;
		mode->HSyncStart = mode->CrtcHSyncStart =
		    ((RADEON_BIOS16(j+12) & 0x01ff)+1)*8;
		mode->HSyncEnd   = mode->CrtcHSyncEnd   =
		    mode->CrtcHSyncStart + (RADEON_BIOS8(j+14) & 0x1f);
		mode->HTotal     = mode->CrtcHTotal     =
		    ((RADEON_BIOS16(j+8)  & 0x01ff)+1)*8;

		mode->VDisplay   = mode->CrtcVDisplay   =
		    (RADEON_BIOS16(j+17) & 0x07ff)+1;
		mode->VSyncStart = mode->CrtcVSyncStart =
		    (RADEON_BIOS16(j+19) & 0x07ff)+1;
		mode->VSyncEnd   = mode->CrtcVSyncEnd   =
		    mode->CrtcVSyncStart + ((RADEON_BIOS16(j+19) >> 11)&0x1f);
		mode->VTotal     = mode->CrtcVTotal     =
		    (RADEON_BIOS16(j+15) & 0x07ff)+1;
		return MODE_OK;
	    }
	}
	return MODE_NOMODE;
    }*****/

    return MODE_OK;
}

/* Adjust viewport into virtual desktop such that (0,0) in viewport space
   is (x,y) in virtual space. */
void RADEONAdjustFrame(int scrnIndex, int x, int y, int flags)
{
    ScrnInfoPtr   pScrn       = xf86Screens[scrnIndex];
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    int           Base;

    Base = y * info->CurrentLayout.displayWidth + x;

    switch (info->CurrentLayout.pixel_code) {
    case 15:
    case 16: Base *= 2; break;
    case 24: Base *= 3; break;
    case 32: Base *= 4; break;
    }

    Base &= ~7;                 /* 3 lower bits are always 0 */

    if(info->IsSecondary)    
    {
        Base += pScrn->fbOffset; 
        OUTREG(RADEON_CRTC2_OFFSET, Base);
    }
    else
    OUTREG(RADEON_CRTC_OFFSET, Base);
      
}

/* Called when VT switching back to the X server.  Reinitialize the video
   mode. */
Bool RADEONEnterVT(int scrnIndex, int flags)
{
    ScrnInfoPtr   pScrn = xf86Screens[scrnIndex];
    RADEONInfoPtr info  = RADEONPTR(pScrn);

    RADEONTRACE(("RADEONEnterVT\n"));
#ifdef XF86DRI
    if (RADEONPTR(pScrn)->directRenderingEnabled) {
	RADEONCP_START(pScrn, info);
	DRIUnlock(pScrn->pScreen);
    }
#endif
    if (!RADEONModeInit(pScrn, pScrn->currentMode)) return FALSE;

    if (info->accelOn)
	RADEONEngineRestore(pScrn);

    /*info->PaletteSavedOnVT = FALSE;*/
    RADEONAdjustFrame(scrnIndex, pScrn->frameX0, pScrn->frameY0, 0);
    return TRUE;
}

/* Called when VT switching away from the X server.  Restore the original
   text mode. */
void RADEONLeaveVT(int scrnIndex, int flags)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    RADEONInfoPtr info  = RADEONPTR(pScrn);
    /*RADEONSavePtr save  = &info->ModeReg;*/

    RADEONTRACE(("RADEONLeaveVT\n"));
#ifdef XvExtension
    RADEONLeaveVT_Video(pScrn);
#endif

#ifdef XF86DRI
    if (RADEONPTR(pScrn)->directRenderingEnabled) {
	DRILock(pScrn->pScreen, 0);
	RADEONCP_STOP(pScrn, info);
    }
#endif
    /* not used at present */
    /*
    RADEONSavePalette(pScrn, save);
    info->PaletteSavedOnVT = TRUE;*/

    RADEONRestore(pScrn);
}

static Bool
RADEONEnterVTFBDev(int scrnIndex, int flags)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    RADEONInfoPtr info = RADEONPTR(pScrn);
    RADEONSavePtr restore = &info->SavedReg;
    fbdevHWEnterVT(scrnIndex,flags);
    RADEONRestorePalette(pScrn,restore);
    if (info->accelOn)
	RADEONEngineRestore(pScrn);
    return TRUE;
}

static void RADEONLeaveVTFBDev(int scrnIndex, int flags)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    RADEONInfoPtr info = RADEONPTR(pScrn);
    RADEONSavePtr save = &info->SavedReg;
    RADEONSavePalette(pScrn,save);
    fbdevHWLeaveVT(scrnIndex,flags);
}

/* Called at the end of each server generation.  Restore the original text
   mode, unmap video memory, and unwrap and call the saved CloseScreen
   function.  */
static Bool RADEONCloseScreen(int scrnIndex, ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    RADEONInfoPtr info  = RADEONPTR(pScrn);

    RADEONTRACE(("RADEONCloseScreen\n"));

#ifdef XF86DRI
				/* Disable direct rendering */
    if (info->directRenderingEnabled) {
	RADEONDRICloseScreen(pScreen);
	info->directRenderingEnabled = FALSE;
    }
#endif

    if (pScrn->vtSema) 
    {	
	RADEONRestore(pScrn);
	RADEONUnmapMem(pScrn);
    }

    if (info->accel)             XAADestroyInfoRec(info->accel);
    info->accel                  = NULL;

    if (info->scratch_save)      xfree(info->scratch_save);
    info->scratch_save           = NULL;

    if (info->cursor)            xf86DestroyCursorInfoRec(info->cursor);
    info->cursor                 = NULL;

    if (info->DGAModes)          xfree(info->DGAModes);
    info->DGAModes               = NULL;

#if 0
    if (info->adaptor) {
        RADEONShutdownVideo(pScrn, info->adaptor->pPortPrivates[0].ptr);
        xfree(info->adaptor->pPortPrivates[0].ptr);
	xf86XVFreeVideoAdaptorRec(info->adaptor);
	info->adaptor = NULL;
    }
#endif

    pScrn->vtSema = FALSE;

    xf86ClearPrimInitDone(pScrn->entityList[0]);

    pScreen->BlockHandler = info->BlockHandler;
    pScreen->CloseScreen = info->CloseScreen;
    return (*pScreen->CloseScreen)(scrnIndex, pScreen);
}

void RADEONFreeScreen(int scrnIndex, int flags)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];

    RADEONTRACE(("RADEONFreeScreen\n"));

    if (xf86LoaderCheckSymbol("vgaHWFreeHWRec"))
	vgaHWFreeHWRec(pScrn);
    RADEONFreeRec(pScrn);
}

/* Sets VESA Display Power Management Signaling (DPMS) Mode.  */
static void RADEONDisplayPowerManagementSet(ScrnInfoPtr pScrn,
					  int PowerManagementMode, int flags)
{
    RADEONInfoPtr info        = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    int           mask        = (RADEON_CRTC_DISPLAY_DIS
				 | RADEON_CRTC_HSYNC_DIS
				 | RADEON_CRTC_VSYNC_DIS);

    switch (PowerManagementMode) {
    case DPMSModeOn:
	/* Screen: On; HSync: On, VSync: On */
	OUTREGP(RADEON_CRTC_EXT_CNTL, 0, ~mask);
	break;
    case DPMSModeStandby:
	/* Screen: Off; HSync: Off, VSync: On */
	OUTREGP(RADEON_CRTC_EXT_CNTL,
		RADEON_CRTC_DISPLAY_DIS | RADEON_CRTC_HSYNC_DIS, ~mask);
	break;
    case DPMSModeSuspend:
	/* Screen: Off; HSync: On, VSync: Off */
	OUTREGP(RADEON_CRTC_EXT_CNTL,
		RADEON_CRTC_DISPLAY_DIS | RADEON_CRTC_VSYNC_DIS, ~mask);
	break;
    case DPMSModeOff:
	/* Screen: Off; HSync: Off, VSync: Off */
	OUTREGP(RADEON_CRTC_EXT_CNTL, mask, ~mask);
	break;
    }
}


