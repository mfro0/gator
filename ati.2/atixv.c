/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/ati/atixv.c,v 1.1 2001/05/09 03:12:04 tsi Exp $ */
/*
 * Copyright 2001 by Marc Aurele La France (TSI @ UQV), tsi@xfree86.org
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of Marc Aurele La France not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  Marc Aurele La France makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as-is" without express or implied warranty.
 *
 * MARC AURELE LA FRANCE DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.  IN NO
 * EVENT SHALL MARC AURELE LA FRANCE BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include "ati.h"
#include "atistruct.h"
#include "atiregs.h"
#include "atichip.h"
#include "atimach64io.h"
#include "atixv.h"

#include "xf86.h"
#include "dixstruct.h"
#include "xf86xv.h"
#include "xf86i2c.h"
#include "fi1236.h"
#include "msp3430.h"
#include "bt829.h"
#include "tda9850.h"
#include "tda8425.h"
#include "i2c_def.h"

#include "Xv.h"
#include "fourcc.h"

#define OFF_DELAY       250  /* milliseconds */
#define FREE_DELAY      15000

#define OFF_TIMER       0x01
#define FREE_TIMER      0x02
#define CLIENT_VIDEO_ON 0x04

#define TIMER_MASK      (OFF_TIMER | FREE_TIMER)


#ifndef XvExtension
Bool ATIInitializeXVideo(ScreenPtr pScreen, ScrnInfoPtr pScreenInfo, ATIPtr pATI) {}
#else

static XF86VideoAdaptorPtr ATISetupImageVideo(ScreenPtr);

/*
 * ATIInitializeXVideo --
 *
 * This function is called to initialise XVideo extension support on a screen.
 */
Bool
ATIInitializeXVideo
(
    ScreenPtr   pScreen,
    ScrnInfoPtr pScreenInfo,
    ATIPtr      pATI
)
{
    XF86VideoAdaptorPtr *ppAdaptors, *ppNewAdaptors=NULL;
    XF86VideoAdaptorPtr newAdaptor = NULL;
    int                 nAdaptors;
    Bool		result;

    if (!(pScreenInfo->memPhysBase = pATI->LinearBase))
        return FALSE;

    if(pATI->pXAAInfo && pATI->pXAAInfo->FillSolidRects){
	newAdaptor = ATISetupImageVideo(pScreen);
        }

    pScreenInfo->fbOffset = 0;

    nAdaptors = xf86XVListGenericAdaptors(pScreenInfo, &ppAdaptors);

    if(newAdaptor) {
	if(!nAdaptors) {
	    nAdaptors = 1;
	    ppAdaptors = &newAdaptor;
	} else {
	    ppNewAdaptors =  /* need to free this someplace */
		xalloc((nAdaptors + 1) * sizeof(XF86VideoAdaptorPtr*));
	    if(ppNewAdaptors) {
		memcpy(ppNewAdaptors, ppAdaptors, nAdaptors *
					sizeof(XF86VideoAdaptorPtr));
		ppNewAdaptors[nAdaptors] = newAdaptor;
		ppAdaptors = ppNewAdaptors;
		nAdaptors++;
	    }
	}
    }

    if(nAdaptors)
	    result=xf86XVScreenInit(pScreen, ppAdaptors, nAdaptors);
	    else result=FALSE;
    if(ppNewAdaptors)
	xfree(ppNewAdaptors);

    return result;
}

static int  ATISetPortAttribute(ScrnInfoPtr, Atom, INT32, pointer);
static int  ATIGetPortAttribute(ScrnInfoPtr, Atom ,INT32 *, pointer);
static void ATIStopVideo(ScrnInfoPtr, pointer, Bool);
static void ATIQueryBestSize(ScrnInfoPtr, Bool, short, short, short, short,
			unsigned int *, unsigned int *, pointer);
static int  ATIPutImage(ScrnInfoPtr, short, short, short, short, short,
			short, short, short, int, unsigned char*, short,
			short, Bool, RegionPtr, pointer);
static int ATIPutVideo(ScrnInfoPtr pScrn, short src_x, short src_y, short drw_x, short drw_y,
                        short src_w, short src_h, short drw_w, short drw_h, 
			RegionPtr clipBoxes, pointer data);
static int  ATIQueryImageAttributes(ScrnInfoPtr, int, unsigned short *,
			unsigned short *,  int *, int *);


static void ATIResetVideo(ScrnInfoPtr);

static void ATIVideoTimerCallback(ScrnInfoPtr pScrn, Time time);


#define MAKE_ATOM(a) MakeAtom(a, sizeof(a) - 1, TRUE)

static Atom xvBrightness, xvColorKey, xvSaturation, xvColor, xvHue, xvContrast,
        xvDoubleBuffer, xvEncoding, xvVolume, xvMute, xvFrequency, xv_autopaint_colorkey,
	xv_set_defaults;

typedef struct
{
    BYTE table_revision;
    BYTE table_size;
    BYTE tuner_type;
    BYTE audio_chip;
    BYTE product_id;
    BYTE tuner_voltage_teletext_fm;
    BYTE i2c_config;
    BYTE video_decoder_type;
    BYTE video_decoder_host_config;
    BYTE input[5];
} _MM_TABLE;


typedef struct {
   int           brightness;
   int           saturation;
   int 		 hue;
   int           contrast;
   Bool          doubleBuffer;
   unsigned char currentBuffer;
   FBLinearPtr   linear;
   RegionRec     clip;
   CARD32        colorKey;
   CARD32        videoStatus;
   Time          offTime;
   Time          freeTime;
   
   I2CBusPtr	 i2c;
   CARD32 	 r128_i2c_timing;
   CARD32        ati_M;
   CARD32        ati_N;
   
   FI1236Ptr     fi1236;
   MSP3430Ptr    msp3430;
   BT829Ptr	 bt829;
   TDA9850Ptr	 tda9850;
   TDA8425Ptr	 tda8425;
   
   Bool          video_stream_active;
   int           encoding;
   CARD32        frequency;
   int           volume;
   Bool		 mute;
   int           v;
   int           ecp_div;
   
   Bool          MM_TABLE_valid;
   _MM_TABLE     MM_TABLE;
   
   Bool		 EEPROM_present;
   int		 EEPROM_addr;
   
   Bool          addon_board;
   CARD8         board_info;
   int           board_control;
   
   Bool          autopaint_colorkey;
   /* mach64 AIW boards have their own, peculiar I2C interface */
   /* In particular, when you reset direction bits the line is dropped 
      so we can't just override I2CGetBits and I2CPutBits (since generic
      routines need to read SCL line in odd places) */
   CARD32	 i2c_status;
   void		 (*setbits)(I2CBusPtr b, CARD32 bits);
   CARD32	 (*getbits)(I2CBusPtr b);
   CARD32	 scl_dir;
   CARD32	 sda_dir;
   CARD32	 scl_set;
   CARD32	 sda_set;
   CARD32	 scl_get;
   CARD32	 sda_get;
   
} ATIPortPrivRec, *ATIPortPrivPtr;

#define I2CSLEEP { \
	b->I2CUDelay(b, b->RiseFallTime); \
	}

#define SCLDIR(a)    { \
	if(a)pPriv->setbits(b, pPriv->i2c_status | pPriv->scl_dir); \
	  else pPriv->setbits(b, pPriv->i2c_status & ~pPriv->scl_dir); \
	  }

#define SDADIR(a)    { \
	if(a)pPriv->setbits(b, pPriv->i2c_status | pPriv->sda_dir); \
	  else pPriv->setbits(b, pPriv->i2c_status & ~pPriv->sda_dir); \
	  }
	  
#define SCLSET(a)    { \
	if(a)pPriv->setbits(b, pPriv->i2c_status | pPriv->scl_set); \
	  else pPriv->setbits(b, pPriv->i2c_status & ~pPriv->scl_set); \
	  I2CSLEEP; \
	  }
	  
#define SDASET(a)    { \
	if(a)pPriv->setbits(b, pPriv->i2c_status | pPriv->sda_set); \
	  else pPriv->setbits(b, pPriv->i2c_status & ~pPriv->sda_set); \
	  I2CSLEEP; \
	  }

#define ISSDA    (pPriv->getbits(b) & pPriv->sda_get)

void ATIReadMM_TABLE(ScrnInfoPtr pScrn, ATIPortPrivPtr pPriv);
void ATI_detect_addon(ATIPortPrivPtr pPriv);
void ATI_read_eeprom(ATIPortPrivPtr pPriv);
void ATI_board_setmisc(ATIPortPrivPtr pPriv);
void ATI_MSP_SetEncoding(ATIPortPrivPtr pPriv);
void ATI_BT_SetEncoding(ScrnInfoPtr pScrn, ATIPortPrivPtr pPriv);
void ATILeaveVT_Video(ScrnInfoPtr pScrn);
void ATIEnterVT_Video(ScrnInfoPtr pScrn);
static void ATIMuteAudio(ATIPortPrivPtr pPriv, Bool mute);

/* client libraries expect an encoding */
static XF86VideoEncodingRec DummyEncoding =
{
   0,
   "XV_IMAGE",
   2048, 2048,
   {1, 1}
};

 /* the picture is interlaced - hence the half-heights */
/* mach64 video cards have overlay scaler that is limited to 720 pixels
   horizontal resolution */

static XF86VideoEncodingRec
VT_GT_InputVideoEncodings[] =
{
    { 0, "XV_IMAGE",			720,2048,{1,1}},        
    { 1, "pal-composite",		720, 288, { 1, 50 }},
    { 2, "pal-tuner",			720, 288, { 1, 50 }},
    { 3, "pal-svideo",			720, 288, { 1, 50 }},
    { 4, "ntsc-composite",		640, 240, { 1001, 60000 }},
    { 5, "ntsc-tuner",			640, 240, { 1001, 60000 }},
    { 6, "ntsc-svideo",			640, 240, { 1001, 60000 }},
    { 7, "secam-composite",		720, 288, { 1, 50 }},
    { 8, "secam-tuner",			720, 288, { 1, 50 }},
    { 9, "secam-svideo",		720, 288, { 1, 50 }},
    { 10,"pal_60-composite",		720, 288, { 1, 50 }},
    { 11,"pal_60-tuner",		720, 288, { 1, 50 }},
    { 12,"pal_60-svideo",		720, 288, { 1, 50 }}
};

static XF86VideoEncodingRec
RagePro_InputVideoEncodings[] =
{
    { 0, "XV_IMAGE",			768,2048,{1,1}},        
    { 1, "pal-composite",		768, 288, { 1, 50 }},
    { 2, "pal-tuner",			768, 288, { 1, 50 }},
    { 3, "pal-svideo",			768, 288, { 1, 50 }},
    { 4, "ntsc-composite",		640, 240, { 1001, 60000 }},
    { 5, "ntsc-tuner",			640, 240, { 1001, 60000 }},
    { 6, "ntsc-svideo",			640, 240, { 1001, 60000 }},
    { 7, "secam-composite",		768, 288, { 1, 50 }},
    { 8, "secam-tuner",			768, 288, { 1, 50 }},
    { 9, "secam-svideo",		768, 288, { 1, 50 }},
    { 10,"pal_60-composite",		768, 288, { 1, 50 }},
    { 11,"pal_60-tuner",		768, 288, { 1, 50 }},
    { 12,"pal_60-svideo",		768, 288, { 1, 50 }}
};


#define NUM_FORMATS 12

static XF86VideoFormatRec Formats[NUM_FORMATS] =
{
   {8, TrueColor}, {8, DirectColor}, {8, PseudoColor},
   {8, GrayScale}, {8, StaticGray}, {8, StaticColor},
   {15, TrueColor}, {16, TrueColor}, {24, TrueColor},
   {15, DirectColor}, {16, DirectColor}, {24, DirectColor}
};


#define NUM_ATTRIBUTES 13

/* +1 for the NULL string.. it is not actually used */
static XF86AttributeRec Attributes[NUM_ATTRIBUTES+1] =
{
   {XvSettable             , 0, 1, "XV_SET_DEFAULTS"},
   {XvSettable | XvGettable, 0, 1, "XV_AUTOPAINT_COLORKEY"},
   {XvSettable | XvGettable, 0, ~0, "XV_COLORKEY"},
   {XvSettable | XvGettable, 0, 1, "XV_DOUBLE_BUFFER"},
   {XvSettable | XvGettable, 0, 12, "XV_ENCODING"},
   {XvSettable | XvGettable, 0, -1, "XV_FREQ"},
   {XvSettable | XvGettable, 0, 1, "XV_MUTE"},
   {XvSettable | XvGettable, 0x01, 0x7F, "XV_VOLUME"},
   {XvSettable | XvGettable, -1000, 1000, "XV_BRIGHTNESS"},
   {XvSettable | XvGettable, -1000, 1000, "XV_CONTRAST"},
   {XvSettable | XvGettable, -1000, 1000, "XV_SATURATION"},
   {XvSettable | XvGettable, -1000, 1000, "XV_COLOR"},
   {XvSettable | XvGettable, -1000, 1000, "XV_HUE"},
   { 0, 0, 0, NULL}  /* just a place holder so I don't have to be fancy with commas */
};

static XF86AttributeRec AIWClassicAttributes[NUM_ATTRIBUTES] =
{
   {XvSettable             , 0, 1, "XV_SET_DEFAULTS"},
   {XvSettable | XvGettable, 0, 1, "XV_AUTOPAINT_COLORKEY"},
   {XvSettable | XvGettable, 0, ~0, "XV_COLORKEY"},
   {XvSettable | XvGettable, 0, 1, "XV_DOUBLE_BUFFER"},
   {XvSettable | XvGettable, 0, 12, "XV_ENCODING"},
   {XvSettable | XvGettable, 0, -1, "XV_FREQ"},
   {XvSettable | XvGettable, 0, 1, "XV_MUTE"},
   {XvSettable | XvGettable, -1000, 1000, "XV_HUE"},
   {XvSettable | XvGettable, -1000, 1000, "XV_BRIGHTNESS"},
   {XvSettable | XvGettable, -1000, 1000, "XV_SATURATION"},
   {XvSettable | XvGettable, -1000, 1000, "XV_COLOR"},
   {XvSettable | XvGettable, -1000, 1000, "XV_CONTRAST"},
   { 0, 0, 0, NULL}  /* just a place holder so I don't have to be fancy with commas */
};

#define NUM_IMAGES 4

static XF86ImageRec Images[NUM_IMAGES] =
{
	XVIMAGE_YUY2,
	XVIMAGE_UYVY,
	XVIMAGE_YV12,
	XVIMAGE_I420
};

static void ATIMuteAudio(ATIPortPrivPtr pPriv, Bool mute)
{
  if (pPriv->msp3430 != NULL) xf86_MSP3430SetVolume(pPriv->msp3430, mute ? MSP3430_FAST_MUTE : pPriv->volume);
  if (pPriv->tda9850 != NULL) xf86_tda9850_mute(pPriv->tda9850, mute);
  if (pPriv->tda8425 != NULL) xf86_tda8425_mute(pPriv->tda8425, mute);
  if ((pPriv->bt829 != NULL) && (pPriv->bt829->out_en)) {
    if (mute) xf86_bt829_SetP_IO(pPriv->bt829, 0x02);
    else {
      switch (pPriv->bt829->mux) {
        case BT829_MUX2:
          xf86_bt829_SetP_IO(pPriv->bt829, 0x00);
          break;
        case BT829_MUX0:
          xf86_bt829_SetP_IO(pPriv->bt829, 0x01);
          break;
        case BT829_MUX1:
          xf86_bt829_SetP_IO(pPriv->bt829, 0x00);
          break;
        default: /* shouldn't get here */
          xf86_bt829_SetP_IO(pPriv->bt829, 0x00); /* hardware default */
          break;
      }
    }
  }
}

void ATILeaveVT_Video(ScrnInfoPtr pScrn)
{
    ATIPtr   pATI      = ATIPTR(pScrn);
    ATIPortPrivPtr pPriv;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Shutting down Xvideo subsystems\n");
    if(pATI->adaptor==NULL)return;
    pPriv = pATI->adaptor->pPortPrivates[0].ptr;
    if(pPriv==NULL)return;
    ATIResetVideo(pScrn);
}

void ATIEnterVT_Video(ScrnInfoPtr pScrn)
{
    ATIPtr   pATI      = ATIPTR(pScrn);
    ATIPortPrivPtr pPriv;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Starting up Xvideo subsystems\n");
    if(pATI->adaptor==NULL)return;
    pPriv = pATI->adaptor->pPortPrivates[0].ptr;
    if(pPriv==NULL)return;
    ATIResetVideo(pScrn);
}


void ATIResetVideo(ScrnInfoPtr pScrn)
{
    ATIPtr   pATI      = ATIPTR(pScrn);
    ATIPortPrivPtr pPriv = pATI->adaptor->pPortPrivates[0].ptr;

    /* this is done here because each time the server is reset these
       could change.. Otherwise they remain constant */

    xvBrightness   = MAKE_ATOM("XV_BRIGHTNESS");
    xvSaturation   = MAKE_ATOM("XV_SATURATION");
    xvColor        = MAKE_ATOM("XV_COLOR");
    xvContrast     = MAKE_ATOM("XV_CONTRAST");
    xvColorKey     = MAKE_ATOM("XV_COLORKEY");
    xvDoubleBuffer = MAKE_ATOM("XV_DOUBLE_BUFFER");
    xvEncoding     = MAKE_ATOM("XV_ENCODING");
    xvFrequency    = MAKE_ATOM("XV_FREQ");
    xvVolume       = MAKE_ATOM("XV_VOLUME");
    xvMute         = MAKE_ATOM("XV_MUTE");
    xvHue          = MAKE_ATOM("XV_HUE");
    xv_autopaint_colorkey = MAKE_ATOM("XV_AUTOPAINT_COLORKEY");
    xv_set_defaults = MAKE_ATOM("XV_SET_DEFAULTS");

    ATIMach64WaitForFIFO(pATI, 14);
    outf(OVERLAY_SCALE_CNTL, 0x80000000);
    outf(OVERLAY_EXCLUSIVE_HORZ, 0);
    outf(OVERLAY_EXCLUSIVE_VERT, 0);
    outf(SCALER_H_COEFF0, 0x00002000);
    outf(SCALER_H_COEFF1, 0x0D06200D);
    outf(SCALER_H_COEFF2, 0x0D0A1C0D);
    outf(SCALER_H_COEFF3, 0x0C0E1A0C);
    outf(SCALER_H_COEFF4, 0x0C14140C);
    outf(SCALER_COLOUR_CNTL, (pPriv->brightness & 0x7f) |
				 (pPriv->saturation << 8) |
				 (pPriv->saturation << 16));
    outf(VIDEO_FORMAT, 0xB000B);
    outf(OVERLAY_GRAPHICS_KEY_MSK, (1 << pScrn->depth) - 1);
    outf(OVERLAY_GRAPHICS_KEY_CLR, pPriv->colorKey);
    outf(OVERLAY_KEY_CNTL, 0x50);
    outf(OVERLAY_TEST, 0);
}

/* I2C_CNTL_0 bits */
#define I2C_DONE	(1<<0)
#define I2C_NACK	(1<<1)
#define I2C_HALT	(1<<2)
#define I2C_SOFT_RST	(1<<5)
#define I2C_START	(1<<8)
#define I2C_STOP	(1<<9)
#define I2C_RECEIVE	(1<<11)
#define I2C_ABORT	(1<<12)
#define I2C_GO		(1<<10)

/* I2C_CNTL_1 bits */
#define I2C_SEL		(1<<22)
#define I2C_EN		(1<<17)


#define MPP_WRITE      0x80038CB0
#define MPP_WRITEINC   0x80338CB0
#define MPP_READ       0x84038CB0
#define MPP_READINC    0x84338CB0

#define IMPACTTV_I2C_CNTL      0x0015

#define TB_SDA_GET	0x40
#define TB_SDA_SET	0x20
#define TB_SDA_DIR	0x10
#define TB_SCL_GET	0x04
#define TB_SCL_SET	0x02
#define TB_SCL_DIR	0x01

static Bool MPP_wait(ATIPtr pATI)
{
CARD32 count = 0x100;

while(count && (in8(MPP_CONFIG+3) & 0x40))
{
   count --;
/*   usleep(1); */
}
if(count) return TRUE;

count = 0x100;

while(count && (in8(MPP_CONFIG+3) & 0x40))
{
   count --;
/*   usleep(1); */
}
if(count) return TRUE;

return FALSE;
}

static void MPP_ImpactTV_addr(ATIPtr pATI, CARD16 addr)
{
    MPP_wait(pATI);
    outr(MPP_CONFIG, MPP_WRITEINC);
    outr(MPP_ADDR, 0x8);
    out8(MPP_DATA, addr & 0xff);
    MPP_wait(pATI);
    out8(MPP_DATA, (addr >> 8) & 0xff);
    MPP_wait(pATI);
    outr(MPP_CONFIG, MPP_WRITE);
    outr(MPP_ADDR, 0x18);
}

static void MPP_ImpactTV_write32(ATIPtr pATI, CARD16 addr, CARD32 data)
{
    MPP_ImpactTV_addr(pATI, addr);
    MPP_wait(pATI);
    outr(MPP_CONFIG, MPP_WRITEINC);
    out8(MPP_DATA, data & 0xff);
    out8(MPP_DATA, (data>>8) & 0xff);
    out8(MPP_DATA, (data>>16) & 0xff);
    out8(MPP_DATA, (data>>24) & 0xff);
    MPP_wait(pATI);
}

static void MPP_ImpactTV_read32(ATIPtr pATI, CARD16 addr, CARD32 *data)
{
    MPP_ImpactTV_addr(pATI, addr);
    MPP_wait(pATI);
    outr(MPP_CONFIG, MPP_READINC);
    *data = in8(MPP_DATA);
    *data |= in8(MPP_DATA)<<8;
    *data |= in8(MPP_DATA)<<16;
    *data |= in8(MPP_DATA)<<24;
    MPP_wait(pATI);
}

static Bool  Detect_ImpactTV(ScrnInfoPtr pScrn, ATIPtr pATI)
{
    CARD8 init;
    CARD8 id;
    
    outr(TVO_CNTL, 0x0);
    outr(MPP_STROBE_CONFIG, 0x383);
    outr(DAC_CNTL, inr(DAC_CNTL) & 0xFFFFBFFF);
    outr(MPP_CONFIG, MPP_READ);
    MPP_wait(pATI);
    outr(MPP_ADDR, 0xA);
    init = in8(MPP_DATA);
    MPP_wait(pATI);
    outr(MPP_ADDR, 0xB);
    id = in8(MPP_DATA);
    MPP_wait(pATI);
    outr(MPP_CONFIG, MPP_WRITE);
    
    if(!init)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "ImpactTV chip id 0x%02x\n", id);
	return TRUE;
    } else
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "No ImpactTV chip found\n");
        return FALSE;
    }
}

static void	Mach64_TB_setbits(I2CBusPtr b, CARD32  bits)
{
    ATIPortPrivPtr pPriv = (ATIPortPrivPtr)(b->DriverPrivate.ptr);
    ScrnInfoPtr pScrn = xf86Screens[b->scrnIndex];
    ATIPtr pATI = ATIPTR(pScrn);

    MPP_ImpactTV_addr(pATI, IMPACTTV_I2C_CNTL);
    MPP_wait(pATI);
    pPriv->i2c_status = bits;
    outr(MPP_CONFIG, MPP_WRITE);
    out8(MPP_DATA, pPriv->i2c_status); 
/*    xf86DrvMsg(b->scrnIndex, X_INFO, "write MPP_DATA = 0x%02x\n", pPriv->i2c_status);  */
    MPP_wait(pATI);
}

static CARD32	Mach64_TB_getbits(I2CBusPtr b)
{
    ScrnInfoPtr pScrn = xf86Screens[b->scrnIndex];
    ATIPtr pATI = ATIPTR(pScrn);
    CARD8 a;

    MPP_ImpactTV_addr(pATI, IMPACTTV_I2C_CNTL);
    MPP_wait(pATI);
    outr(MPP_CONFIG, MPP_READ);
    MPP_wait(pATI);
    a = in8(MPP_DATA); 
/*    xf86DrvMsg(b->scrnIndex, X_INFO, "read MPP_DATA = 0x%02x\n", a);  */
    return a;
}


static void	RagePro_setbits(I2CBusPtr b, CARD32  bits)
{
    ATIPortPrivPtr pPriv = (ATIPortPrivPtr)(b->DriverPrivate.ptr);
    ScrnInfoPtr pScrn = xf86Screens[b->scrnIndex];
    ATIPtr pATI = ATIPTR(pScrn);

    pPriv->i2c_status = bits;
    out8(I2C_CNTL_0+1, pPriv->i2c_status); 
}

static CARD32	RagePro_getbits(I2CBusPtr b)
{
    ScrnInfoPtr pScrn = xf86Screens[b->scrnIndex];
    ATIPtr pATI = ATIPTR(pScrn);
    CARD8 a;

    a = in8(I2C_CNTL_0+1); 
    return a;
}


/* Note: GPIO is broken right now. Moreover there are actually 
    two GPIO modes and also a DAC+GEN_TEST mode */

static void	Mach64_GPIO_setbits(I2CBusPtr b, CARD32  bits)
{
    ATIPortPrivPtr pPriv = (ATIPortPrivPtr)(b->DriverPrivate.ptr);
    ScrnInfoPtr pScrn = xf86Screens[b->scrnIndex];
    ATIPtr pATI = ATIPTR(pScrn);

    pPriv->i2c_status = bits;
    outr(GP_IO, pPriv->i2c_status); 
}

static CARD32	Mach64_GPIO_getbits(I2CBusPtr b)
{
    ScrnInfoPtr pScrn = xf86Screens[b->scrnIndex];
    ATIPtr pATI = ATIPTR(pScrn);
    CARD32 a;

    a = inr(GP_IO); 
    return a;
}

#define ATI_DAC_CNTL_MASK   (~(0x01000000 | 0x08000000))
#define ATI_GEN_TEST_MASK   (~(0x00000001 | 0x00000008 | 0x00000020 ))

/* this is a hack.. but it works and we should not be getting any more i2c modes */

static void	Mach64_DAC_GEN_TEST_setbits(I2CBusPtr b, CARD32  bits)
{
    ATIPortPrivPtr pPriv = (ATIPortPrivPtr)(b->DriverPrivate.ptr);
    ScrnInfoPtr pScrn = xf86Screens[b->scrnIndex];
    ATIPtr pATI = ATIPTR(pScrn);
    CARD32 tmp;

    pPriv->i2c_status = bits;
    tmp = inr(DAC_CNTL);
    outr(DAC_CNTL, (tmp & ATI_DAC_CNTL_MASK) | (pPriv->i2c_status & ATI_DAC_CNTL_MASK)); 
    tmp = inr(GEN_TEST_CNTL);
    outr(GEN_TEST_CNTL, (tmp & ATI_GEN_TEST_MASK) | (pPriv->i2c_status & ATI_GEN_TEST_MASK));
}

static CARD32	Mach64_DAC_GEN_TEST_getbits(I2CBusPtr b)
{
    ScrnInfoPtr pScrn = xf86Screens[b->scrnIndex];
    ATIPtr pATI = ATIPTR(pScrn);
    CARD32 a,d;

    a = inr(DAC_CNTL); 
    d = inr(GEN_TEST_CNTL);
    return ((a & ATI_DAC_CNTL_MASK) | (d & ATI_GEN_TEST_MASK));
}


const struct 
{
   char *name; 
   int type;
} ATI_tuners[32] =
    {
        /* name	,index to tuner_parms table */
	{"NO TUNER"		, -1},
	{"FI1236"		, TUNER_TYPE_FI1236},
	{"FI1236"		, TUNER_TYPE_FI1236},
	{"FI1216"		, TUNER_TYPE_FI1216},
	{"FI1246"		, -1},
	{"FI1216MF"		, TUNER_TYPE_FI1216},
	{"FI1236"		, TUNER_TYPE_FI1236},
	{"FI1256"		, -1},
	{"FI1236"		, TUNER_TYPE_FI1236},
	{"FI1216"		, TUNER_TYPE_FI1216},
	{"FI1246"		, -1},
	{"FI1216MF"		, TUNER_TYPE_FI1216},
	{"FI1236"		, TUNER_TYPE_FI1236},
	{"TEMIC-FN5AL"		, TUNER_TYPE_TEMIC_FN5AL},
	{"FQ1216ME/P"		, TUNER_TYPE_FI1216},
	{"UNKNOWN-15"		, -1},
	{"Alps TSBH5"		, -1},
	{"Alps TSCxx"		, -1},
	{"Alps TSCH5 FM"	, -1},
	{"UNKNOWN-19"		, -1},
	{"UNKNOWN-20"		, -1},
	{"UNKNOWN-21"		, -1},
	{"UNKNOWN-22"		, -1},
	{"UNKNOWN-23"		, -1},
        {"UNKNOWN-24"		, -1},
	{"UNKNOWN-25"		, -1},
	{"UNKNOWN-26"		, -1},
	{"UNKNOWN-27"		, -1},
	{"UNKNOWN-28"		, -1},
	{"UNKNOWN-29"		, -1},
        {"UNKNOWN-30"		, -1},
	{"UNKNOWN-31"		, -1}
    };

static void    ATI_I2CStop   (I2CDevPtr d)
{
    ATIPortPrivPtr pPriv = (ATIPortPrivPtr)(d->pI2CBus->DriverPrivate.ptr);
    I2CBusPtr b = d->pI2CBus;
    
    SCLSET(0);
    SDASET(0);
    SCLSET(1);
    SDASET(1);
    SCLDIR(0);
    SDADIR(0);
}

static Bool	ATI_I2CPutByte(I2CDevPtr d, I2CByte data)
{
    ATIPortPrivPtr pPriv = (ATIPortPrivPtr)(d->pI2CBus->DriverPrivate.ptr);
    I2CBusPtr b = d->pI2CBus;
    int i;
    int nack;

    SDADIR(1);
    for(i=7; i>=0; i--)
    {
       SCLSET(0);
       SDASET(data & (1<<i));
       SCLSET(1);
    }
    SCLSET(0);
    SDADIR(0);
    SDASET(1);
    SCLSET(1);
    nack = ISSDA;
    SCLSET(0);
    return !nack;
}

static Bool	ATI_I2CGetByte(I2CDevPtr d, I2CByte *data, Bool last)
{
    ATIPortPrivPtr pPriv = (ATIPortPrivPtr)(d->pI2CBus->DriverPrivate.ptr);
    I2CBusPtr b = d->pI2CBus;
    int i;

    *data = 0;
    SDADIR(0);
    for(i=7; i>=0; i--)
    {
       SCLSET(1);
       if(ISSDA)*data |= (1<<i);
       I2CSLEEP;
       SCLSET(0);
    }
    SDADIR(1);
    SDASET(last ? 1 : 0);
    SCLSET(1);
    SCLSET(0);
    SDADIR(0);
    return TRUE;
}

static Bool    ATI_I2CAddress(I2CDevPtr d, I2CSlaveAddr addr)
{
    ATIPortPrivPtr pPriv = (ATIPortPrivPtr)(d->pI2CBus->DriverPrivate.ptr);
    I2CBusPtr b = d->pI2CBus;

    SCLDIR(1);
    SDADIR(1);
    SDASET(1);
    SCLSET(1);
    SDASET(0);
    SCLSET(0);
    
    if(ATI_I2CPutByte(d, addr)) return TRUE;
    
    ATI_I2CStop(d);
    return FALSE;
}


static void ATIInitI2C(ScrnInfoPtr pScrn, ATIPortPrivPtr pPriv)
{
    ATIPtr pATI = ATIPTR(pScrn);

    pPriv->fi1236 = NULL;
    pPriv->bt829 = NULL;
    pPriv->tda9850 = NULL;
    pPriv->tda8425 = NULL;
    pPriv->msp3430 = NULL;
    
    if(pPriv->i2c==NULL) {
    if(!xf86LoadSubModule(pScrn,"i2c")) {
    	xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"Unable to initialize i2c bus\n");
	pPriv->i2c = NULL;
	return;
        } 
    xf86LoaderReqSymbols("xf86CreateI2CBusRec", 
    			  "xf86I2CBusInit",
			  "xf86DestroyI2CBus",
			  "xf86CreateI2CDevRec",
			  "xf86DestroyI2CDevRec",
			  "xf86I2CDevInit",
			  "xf86I2CWriteRead",
			  NULL);
    pPriv->i2c = CreateI2CBusRec();
    pPriv->i2c->scrnIndex = pScrn->scrnIndex;
    pPriv->i2c->I2CPutByte = ATI_I2CPutByte;
    pPriv->i2c->I2CGetByte = ATI_I2CGetByte;
    pPriv->i2c->I2CAddress = ATI_I2CAddress;
    pPriv->i2c->I2CStop = ATI_I2CStop;
    pPriv->i2c->DriverPrivate.ptr = (pointer)pPriv;
    pPriv->i2c_status = 0;
    
    if((pATI->Chip >= ATI_CHIP_264GTPRO) && (pATI->Chip <= ATI_CHIP_MOBILITY)) 
    {   
       pPriv->i2c->BusName = "Rage Pro i2c bus";
       pPriv->setbits = RagePro_setbits;
       pPriv->getbits = RagePro_getbits;
       pPriv->scl_dir = 0;
       pPriv->sda_dir = 0;
       pPriv->scl_set = 0x40;
       pPriv->sda_set = 0x80;
       pPriv->scl_get = 0x40;
       pPriv->sda_get = 0x80;
       out8(I2C_CNTL_1+2, (I2C_SEL >>16));
       out8(I2C_CNTL_0+0, (I2C_DONE | I2C_NACK | I2C_HALT | I2C_SOFT_RST ));
    } else 
    if(Detect_ImpactTV(pScrn, pATI) && 
	(pATI->Chip <= ATI_CHIP_264GTDVD))
    {  /* This mode is used _only_ on RageII+DVD boards with Impact TV chip */
       pPriv->i2c->BusName = "Mach64 i2c bus via ImpactTV";
       pPriv->setbits = Mach64_TB_setbits;
       pPriv->getbits = Mach64_TB_getbits;
       pPriv->scl_dir = TB_SCL_DIR;
       pPriv->sda_dir = TB_SDA_DIR;
       pPriv->scl_set = TB_SCL_SET;
       pPriv->sda_set = TB_SDA_SET;
       pPriv->scl_get = TB_SCL_GET;
       pPriv->sda_get = TB_SDA_GET;
       MPP_ImpactTV_write32(pATI, IMPACTTV_I2C_CNTL, 0x5500);
    } else 
    {
       pPriv->i2c->BusName = "Mach64 i2c bus via GPIO";
       pPriv->setbits = Mach64_GPIO_setbits;
       pPriv->getbits = Mach64_GPIO_getbits;
       pPriv->scl_dir = 0x08000000;
       pPriv->sda_dir = 0x00100000;
       pPriv->scl_set = 0x00000800;
       pPriv->sda_set = 0x00000010;
       pPriv->scl_get = 0x00000800;
       pPriv->sda_get = 0x00000010;
    }
    
    if(!I2CBusInit(pPriv->i2c)){
    	xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"Failed to register i2c bus\n");
#if 0
	pPriv->i2c = NULL;
	return;
#endif
    	}
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "StartTimeout = %d\n", pPriv->i2c->StartTimeout);
    
    if(!pPriv->MM_TABLE_valid)
    {
       xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Detecting addon board, trying default i2c mode\n");
       ATI_detect_addon(pPriv);
       if(pPriv->setbits == Mach64_GPIO_setbits)
       {
       /****/
       if(!pPriv->addon_board) /* try another gpio mode, this is only needed for pre-RagePro mach64 cards */
       {
          xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Addon board not detected, trying second GPIO mode\n");
          pPriv->scl_dir = 0x04000000;
	  pPriv->sda_dir = 0x10000000;
	  pPriv->scl_set = 0x00000400;
	  pPriv->sda_set = 0x00001000;
	  pPriv->scl_get = 0x00000400;
	  pPriv->sda_get = 0x00001000;
	  ATI_detect_addon(pPriv);
          if(pPriv->addon_board)xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Addon board detected, using second GPIO mode\n");
       }
       if(!pPriv->addon_board) /* try GEN_TEST mode */
       {
          xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Addon board not detected, trying DAC+GEN TEST mode\n");
          pPriv->scl_dir = 0x08000000;
	  pPriv->sda_dir = 0x00000020;
	  pPriv->scl_set = 0x01000000;
	  pPriv->sda_set = 0x00000001;
	  pPriv->scl_get = 0x01000000;
	  pPriv->sda_get = 0x00000008;
	  pPriv->setbits = Mach64_DAC_GEN_TEST_setbits;
	  pPriv->getbits = Mach64_DAC_GEN_TEST_getbits;
	  outr(GP_IO, inr(GP_IO) & 0x7fffffff);
	  ATI_detect_addon(pPriv);
          if(pPriv->addon_board)xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Addon board detected, using DAC+GEN TEST mode\n");
       }
       /****/
       }
    }
       else pPriv->addon_board = FALSE;
    
    /* no multimedia capabilities detected */
    if(!pPriv->MM_TABLE_valid && !pPriv->addon_board)
    {
       if((pATI->Chip>=ATI_CHIP_264LTPRO) &&
          (pATI->Chip <= ATI_CHIP_MOBILITY))
       {
          xf86DrvMsg(pScrn->scrnIndex, X_INFO, "No video input capabilities detected\n");
          return;
       }
       pPriv->board_info = 1;
    }


    if(!xf86LoadSubModule(pScrn,"fi1236"))
    {
    xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Unable to initialize fi1236 driver\n");
    }
    else
    {
    xf86LoaderReqSymbols(FI1236SymbolsList, NULL);
    if(pPriv->fi1236 == NULL)
    {
    	pPriv->fi1236 = xf86_Detect_FI1236(pPriv->i2c, FI1236_ADDR_1);
    }
    if(pPriv->fi1236 == NULL)
    {
    	pPriv->fi1236 = xf86_Detect_FI1236(pPriv->i2c, FI1236_ADDR_2);
    }
    }
    if(pPriv->fi1236 != NULL)
    {
         xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Detected %s device at 0x%02x\n", 
	       ATI_tuners[pPriv->board_info & 0xf].name,
               FI1236_ADDR(pPriv->fi1236));
         xf86_FI1236_set_tuner_type(pPriv->fi1236, ATI_tuners[pPriv->board_info & 0xf].type);
    }

    if(!xf86LoadSubModule(pScrn, "bt829"))
    {
       xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Unable to initialize bt829 driver\n");
    } else 
    {
      xf86LoaderReqSymbols(BT829SymbolsList, NULL);
      if(pPriv->bt829 == NULL)
      {
         pPriv->bt829 = xf86_bt829_Detect(pPriv->i2c, BT829_ATI_ADDR_1);
      }
      if(pPriv->bt829 == NULL)
      {
         pPriv->bt829 = xf86_bt829_Detect(pPriv->i2c, BT829_ATI_ADDR_2);
      }
      if(pPriv->bt829 != NULL)
      {
         if(pPriv->MM_TABLE_valid && 
	     (pATI->Chip >= ATI_CHIP_264GTPRO) && 
	     (pATI->Chip <= ATI_CHIP_MOBILITY))
	 {
           xf86_bt829_SetP_IO(pPriv->bt829, 0x02); /* mute */
           xf86_bt829_SetOUT_EN(pPriv->bt829, 1);
	 }
	 xf86DrvMsg(pScrn->scrnIndex, X_INFO, "vpole=%d\n", pPriv->bt829->out_en);
	 pPriv->bt829->tunertype = pPriv->board_info & 0x0f;
         if(xf86_bt829_ATIInit(pPriv->bt829) < 0)pPriv->bt829 = NULL; /* disable it */
      }
    }

    if(pPriv->bt829 == NULL)  /* No decoder found.. no sense initializing audio
                                 chips */
    {
       return;
    }

    if(!xf86LoadSubModule(pScrn, "msp3430"))
    {
       xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Unable to initialize msp3430 driver\n");
    } 
    else 
    {
    xf86LoaderReqSymbols(MSP3430SymbolsList, NULL);
    if(pPriv->msp3430 == NULL)
    {
       pPriv->msp3430 = xf86_DetectMSP3430(pPriv->i2c, MSP3430_ADDR_1);
    }
    if(pPriv->msp3430 == NULL)
    {
       pPriv->msp3430 = xf86_DetectMSP3430(pPriv->i2c, MSP3430_ADDR_2);
    }
#if 0 /* this can conflict with bt829 */
    if(pPriv->msp3430 == NULL)
    {
       pPriv->msp3430 = xf86_DetectMSP3430(pPriv->i2c, MSP3430_ADDR_3);
    }
#endif
    }
    if(pPriv->msp3430 != NULL)
    {
       xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Detected MSP3430 at 0x%02x\n", 
                 MSP3430_ADDR(pPriv->msp3430));
    }

    if(!xf86LoadSubModule(pScrn, "tda9850"))
    {
       xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Unable to initialize tda9850 driver\n");
    } else 
    {
       xf86LoaderReqSymbols(TDA9850SymbolsList, NULL);
       if(pPriv->tda9850 == NULL)
       {
          pPriv->tda9850 = xf86_Detect_tda9850(pPriv->i2c, TDA9850_ADDR_1);
       }
       if(pPriv->tda9850 != NULL)
       {
          if(!xf86_tda9850_init(pPriv->tda9850))pPriv->tda9850 = NULL; /* disable it */
       }
       if(pPriv->tda9850 != NULL)
       {
          xf86_tda9850_setaudio(pPriv->tda9850);
          xf86_tda9850_mute(pPriv->tda9850, TRUE);
       }  
    }
    if(!xf86LoadSubModule(pScrn, "tda8425"))
    {
       xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Unable to initialize tda8425 driver\n");
    } else 
    {
       /* we don't need to force detection of TDA8425 for any of mach64 cards */
       xf86LoaderReqSymbols(TDA8425SymbolsList, NULL);
       if(pPriv->tda8425 == NULL)
       {
          pPriv->tda8425 = xf86_Detect_tda8425(pPriv->i2c, TDA8425_ADDR_1, FALSE);
       }
       if(pPriv->tda8425 != NULL)
       {
          if(!xf86_tda8425_init(pPriv->tda8425))pPriv->tda8425 = NULL; /* disable it */
       }
       if(pPriv->tda8425 != NULL)
       {
          xf86_tda8425_mute(pPriv->tda8425, TRUE);
       }  
    }

    if(pPriv->i2c != NULL) ATI_board_setmisc(pPriv);      
}


void ATIReadMM_TABLE(ScrnInfoPtr pScrn, ATIPortPrivPtr pPriv)
{
     ATIPtr pATI = ATIPTR(pScrn);
     CARD16 mm_table;
     CARD16 bios_header;

     if(pATI->VBIOS==NULL){
     	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Cannot access BIOS: info->VBIOS==NULL.\n");
     	}

     xf86DrvMsg(pScrn->scrnIndex, X_INFO, "0x%02x 0x%02x\n", pATI->VBIOS[0],
               pATI->VBIOS[1]);	
     bios_header=pATI->VBIOS[0x48];
     bios_header+=(((int)pATI->VBIOS[0x49]+0)<<8);	     

	/* mach64 table is at different address than r128 one */	
     mm_table=pATI->VBIOS[bios_header+0x46];
     if(mm_table==0)
     {
         xf86DrvMsg(pScrn->scrnIndex,X_INFO,"No MM_TABLE found\n",bios_header,mm_table);
	 pPriv->MM_TABLE_valid = FALSE;
	 return;
     }    
     mm_table+=(((int)pATI->VBIOS[bios_header+0x47]+0)<<8)-2;

     xf86DrvMsg(pScrn->scrnIndex,X_INFO,"VIDEO BIOS TABLE OFFSETS: bios_header=0x%04x mm_table=0x%04x\n",bios_header,mm_table);

     if(mm_table>0)
     {
	 memcpy(&(pPriv->MM_TABLE), &(pATI->VBIOS[mm_table]), sizeof(_MM_TABLE));
         xf86DrvMsg(pScrn->scrnIndex, X_INFO, "MM_TABLE: %02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x\n",
	     pPriv->MM_TABLE.table_revision,
    	     pPriv->MM_TABLE.table_size,
	     pPriv->MM_TABLE.tuner_type,
    	     pPriv->MM_TABLE.audio_chip,
	     pPriv->MM_TABLE.product_id,
    	     pPriv->MM_TABLE.tuner_voltage_teletext_fm,
	     pPriv->MM_TABLE.i2c_config,
	     pPriv->MM_TABLE.video_decoder_type,
    	     pPriv->MM_TABLE.video_decoder_host_config,
	     pPriv->MM_TABLE.input[0],
    	     pPriv->MM_TABLE.input[1],
	     pPriv->MM_TABLE.input[2],
    	     pPriv->MM_TABLE.input[3],
	     pPriv->MM_TABLE.input[4]);
	 pPriv->MM_TABLE_valid = TRUE;
	 pPriv->board_info = pPriv->MM_TABLE.tuner_type;
     } else {
         xf86DrvMsg(pScrn->scrnIndex,X_INFO,"No MM_TABLE found",bios_header,mm_table);
	 pPriv->MM_TABLE_valid = FALSE;	 
     }    
}

static int ATI_eeprom_addresses[] = { 0xA8, 0};

void ATI_read_eeprom(ATIPortPrivPtr pPriv)
{
   I2CDevRec d;
   unsigned char data[5];
   int i;
   if(pPriv->i2c == NULL) return;
   
   d.DevName = "temporary";
   d.pI2CBus = pPriv->i2c;
   d.NextDev = NULL;
   d.StartTimeout = pPriv->i2c->StartTimeout;
   d.BitTimeout = pPriv->i2c->BitTimeout;
   d.AcknTimeout = pPriv->i2c->AcknTimeout;
   d.ByteTimeout = pPriv->i2c->ByteTimeout;
   pPriv->EEPROM_addr = 0;

   for(i=0;ATI_eeprom_addresses[i];i++)
   {
     d.SlaveAddr = ATI_eeprom_addresses[i];
     data[0]=0x00;
     if(!I2C_WriteRead(&d, data, 1, NULL, 0))continue;
     if(!I2C_WriteRead(&d, NULL, 0, data, 5))continue;
     if(!memcmp(data, "ATI", 3))
     {
        pPriv->EEPROM_present = TRUE;
	pPriv->EEPROM_addr = ATI_eeprom_addresses[i];
	break;
     }
     xf86DrvMsg(pPriv->i2c->scrnIndex, X_INFO, "Device at eeprom addr 0x%02x found, returned 0x%02x-0x%02x-0x%02x-0x%02x-0x%02x\n",
         d.SlaveAddr,
     	 data[0], data[1], data[2], data[3], data[4]);
   }


}

static int ATI_addon_addresses[] = { 0x70, 0x40, 0x78, 0x72, 0x42, 0};

void ATI_detect_addon(ATIPortPrivPtr pPriv)
{
   I2CDevRec d;
   CARD8 data[1];
   int i;
   if(pPriv->i2c == NULL) return;
   
   d.DevName = "temporary";
   d.pI2CBus = pPriv->i2c;
   d.NextDev = NULL;
   d.StartTimeout = pPriv->i2c->StartTimeout;
   d.BitTimeout = pPriv->i2c->BitTimeout;
   d.AcknTimeout = pPriv->i2c->AcknTimeout;
   d.ByteTimeout = pPriv->i2c->ByteTimeout;
   pPriv->addon_board = FALSE;

   for(i=0;ATI_addon_addresses[i];i++)
   {
     d.SlaveAddr = ATI_addon_addresses[i];
     data[0]=0xFF;
     if(!I2C_WriteRead(&d, data, 1, NULL, 0))continue;
     if(!I2C_WriteRead(&d, NULL, 0, data, 1))continue;
     if((data[0] == 0xFF) || (data[0] == 0x00))continue;
     pPriv->addon_board = TRUE;
     pPriv->board_control = ATI_addon_addresses[i];
     pPriv->board_info = data[0];
     xf86DrvMsg(pPriv->i2c->scrnIndex, X_INFO, "Standalone board at addr 0x%02x found, returned 0x%02x\n",
         d.SlaveAddr,
     	 data[0]);
     ATI_board_setmisc(pPriv);
     break;
   }
}

static XF86VideoAdaptorPtr
ATIAllocAdaptor(ScrnInfoPtr pScrn)
{
    XF86VideoAdaptorPtr adapt;
    ATIPtr pATI = ATIPTR(pScrn);
    ATIPortPrivPtr pPriv;
    
    if(!(adapt = xf86XVAllocateVideoAdaptorRec(pScrn)))
	return NULL;

    if(!(pPriv = xcalloc(1, sizeof(ATIPortPrivRec) + sizeof(DevUnion))))
    {
	xfree(adapt);
	return NULL;
    }

    adapt->pPortPrivates = (DevUnion*)(&pPriv[1]);
    adapt->pPortPrivates[0].ptr = (pointer)pPriv;

    pPriv->colorKey = (3<<pScrn->offset.red)|(2<<pScrn->offset.green)|(1<pScrn->offset.blue);
    pPriv->doubleBuffer = TRUE;
    pPriv->videoStatus = 0;
    pPriv->brightness = 0;
    pPriv->saturation = 16;
    pPriv->currentBuffer = 0;

    pPriv->video_stream_active = FALSE;
    pPriv->encoding = 1;
    pPriv->frequency = 1000;
    pPriv->volume = 0x01;
    pPriv->mute = TRUE;
    pPriv->v=0;
    
    pPriv->autopaint_colorkey = TRUE;
   
    /* choose ecp_div setting */
    pPriv->ecp_div = (pATI->NewHW.pll_vclk_cntl & PLL_ECP_DIV) >> 4;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "ecp_div is %d\n", pPriv->ecp_div);

    return adapt;
}

static XF86VideoAdaptorPtr
ATISetupImageVideo(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    ATIPtr pATI = ATIPTR(pScrn);
    ATIPortPrivPtr pPriv;
    XF86VideoAdaptorPtr adapt;

    if(pATI->adaptor!=NULL){
    	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Reinitializing Xvideo subsystems\n");
    	ATIResetVideo(pScrn);
    	return pATI->adaptor;
	}

    if(!(adapt = ATIAllocAdaptor(pScrn)))
	return NULL;
	
    if(pATI->Chip < ATI_CHIP_264VTB) return NULL;
    if(pATI->Chip > ATI_CHIP_Mach64) return NULL;
   	
    pPriv = (ATIPortPrivPtr)(adapt->pPortPrivates[0].ptr);
    
    ATIReadMM_TABLE(pScrn, pPriv);

    ATIInitI2C(pScrn,pPriv);
    
       adapt->flags = VIDEO_OVERLAID_IMAGES | VIDEO_CLIP_TO_VIEWPORT;
       adapt->name = "ATI mach64 Video Overlay";
       if(pPriv->bt829 != NULL)
       {
          adapt->type = XvWindowMask | XvInputMask | XvImageMask | XvVideoMask;
          adapt->nEncodings = 13;
	  if(pATI->Chip >= ATI_CHIP_264GTPRO)
	  	adapt->pEncodings = RagePro_InputVideoEncodings;
		else
	  	adapt->pEncodings = VT_GT_InputVideoEncodings;
       } else
       {
          adapt->type = XvWindowMask | XvInputMask | XvImageMask;
          adapt->nEncodings = 1;
          adapt->pEncodings =  &DummyEncoding;
       }
       adapt->nFormats = NUM_FORMATS;
       adapt->pFormats = Formats;
       adapt->nPorts = 1;
       if(pPriv->MM_TABLE_valid && (pATI->Chip == ATI_CHIP_264GTDVD))
       {
       	  adapt->nAttributes = NUM_ATTRIBUTES-1;
	  adapt->pAttributes = AIWClassicAttributes;
       } else
       {
          adapt->nAttributes = NUM_ATTRIBUTES;
          adapt->pAttributes = Attributes;
       }
       adapt->nImages = NUM_IMAGES;
       adapt->pImages = Images;
       adapt->PutVideo = ATIPutVideo;
       adapt->PutStill = NULL;
       adapt->GetVideo = NULL;
       adapt->GetStill = NULL;
       adapt->StopVideo = ATIStopVideo;
       adapt->SetPortAttribute = ATISetPortAttribute;
       adapt->GetPortAttribute = ATIGetPortAttribute;
       adapt->QueryBestSize = ATIQueryBestSize;
       adapt->PutImage = ATIPutImage;
       adapt->QueryImageAttributes = ATIQueryImageAttributes;
    

    pATI->adaptor = adapt;

    REGION_INIT(pScreen, &(pPriv->clip), NullBox, 0);

    ATIResetVideo(pScrn);
    
    if(pATI->VBIOS!=NULL){
    	xfree(pATI->VBIOS);
	pATI->VBIOS=NULL;
    	}

    return adapt;
}

/* I really should stick this in miregion */
static Bool
RegionsEqual(RegionPtr A, RegionPtr B)
{
    int *dataA, *dataB;
    int num;

    num = REGION_NUM_RECTS(A);
    if(num != REGION_NUM_RECTS(B))
	return FALSE;

    if((A->extents.x1 != B->extents.x1) ||
       (A->extents.x2 != B->extents.x2) ||
       (A->extents.y1 != B->extents.y1) ||
       (A->extents.y2 != B->extents.y2))
	return FALSE;

    dataA = (int*)REGION_RECTS(A);
    dataB = (int*)REGION_RECTS(B);

    while(num--) {
	if((dataA[0] != dataB[0]) || (dataA[1] != dataB[1]))
	   return FALSE;
	dataA += 2;
	dataB += 2;
    }

    return TRUE;
}


/* ATIClipVideo -

   Takes the dst box in standard X BoxRec form (top and left
   edges inclusive, bottom and right exclusive).  The new dst
   box is returned.  The source boundaries are given (xa, ya
   inclusive, xb, yb exclusive) and returned are the new source
   boundaries in 16.16 fixed point.
*/

#define DummyScreen screenInfo.screens[0]

static Bool
ATIClipVideo(
  BoxPtr dst,
  INT32 *xa,
  INT32 *xb,
  INT32 *ya,
  INT32 *yb,
  RegionPtr reg,
  INT32 width,
  INT32 height
){
    INT32 vscale, hscale, delta;
    BoxPtr extents = REGION_EXTENTS(DummyScreen, reg);
    int diff;

    hscale = ((*xb - *xa) << 16) / (dst->x2 - dst->x1);
    vscale = ((*yb - *ya) << 16) / (dst->y2 - dst->y1);

    *xa <<= 16; *xb <<= 16;
    *ya <<= 16; *yb <<= 16;

    diff = extents->x1 - dst->x1;
    if(diff > 0) {
	dst->x1 = extents->x1;
	*xa += diff * hscale;
    }
    diff = dst->x2 - extents->x2;
    if(diff > 0) {
	dst->x2 = extents->x2;
	*xb -= diff * hscale;
    }
    diff = extents->y1 - dst->y1;
    if(diff > 0) {
	dst->y1 = extents->y1;
	*ya += diff * vscale;
    }
    diff = dst->y2 - extents->y2;
    if(diff > 0) {
	dst->y2 = extents->y2;
	*yb -= diff * vscale;
    }

    if(*xa < 0) {
	diff =  (- *xa + hscale - 1)/ hscale;
	dst->x1 += diff;
	*xa += diff * hscale;
    }
    delta = *xb - (width << 16);
    if(delta > 0) {
	diff = (delta + hscale - 1)/ hscale;
	dst->x2 -= diff;
	*xb -= diff * hscale;
    }
    if(*xa >= *xb) return FALSE;

    if(*ya < 0) {
	diff =  (- *ya + vscale - 1)/ vscale;
	dst->y1 += diff;
	*ya += diff * vscale;
    }
    delta = *yb - (height << 16);
    if(delta > 0) {
	diff = (delta + vscale - 1)/ vscale;
	dst->y2 -= diff;
	*yb -= diff * vscale;
    }
    if(*ya >= *yb) return FALSE;

    if((dst->x1 != extents->x1) || (dst->x2 != extents->x2) ||
       (dst->y1 != extents->y1) || (dst->y2 != extents->y2))
    {
	RegionRec clipReg;
	REGION_INIT(DummyScreen, &clipReg, dst, 1);
	REGION_INTERSECT(DummyScreen, reg, reg, &clipReg);
	REGION_UNINIT(DummyScreen, &clipReg);
    }
    return TRUE;
}

static void
ATIStopVideo(ScrnInfoPtr pScrn, pointer data, Bool cleanup)
{
  ATIPtr pATI = ATIPTR(pScrn);
  ATIPortPrivPtr pPriv = (ATIPortPrivPtr)data;

  REGION_EMPTY(pScrn->pScreen, &pPriv->clip);

  xf86DrvMsg(pScrn->scrnIndex, X_INFO, "StopVideo %s\n", cleanup ? "cleanup": "");

  if(cleanup) {
     ATIMach64WaitForFIFO(pATI, 2); 
     if(pPriv->videoStatus & CLIENT_VIDEO_ON) {
	outf(OVERLAY_SCALE_CNTL, 0x80000000);
     }
     if(pPriv->video_stream_active) {
        outf(TRIG_CNTL, 0x0);
	pPriv->video_stream_active = FALSE;
        ATIMuteAudio(pPriv, TRUE);
	if(pPriv->i2c != NULL) ATI_board_setmisc(pPriv);
	
     }
     if(pPriv->linear) {
	xf86FreeOffscreenLinear(pPriv->linear);
	pPriv->linear = NULL;
     }
     pPriv->videoStatus = 0;
  } else {
     if(pPriv->videoStatus & CLIENT_VIDEO_ON) {
	pPriv->videoStatus |= OFF_TIMER;
	pPriv->offTime = currentTime.milliseconds + OFF_DELAY;
     }
  }
}

static int
ATISetPortAttribute(
  ScrnInfoPtr pScrn,
  Atom attribute,
  INT32 value,
  pointer data
){
  ATIPtr pATI = ATIPTR(pScrn);
  ATIPortPrivPtr pPriv = (ATIPortPrivPtr)data;

  if(attribute == xv_autopaint_colorkey) {
  	pPriv->autopaint_colorkey = value;
  } else
  if(attribute == xv_set_defaults) {
        ATISetPortAttribute(pScrn, xv_autopaint_colorkey, TRUE, data);
        ATISetPortAttribute(pScrn, xvBrightness, 0, data);
        ATISetPortAttribute(pScrn, xvSaturation, 0, data);
        ATISetPortAttribute(pScrn, xvContrast,   0, data);
        ATISetPortAttribute(pScrn, xvHue,   0, data);
        ATISetPortAttribute(pScrn, xvVolume,   0, data);
        ATISetPortAttribute(pScrn, xvMute,   1, data);
        ATISetPortAttribute(pScrn, xvDoubleBuffer,   0, data);
  } else
  if(attribute == xvBrightness) {
        if(value < -1000)value = -1000;
	if(value > 1000)value = 1000;	
	pPriv->brightness = value;
	ATIMach64WaitForFIFO(pATI, 2); 
	outf(SCALER_COLOUR_CNTL, (((pPriv->brightness*64)/1000) & 0x7f) |
				     (((pPriv->saturation*31+31000)/2000) << 8) |
				     (((pPriv->saturation*31+31000)/2000) << 16));
	if(pPriv->bt829!=NULL) xf86_bt829_SetBrightness(pPriv->bt829, pPriv->brightness);	
  } else
  if((attribute == xvSaturation) || (attribute == xvColor)) {
        if(value < -1000)value = -1000;
	if(value > 1000)value = 1000;
	pPriv->saturation = value;
	ATIMach64WaitForFIFO(pATI, 2); 
	outf(SCALER_COLOUR_CNTL, (((pPriv->brightness*64)/1000) & 0x7f) |
				     (((pPriv->saturation*31+31000)/2000) << 8) |
				     (((pPriv->saturation*31+31000)/2000) << 16));
	if(pPriv->bt829!=NULL) xf86_bt829_SetSaturation(pPriv->bt829, pPriv->saturation);	
  } else
  if(attribute == xvContrast) {
        if(value < -1000)value = -1000;
	if(value > 1000)value = 1000;
	pPriv->contrast = value;
	if(pPriv->bt829!=NULL) xf86_bt829_SetContrast(pPriv->bt829, pPriv->contrast);	
  } else
  if(attribute == xvHue) {
        if(value < -1000)value = -1000;
	if(value > 1000)value = 1000;
	pPriv->hue = value;
	if(pPriv->bt829!=NULL) xf86_bt829_SetTint(pPriv->bt829, pPriv->hue);	
  } else
  if(attribute == xvDoubleBuffer) {
	if((value < 0) || (value > 1))
	   return BadValue;
	pPriv->doubleBuffer = value;
  } else
  if(attribute == xvColorKey) {
	pPriv->colorKey = value;
	ATIMach64WaitForFIFO(pATI, 2); 
	outf(OVERLAY_GRAPHICS_KEY_CLR, pPriv->colorKey);

	REGION_EMPTY(pScrn->pScreen, &pPriv->clip);
  } else 
  if(attribute == xvEncoding) {
	pPriv->encoding = value;
	if(pPriv->video_stream_active)
	{
	   if(pPriv->bt829 != NULL) ATI_BT_SetEncoding(pScrn, pPriv);
	   if(pPriv->msp3430 != NULL) ATI_MSP_SetEncoding(pPriv);
           if(pPriv->i2c!=NULL) ATI_board_setmisc(pPriv);
	/* put more here to actually change it */
	}
  } else 
  if(attribute == xvFrequency) {
        pPriv->frequency = value;
  	if(pPriv->fi1236 != NULL) xf86_FI1236_tune(pPriv->fi1236, value);
	if((pPriv->msp3430 != NULL) && (pPriv->msp3430->recheck))
		xf86_InitMSP3430(pPriv->msp3430);
  } else 
  if(attribute == xvMute) {
        pPriv->mute = value;
        ATIMuteAudio(pPriv, pPriv->mute);
        if(pPriv->i2c!=NULL) ATI_board_setmisc(pPriv);
  } else 
  if(attribute == xvVolume) {
  	if(value<0x01) value=0x01;
	if(value>0x7F) value=0x7F;
        pPriv->volume = value;
	pPriv->mute = FALSE;
        if(pPriv->msp3430 != NULL) xf86_MSP3430SetVolume(pPriv->msp3430, value);
        ATIMuteAudio(pPriv, pPriv->mute);
        if(pPriv->i2c!=NULL) ATI_board_setmisc(pPriv);
  } else 
     return BadMatch;

  return Success;
}

static int
ATIGetPortAttribute(
  ScrnInfoPtr pScrn,
  Atom attribute,
  INT32 *value,
  pointer data
){
  ATIPortPrivPtr pPriv = (ATIPortPrivPtr)data;

  if(attribute == xv_autopaint_colorkey) {
  	*value = pPriv->autopaint_colorkey;
  } else
  if(attribute == xvBrightness) {
	*value = pPriv->brightness;
  } else
  if((attribute == xvSaturation) || (attribute == xvColor)) {
	*value = pPriv->saturation;
  } else
  if(attribute == xvContrast) {
	*value = pPriv->contrast;
  } else
  if(attribute == xvHue) {
	*value = pPriv->hue;
  } else
  if(attribute == xvDoubleBuffer) {
	*value = pPriv->doubleBuffer ? 1 : 0;
  } else
  if(attribute == xvColorKey) {
	*value = pPriv->colorKey;
  } else 
  if(attribute == xvEncoding) {
	*value = pPriv->encoding;
  } else 
  if(attribute == xvFrequency) {
        *value = pPriv->frequency;
  } else 
  if(attribute == xvMute) {
        *value = pPriv->mute;
  } else 
  if(attribute == xvVolume) {
        *value = pPriv->volume;
  } else 
    return BadMatch;

  return Success;
}


static void
ATIQueryBestSize(
  ScrnInfoPtr pScrn,
  Bool motion,
  short vid_w, short vid_h,
  short drw_w, short drw_h,
  unsigned int *p_w, unsigned int *p_h,
  pointer data
){
  *p_w = drw_w;
  *p_h = drw_h;
}


static void
ATICopyData(
  unsigned char *src,
  unsigned char *dst,
  int srcPitch,
  int dstPitch,
  int h,
  int w
){
    w <<= 1;
    while(h--) {
	memcpy(dst, src, w);
	src += srcPitch;
	dst += dstPitch;
    }
}

static void
ATICopyMungedData(
   unsigned char *src1,
   unsigned char *src2,
   unsigned char *src3,
   unsigned char *dst1,
   int srcPitch,
   int srcPitch2,
   int dstPitch,
   int h,
   int w
){
   CARD32 *dst;
   CARD8 *s1, *s2, *s3;
   int i, j;

   w >>= 1;

   for(j = 0; j < h; j++) {
	dst = (CARD32*)dst1;
	s1 = src1;  s2 = src2;  s3 = src3;
	i = w;
	while(i > 4) {
	   dst[0] = s1[0] | (s1[1] << 16) | (s3[0] << 8) | (s2[0] << 24);
	   dst[1] = s1[2] | (s1[3] << 16) | (s3[1] << 8) | (s2[1] << 24);
	   dst[2] = s1[4] | (s1[5] << 16) | (s3[2] << 8) | (s2[2] << 24);
	   dst[3] = s1[6] | (s1[7] << 16) | (s3[3] << 8) | (s2[3] << 24);
	   dst += 4; s2 += 4; s3 += 4; s1 += 8;
	   i -= 4;
	}
	while(i--) {
	   dst[0] = s1[0] | (s1[1] << 16) | (s3[0] << 8) | (s2[0] << 24);
	   dst++; s2++; s3++;
	   s1 += 2;
	}

	dst1 += dstPitch;
	src1 += srcPitch;
	if(j & 1) {
	    src2 += srcPitch2;
	    src3 += srcPitch2;
	}
   }
}


static FBLinearPtr
ATIAllocateMemory(
   ScrnInfoPtr pScrn,
   FBLinearPtr linear,
   int size
){
   ScreenPtr pScreen;
   FBLinearPtr new_linear;

   if(linear) {
	if(linear->size >= size)
	   return linear;

	if(xf86ResizeOffscreenLinear(linear, size))
	   return linear;

	xf86FreeOffscreenLinear(linear);
   }

   pScreen = screenInfo.screens[pScrn->scrnIndex];

   new_linear = xf86AllocateOffscreenLinear(pScreen, size, 16,
						NULL, NULL, NULL);

   if(!new_linear) {
	int max_size;

	xf86QueryLargestOffscreenLinear(pScreen, &max_size, 16,
						PRIORITY_EXTREME);

	if(max_size < size)
	   return NULL;

	xf86PurgeUnlockedOffscreenAreas(pScreen);
	new_linear = xf86AllocateOffscreenLinear(pScreen, size, 16,
						NULL, NULL, NULL);
   }

   return new_linear;
}

/* Note - unlike R128 versions the pitch is in pixels */

static void
ATIDisplayVideo(
    ScrnInfoPtr pScrn,
    ATIPortPrivPtr pPriv,
    int id,
    int offset1,
    int offset2,
    short width, short height,
    int pitch,
    int left, int right, int top,
    BoxPtr dstBox,
    short src_w, short src_h,
    short drw_w, short drw_h
){
    ATIPtr pATI = ATIPTR(pScrn);
    int v_inc, h_inc, tmp;
    int p1_h_accum_init, p23_h_accum_init;
    int p1_v_accum_init;
    int start;
    CARD32 video_format;

    /* Choose ecp_div setting again. We need this as it is possible
      the user has switched resolutions thus requiring a new ecp_div
      setting */
    pPriv->ecp_div = (pATI->NewHW.pll_vclk_cntl & PLL_ECP_DIV) >> 4;

    v_inc = ((src_h) << (12
		+((pScrn->currentMode->Flags & V_INTERLACE)?1:0)
		-((pScrn->currentMode->Flags & V_DBLSCAN)?1:0))) / drw_h;
    h_inc = ((src_w << (12
    		+pPriv->ecp_div)) / drw_w);

    /* keep everything in 16.16 */

/*    offset += ((left >> 16) & ~7) << 1; */

    tmp = (left & 0x0003ffff) + 0x00028000 + (h_inc << 3);
    p1_h_accum_init = ((tmp <<  4) & 0x000f8000) |
		      ((tmp << 12) & 0xf0000000);

    tmp = ((left >> 1) & 0x0001ffff) + 0x00028000 + (h_inc << 2);
    p23_h_accum_init = ((tmp <<  4) & 0x000f8000) |
		       ((tmp << 12) & 0x70000000);

    tmp = (top & 0x0000ffff) + 0x00018000;
    p1_v_accum_init = ((tmp << 4) & 0x03ff8000) | 0x00000001;

    left = (left >> 16) & 7;

    start = (left *pitch)/width + top * pitch;
    start = 0;

    ATIMach64WaitForFIFO(pATI, 13); 
    outf(OVERLAY_Y_X_START, (dstBox->y1*((pScrn->currentMode->Flags & V_DBLSCAN)?2:1)) | (dstBox->x1 << 16));
    outf(OVERLAY_Y_X_END,   (dstBox->y2*((pScrn->currentMode->Flags & V_DBLSCAN)?2:1)) | (dstBox->x2 << 16));
    outf(OVERLAY_SCALE_INC, (h_inc << 16) | (v_inc));
    outf(SCALER_BUF_PITCH, pitch);

    outf(SCALER_HEIGHT_WIDTH, ((src_w - left)<<16) | (src_h - top));

    outf(SCALER_BUF0_OFFSET, offset1 + start);
    outf(SCALER_BUF0_OFFSET_U, offset1 + start);
    outf(SCALER_BUF0_OFFSET_V, offset1 + start);

    outf(SCALER_BUF1_OFFSET, offset2 + start);
    outf(SCALER_BUF1_OFFSET_U, offset2 + start);
    outf(SCALER_BUF1_OFFSET_V, offset2 + start);

    outf(OVERLAY_SCALE_CNTL, 0x4000001 | (3 << 30));

    ATIMach64WaitForIdle(pATI);
    video_format=inm(VIDEO_FORMAT);
    if(id == FOURCC_UYVY)
       outf(VIDEO_FORMAT, (video_format & ~ 0xF000) | (0xB000));
    else
       outf(VIDEO_FORMAT, (video_format & ~ 0xF000) | (0xC000));
}


static int
ATIPutImage(
  ScrnInfoPtr pScrn,
  short src_x, short src_y,
  short drw_x, short drw_y,
  short src_w, short src_h,
  short drw_w, short drw_h,
  int id, unsigned char* buf,
  short width, short height,
  Bool Sync,
  RegionPtr clipBoxes, pointer data
){
   ATIPtr pATI = ATIPTR(pScrn);
   ATIPortPrivPtr pPriv = (ATIPortPrivPtr)data;
   INT32 xa, xb, ya, yb;
   unsigned char *dst_start;
   int pitch, new_size, offset, s2offset, s3offset;
   int srcPitch, srcPitch2, dstPitch;
   int top, left, npixels, nlines, bpp;
   BoxRec dstBox;
   CARD32 tmp;

   /*
    * s2offset, s3offset - byte offsets into U and V plane of the
    *                      source where copying starts.  Y plane is
    *                      done by editing "buf".
    *
    * offset - byte offset to the first line of the destination.
    *
    * dst_start - byte address to the first displayed pel.
    *
    */

   /* make the compiler happy */
   s2offset = s3offset = srcPitch2 = 0;

   if(src_w > (drw_w << 4))
	drw_w = src_w >> 4;
   if(src_h > (drw_h << 4))
	drw_h = src_h >> 4;

   /* Clip */
   xa = src_x;
   xb = src_x + src_w;
   ya = src_y;
   yb = src_y + src_h;

   dstBox.x1 = drw_x;
   dstBox.x2 = drw_x + drw_w;
   dstBox.y1 = drw_y;
   dstBox.y2 = drw_y + drw_h;

   if(!ATIClipVideo(&dstBox, &xa, &xb, &ya, &yb, clipBoxes, width, height))
	return Success;

   dstBox.x1 -= pScrn->frameX0;
   dstBox.x2 -= pScrn->frameX0;
   dstBox.y1 -= pScrn->frameY0;
   dstBox.y2 -= pScrn->frameY0;

   bpp = pScrn->bitsPerPixel >> 3;
   pitch = bpp * pScrn->displayWidth;

/* Note: the use of dstPitch in Display Video relies on the fact
   that all current formats are copied to a format with sizeof(pixel)=2 
   If something new appears make a new variable for pixel_pitch */

   switch(id) {
   case FOURCC_YV12:
   case FOURCC_I420:
	dstPitch = ((width << 1) + 15) & ~15;
	new_size = ((dstPitch * height) + bpp - 1) / bpp;
	srcPitch = (width + 3) & ~3;
	s2offset = srcPitch * height;
	srcPitch2 = ((width >> 1) + 3) & ~3;
	s3offset = (srcPitch2 * (height >> 1)) + s2offset;
	break;
   case FOURCC_UYVY:
   case FOURCC_YUY2:
   default:
	dstPitch = ((width << 1) + 15) & ~15;
	new_size = ((dstPitch * height) + bpp - 1) / bpp;
	srcPitch = (width << 1);
	break;
   }

   if(!(pPriv->linear = ATIAllocateMemory(pScrn, pPriv->linear,
		pPriv->doubleBuffer ? (new_size << 1) : new_size)))
   {
	return BadAlloc;
   }

   pPriv->currentBuffer ^= 1;

    /* copy data */
   top = ya >> 16;
   left = (xa >> 16) & ~1;
   npixels = ((((xb + 0xffff) >> 16) + 1) & ~1) - left;

   offset = (pPriv->linear->offset * bpp) + (top * dstPitch);
   if(pPriv->doubleBuffer)
	offset += pPriv->currentBuffer * new_size * bpp;
   dst_start = (CARD8 *)pATI->pMemory + offset;

   switch(id) {
    case FOURCC_YV12:
    case FOURCC_I420:
	top &= ~1;
	dst_start += left << 1;
	tmp = ((top >> 1) * srcPitch2) + (left >> 1);
	s2offset += tmp;
	s3offset += tmp;
	if(id == FOURCC_I420) {
	   tmp = s2offset;
	   s2offset = s3offset;
	   s3offset = tmp;
	}
	nlines = ((((yb + 0xffff) >> 16) + 1) & ~1) - top;
	(*pATI->pXAAInfo->Sync)(pScrn);
	ATICopyMungedData(buf + (top * srcPitch) + left, buf + s2offset,
			   buf + s3offset, dst_start, srcPitch, srcPitch2,
			   dstPitch, nlines, npixels);
	break;
    case FOURCC_UYVY:
    case FOURCC_YUY2:
    default:
	left <<= 1;
	buf += (top * srcPitch) + left;
	nlines = ((yb + 0xffff) >> 16) - top;
	dst_start += left;
	(*pATI->pXAAInfo->Sync)(pScrn);
	ATICopyData(buf, dst_start, srcPitch, dstPitch, nlines, npixels);
	break;
    }


    /* update cliplist */
    if(!RegionsEqual(&pPriv->clip, clipBoxes)) {
	REGION_COPY(pScreen, &pPriv->clip, clipBoxes);
	/* draw these */
	if(pPriv->autopaint_colorkey)(*pATI->pXAAInfo->FillSolidRects)(pScrn, pPriv->colorKey, GXcopy, ~0,
					REGION_NUM_RECTS(clipBoxes),
					REGION_RECTS(clipBoxes));
    }

    ATIDisplayVideo(pScrn, pPriv, id, offset, offset, width, height, dstPitch/2,
		     xa, xb, ya, &dstBox, src_w, src_h, drw_w, drw_h);

    pPriv->videoStatus = CLIENT_VIDEO_ON;

    pATI->VideoTimerCallback = ATIVideoTimerCallback;

    return Success;
}


static int
ATIQueryImageAttributes(
    ScrnInfoPtr pScrn,
    int id,
    unsigned short *w, unsigned short *h,
    int *pitches, int *offsets
){
    int size, tmp;

    if(*w > 2048) *w = 2048;
    if(*h > 2048) *h = 2048;

    *w = (*w + 1) & ~1;
    if(offsets) offsets[0] = 0;

    switch(id) {
    case FOURCC_YV12:
    case FOURCC_I420:
	*h = (*h + 1) & ~1;
	size = (*w + 3) & ~3;
	if(pitches) pitches[0] = size;
	size *= *h;
	if(offsets) offsets[1] = size;
	tmp = ((*w >> 1) + 3) & ~3;
	if(pitches) pitches[1] = pitches[2] = tmp;
	tmp *= (*h >> 1);
	size += tmp;
	if(offsets) offsets[2] = size;
	size += tmp;
	break;
    case FOURCC_UYVY:
    case FOURCC_YUY2:
    default:
	size = *w << 1;
	if(pitches) pitches[0] = size;
	size *= *h;
	break;
    }

    return size;
}

/* this routine is here because different mach64 boards use
   different methods of muting. And because sometimes there is
   no good way to autodetect this (besides knowing which board
   we have) */



void ATI_board_setmisc(ATIPortPrivPtr pPriv)
{
    CARD8 a;
    CARD32 tmp;
    I2CDevRec d;
    ScrnInfoPtr pScrn = xf86Screens[pPriv->i2c->scrnIndex];
    ATIPtr pATI = ATIPTR(pScrn);

    d.DevName = "temporary";
    d.pI2CBus = pPriv->i2c;
    d.NextDev = NULL;
    d.StartTimeout = pPriv->i2c->StartTimeout;
    d.BitTimeout = pPriv->i2c->BitTimeout;
    d.AcknTimeout = pPriv->i2c->AcknTimeout;
    d.ByteTimeout = pPriv->i2c->ByteTimeout;

    if((pPriv->MM_TABLE_valid) && (pATI->Chip == ATI_CHIP_264GTDVD))
    {
       a = 0x2F;
       if(!pPriv->mute && pPriv->video_stream_active)a |= 0x10;   
       if(((pPriv->encoding-1) % 3) != 1) a |= 0x40; /* this is a fancy way
                                                    to choose all encodings
						    except tuner ones */ 
       d.SlaveAddr = 0x76; /* PCF8574 I/O expander */
       I2C_WriteRead(&d, &a, 1, NULL, 0);
       
    }

    if(pPriv->addon_board)
    {
       a = 0x2F;
       if(!pPriv->mute && pPriv->video_stream_active) a |= 0x50;
       if(((pPriv->encoding-1) % 3) != 1) a |= 0x40; 
       d.SlaveAddr = pPriv->board_control;
       I2C_WriteRead(&d, &a, 1, NULL, 0);    
    }

    if(pPriv->tda8425 != NULL)
    {
       pPriv->tda8425->v_left = (pPriv->mute ? 0xc0 : 0xff);
       pPriv->tda8425->v_right = (pPriv->mute ? 0xc0 : 0xff);
       if(((pPriv->encoding-1) % 3) != 1) pPriv->tda8425->mux = 0;
                         else pPriv->tda8425->mux = 1; /* this is a fancy way
                                                    to choose all encodings
						    except tuner ones */ 
       xf86_tda8425_setaudio(pPriv->tda8425);
    }
   
   /* setup EXT_DAC_REGS for RagePro All-in-Wonders */ 
   if((pATI->Chip == ATI_CHIP_264GTPRO) && (pPriv->MM_TABLE_valid))
   {
      tmp = inr(EXT_DAC_REGS) & ~0x3;
      if(pPriv->mute || !pPriv->video_stream_active) tmp |= 0x02;
      if(((pPriv->encoding-1) % 3) == 1) tmp |= 0x01;
      outr(EXT_DAC_REGS, tmp);             
   }

    /* Adjust PAL/SECAM constants for FI1216MF tuner */
    if((((pPriv->board_info & 0xf)==5) ||
       ((pPriv->board_info & 0xf)==11) ||
       ((pPriv->board_info & 0xf)==14))&& (pPriv->fi1236!=NULL))
    {
        if((pPriv->encoding>=1)&&(pPriv->encoding<=3)) /*PAL*/
	{
    	   pPriv->fi1236->parm.band_low = 0xA1;
	   pPriv->fi1236->parm.band_mid = 0x91;
	   pPriv->fi1236->parm.band_high = 0x31;
	}
        if((pPriv->encoding>=7)&&(pPriv->encoding<=9)) /*SECAM-L*/
	{
    	   pPriv->fi1236->parm.band_low = 0xA3;
	   pPriv->fi1236->parm.band_mid = 0x93;
	   pPriv->fi1236->parm.band_high = 0x33;
	}
    }
}

void ATI_MSP_SetEncoding(ATIPortPrivPtr pPriv)
{
xf86_MSP3430SetVolume(pPriv->msp3430, MSP3430_FAST_MUTE);
switch(pPriv->encoding){
	case 1:
		pPriv->msp3430->standard = MSP3430_PAL;
		pPriv->msp3430->connector = MSP3430_CONNECTOR_3;
		break;
	case 2:
		pPriv->msp3430->standard = MSP3430_PAL;
		pPriv->msp3430->connector = MSP3430_CONNECTOR_1;
		break;
	case 3:
		pPriv->msp3430->standard = MSP3430_PAL;
		pPriv->msp3430->connector = MSP3430_CONNECTOR_2;
		break;
	case 4:
		pPriv->msp3430->standard = MSP3430_NTSC;
		pPriv->msp3430->connector = MSP3430_CONNECTOR_3;
		break;
	case 5:
		pPriv->msp3430->standard = MSP3430_NTSC;
		pPriv->msp3430->connector = MSP3430_CONNECTOR_1;
		break;
	case 6:
		pPriv->msp3430->standard = MSP3430_NTSC;
		pPriv->msp3430->connector = MSP3430_CONNECTOR_2;
		break;
	case 7:
		pPriv->msp3430->standard = MSP3430_SECAM;
		pPriv->msp3430->connector = MSP3430_CONNECTOR_3;
		break;
	case 8:
		pPriv->msp3430->standard = MSP3430_SECAM;
		pPriv->msp3430->connector = MSP3430_CONNECTOR_1;
		break;
	case 9:
		pPriv->msp3430->standard = MSP3430_SECAM;
		pPriv->msp3430->connector = MSP3430_CONNECTOR_2;
		break;
	case 10:
		pPriv->msp3430->standard = MSP3430_SECAM;
		pPriv->msp3430->connector = MSP3430_CONNECTOR_3;
		break;
	case 11:
		pPriv->msp3430->standard = MSP3430_SECAM;
		pPriv->msp3430->connector = MSP3430_CONNECTOR_1;
		break;
	case 12:
		pPriv->msp3430->standard = MSP3430_SECAM;
		pPriv->msp3430->connector = MSP3430_CONNECTOR_2;
		break;
	default:
		return;
	}	
xf86_InitMSP3430(pPriv->msp3430);
xf86_MSP3430SetVolume(pPriv->msp3430, pPriv->mute ? MSP3430_FAST_MUTE : pPriv->volume);
}

void ATI_BT_SetEncoding(ScrnInfoPtr pScrn, ATIPortPrivPtr pPriv)
{
ATIPtr pATI = ATIPTR(pScrn);
int width, height;
switch(pPriv->encoding){
	case 1:
                xf86_bt829_SetMux(pPriv->bt829, BT829_MUX2);
		pPriv->v=24;
	        xf86_bt829_SetFormat(pPriv->bt829, BT829_PAL);
		break;
	case 2:
                xf86_bt829_SetMux(pPriv->bt829, BT829_MUX0);
		pPriv->v=24;
	        xf86_bt829_SetFormat(pPriv->bt829, BT829_PAL);
		break;
	case 3:
                xf86_bt829_SetMux(pPriv->bt829, BT829_MUX1);
		pPriv->v=24;
	        xf86_bt829_SetFormat(pPriv->bt829, BT829_PAL);
		break;
	case 4:
                xf86_bt829_SetMux(pPriv->bt829, BT829_MUX2);
		pPriv->v=23;
	        xf86_bt829_SetFormat(pPriv->bt829, BT829_NTSC);
		break;
	case 5:
                xf86_bt829_SetMux(pPriv->bt829, BT829_MUX0);
		pPriv->v=23;
	        xf86_bt829_SetFormat(pPriv->bt829, BT829_NTSC);
		break;
	case 6:
                xf86_bt829_SetMux(pPriv->bt829, BT829_MUX1);
		pPriv->v=23;
	        xf86_bt829_SetFormat(pPriv->bt829, BT829_NTSC);
		break;
	case 7:
                xf86_bt829_SetMux(pPriv->bt829, BT829_MUX2);
		pPriv->v=25;
	        xf86_bt829_SetFormat(pPriv->bt829, BT829_SECAM);
		break;
	case 8:
                xf86_bt829_SetMux(pPriv->bt829, BT829_MUX0);
		pPriv->v=25;
	        xf86_bt829_SetFormat(pPriv->bt829, BT829_SECAM);
		break;
	case 9:
                xf86_bt829_SetMux(pPriv->bt829, BT829_MUX1);
		pPriv->v=25;
	        xf86_bt829_SetFormat(pPriv->bt829, BT829_SECAM);
		break;
	default:
                xf86_bt829_SetMux(pPriv->bt829, BT829_MUX2);
		pPriv->v=23;
	        xf86_bt829_SetFormat(pPriv->bt829, BT829_NTSC);
		return;
	}	
if(pATI->Chip>=ATI_CHIP_264GTPRO){
	      width = RagePro_InputVideoEncodings[pPriv->encoding].width;
      	      height = RagePro_InputVideoEncodings[pPriv->encoding].height; 
	      } else {
	      width = VT_GT_InputVideoEncodings[pPriv->encoding].width;
      	      height = VT_GT_InputVideoEncodings[pPriv->encoding].height; 
	      }
xf86_bt829_SetCaptSize(pPriv->bt829, width, height*2);
if(pPriv->tda9850!=NULL){
	pPriv->tda9850->mux = pPriv->bt829->mux;
	xf86_tda9850_setaudio(pPriv->tda9850);
	}
}

/* capture config constants */
#define BUF_TYPE_FIELD		0
#define BUF_TYPE_ALTERNATING	1
#define BUF_TYPE_FRAME		2


#define BUF_MODE_SINGLE		0
#define BUF_MODE_DOUBLE		1
#define BUF_MODE_TRIPLE		2
/* CAP0_CONFIG values */

/*
#define ENABLE_ATI_CAPTURE_WEAVE (0x1C000005L | (BUF_MODE_SINGLE <<7) | (BUF_TYPE_FRAME << 4) |  (0) | (1<<29))
#define ENABLE_ATI_CAPTURE_BOB (0x1C000005L | (BUF_MODE_SINGLE <<7) | (BUF_TYPE_ALTERNATING << 4) | (0) | (0<<15) | (1<<29))
*/
#define ENABLE_ATI_CAPTURE_DOUBLEBUF 0x10000015



static int
ATIPutVideo(
  ScrnInfoPtr pScrn,
  short src_x, short src_y,
  short drw_x, short drw_y,
  short src_w, short src_h,
  short drw_w, short drw_h,
  RegionPtr clipBoxes, pointer data
){
   ATIPtr pATI = ATIPTR(pScrn);
   ATIPortPrivPtr pPriv = (ATIPortPrivPtr)data;
   INT32 xa, xb, ya, yb, top;
   int pitch, new_size, offset1, offset2, s2offset, s3offset;
   int srcPitch, srcPitch2, dstPitch;
   int bpp;
   BoxRec dstBox;
   CARD32 id;
   int width, height;

   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "PutVideo\n");
   /*
    * s2offset, s3offset - byte offsets into U and V plane of the
    *                      source where copying starts.  Y plane is
    *                      done by editing "buf".
    *
    * offset - byte offset to the first line of the destination.
    *
    * dst_start - byte address to the first displayed pel.
    *
    */

   /* make the compiler happy */
   s2offset = s3offset = srcPitch2 = 0;

   if(src_w > (drw_w << 4))
	drw_w = src_w >> 4;
   if(src_h > (drw_h << 4))
	drw_h = src_h >> 4;

   /* Clip */
   xa = src_x;
   xb = src_x + src_w;
   ya = src_y;
   yb = src_y + src_h;

   dstBox.x1 = drw_x;
   dstBox.x2 = drw_x + drw_w;
   dstBox.y1 = drw_y;
   dstBox.y2 = drw_y + drw_h;

   if(pPriv->bt829 != NULL)
   {
      if(pATI->Chip>=ATI_CHIP_264GTPRO){
	      width = RagePro_InputVideoEncodings[pPriv->encoding].width;
      	      height = RagePro_InputVideoEncodings[pPriv->encoding].height; 
	      } else {
	      width = VT_GT_InputVideoEncodings[pPriv->encoding].width;
      	      height = VT_GT_InputVideoEncodings[pPriv->encoding].height; 
	      }
   } else 
      return FALSE;
        
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "PutVideo %d %d\n", width,height);

   if(!ATIClipVideo(&dstBox, &xa, &xb, &ya, &yb, clipBoxes, width, height))
	return Success;

   dstBox.x1 -= pScrn->frameX0;
   dstBox.x2 -= pScrn->frameX0;
   dstBox.y1 -= pScrn->frameY0;
   dstBox.y2 -= pScrn->frameY0;

   bpp = pScrn->bitsPerPixel >> 3;
   pitch = bpp * pScrn->displayWidth;

   id = FOURCC_YUY2;
   
   top = ya>>16;

   switch(id) {
   case FOURCC_YV12:
   case FOURCC_I420:
   	top &= ~1;
	dstPitch = ((width << 1) + 15) & ~15;
	new_size = ((dstPitch * height) + bpp - 1) / bpp;
	srcPitch = (width + 3) & ~3;
	s2offset = srcPitch * height;
	srcPitch2 = ((width >> 1) + 3) & ~3;
	s3offset = (srcPitch2 * (height >> 1)) + s2offset;
	break;
   case FOURCC_UYVY:
   case FOURCC_YUY2:
   default:
	dstPitch = ((width << 1) + 15) & ~15;
	new_size = ((dstPitch * height) + bpp - 1) / bpp;
	srcPitch = (width << 1);
	break;
   }

   new_size += 0x1f;  /* for aligning */

   if(!(pPriv->linear = ATIAllocateMemory(pScrn, pPriv->linear, new_size*2)))
   {
	return BadAlloc;
   }


   offset1 = (pPriv->linear->offset*bpp+0xf) & (~0xf);
   offset2 = ((pPriv->linear->offset+new_size)*bpp + 0x1f) & (~0xf);
   
   ATIMach64WaitForFIFO(pATI, 9); 
   outf(CAPTURE_BUF0_OFFSET, offset1);
/*   outf(BUF0_CAP_ODD_OFFSET, 0); */
   outf(ONESHOT_BUF_OFFSET, offset1);
   outf(CAPTURE_BUF1_OFFSET, offset2);
/*   outf(BUF1_CAP_ODD_OFFSET, 0); */
   
   outf(CAPTURE_X_WIDTH, (width*2)<<16);
   outf(CAPTURE_START_END, (((height)+pPriv->v-1)<<16)|(pPriv->v));
   outf(CAPTURE_CONFIG, ENABLE_ATI_CAPTURE_DOUBLEBUF);
   outf(CAPTURE_DEBUG, 0);
   
   outf(OVERLAY_TEST, 0);
   
   /* activate capture unit */
   outf(TRIG_CNTL, (1<<31) | 1);
   
   if(! pPriv->video_stream_active){
      
   if(pPriv->bt829 != NULL) 
   {
      ATI_BT_SetEncoding(pScrn, pPriv);
      xf86_bt829_SetCaptSize(pPriv->bt829, width, height*2);
   }
   
   if(pPriv->i2c!=NULL) ATI_board_setmisc(pPriv);

   if(pPriv->msp3430 != NULL) ATI_MSP_SetEncoding(pPriv);
   ATIMuteAudio(pPriv, pPriv->mute);
   }

    /* update cliplist */
    if(!RegionsEqual(&pPriv->clip, clipBoxes)) {
	REGION_COPY(pScreen, &pPriv->clip, clipBoxes);
	/* draw these */
	if(pPriv->autopaint_colorkey)(*pATI->pXAAInfo->FillSolidRects)(pScrn, pPriv->colorKey, GXcopy, ~0,
					REGION_NUM_RECTS(clipBoxes),
					REGION_RECTS(clipBoxes));
    }

    pPriv->doubleBuffer = 1;
    ATIDisplayVideo(pScrn, pPriv, id, offset1 +top*srcPitch, offset2+top*srcPitch, width, height, width,
		     xa, xb, ya, &dstBox, src_w, src_h, drw_w, drw_h);

    pPriv->videoStatus = CLIENT_VIDEO_ON;
    pPriv->video_stream_active = TRUE;

    pATI->VideoTimerCallback = ATIVideoTimerCallback;

    return Success;
}

static void
ATIVideoTimerCallback(ScrnInfoPtr pScrn, Time time)
{
    ATIPtr pATI = ATIPTR(pScrn);
    ATIPortPrivPtr pPriv = pATI->adaptor->pPortPrivates[0].ptr;

    if(pPriv->videoStatus & TIMER_MASK) {
	if(pPriv->videoStatus & OFF_TIMER) {
	    if(pPriv->offTime < time) {
		ATIMach64WaitForFIFO(pATI, 2); 
		outf(OVERLAY_SCALE_CNTL, 0x80000000);
		pPriv->videoStatus = FREE_TIMER;
		pPriv->freeTime = time + FREE_DELAY;
	    }
	} else {  /* FREE_TIMER */
	    if(pPriv->freeTime < time) {
		if(pPriv->linear) {
		   xf86FreeOffscreenLinear(pPriv->linear);
		   pPriv->linear = NULL;
		}
		pPriv->videoStatus = 0;
		pATI->VideoTimerCallback = NULL;
	    }
	}
    } else  /* shouldn't get here */
	pATI->VideoTimerCallback = NULL;
}


#endif  /* !XvExtension */
