/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/ati/radeon_video.c,v 1.12 2001/07/19 02:22:50 tsi Exp $ */

#include "radeon.h"
#include "radeon_reg.h"

#include "xf86.h"
#include "dixstruct.h"

#include "Xv.h"
#include "fourcc.h"


#include "xf86i2c.h"
#include "fi1236.h"
#include "msp3430.h"
#include "generic_bus.h"
#include "theatre_reg.h"
#include "theatre.h"
#include "i2c_def.h"

#define OFF_DELAY       250  /* milliseconds */
#define FREE_DELAY      15000

#define OFF_TIMER       0x01
#define FREE_TIMER      0x02
#define CLIENT_VIDEO_ON 0x04

#define TIMER_MASK      (OFF_TIMER | FREE_TIMER)



#ifndef XvExtension
void RADEONInitVideo(ScreenPtr pScreen) {}
#else


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
   CARD32	 transform_index;
   int           brightness;
   int           saturation;
   int           hue;
   int           contrast;

   int           dec_brightness;
   int           dec_saturation;
   int           dec_hue;
   int           dec_contrast;

   Bool          doubleBuffer;
   unsigned char currentBuffer;
   FBLinearPtr   linear;
   RegionRec     clip;
   CARD32        colorKey;
   CARD32        videoStatus;
   Time          offTime;
   Time          freeTime;
   
   I2CBusPtr	 i2c;
   CARD32 	 radeon_i2c_timing;
   CARD32        radeon_M;
   CARD32        radeon_N;
   
   FI1236Ptr     fi1236;
   MSP3430Ptr    msp3430;
   
   GENERIC_BUS_Ptr  VIP;
   TheatrePtr       theatre;
   
   Bool          video_stream_active;
   int           encoding;
   CARD32        frequency;
   int           volume;
   Bool 	 mute;
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
} RADEONPortPrivRec, *RADEONPortPrivPtr;

static XF86VideoAdaptorPtr RADEONSetupImageVideo(ScreenPtr);
static int  RADEONSetPortAttribute(ScrnInfoPtr, Atom, INT32, pointer);
static int  RADEONGetPortAttribute(ScrnInfoPtr, Atom ,INT32 *, pointer);
static void RADEONStopVideo(ScrnInfoPtr, pointer, Bool);
static void RADEONQueryBestSize(ScrnInfoPtr, Bool, short, short, short, short,
			unsigned int *, unsigned int *, pointer);
static int  RADEONPutImage(ScrnInfoPtr, short, short, short, short, short,
			short, short, short, int, unsigned char*, short,
			short, Bool, RegionPtr, pointer);
static int RADEONPutVideo(ScrnInfoPtr pScrn, short src_x, short src_y, short drw_x, short drw_y,
                        short src_w, short src_h, short drw_w, short drw_h, 
			RegionPtr clipBoxes, pointer data);
static int  RADEONQueryImageAttributes(ScrnInfoPtr, int, unsigned short *,
			unsigned short *,  int *, int *);

void RADEON_RT_SetEncoding(RADEONPortPrivPtr);
void RADEON_board_setmisc(RADEONPortPrivPtr pPriv);
void RADEON_MSP_SetEncoding(RADEONPortPrivPtr pPriv);
void RADEONSetColorKey(ScrnInfoPtr pScrn, CARD32 pixel);
static void RADEONResetI2C(ScrnInfoPtr pScrn, RADEONPortPrivPtr pPriv);
void RADEONShutdownVideo(ScrnInfoPtr pScrn);
void RADEONVIP_reset(ScrnInfoPtr pScrn, RADEONPortPrivPtr pPriv);


static void RADEONVideoTimerCallback(ScrnInfoPtr pScrn, Time now);


#define MAKE_ATOM(a) MakeAtom(a, sizeof(a) - 1, TRUE)

static Atom xvBrightness, xvColorKey, xvSaturation, xvDoubleBuffer, 
             xvEncoding, xvVolume, xvMute, xvFrequency, xvContrast, xvHue, xvColor,
	     xv_autopaint_colorkey, xv_set_defaults,
	     xvDecBrightness, xvDecContrast, xvDecHue, xvDecColor, xvDecSaturation;



void RADEONInitVideo(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    RADEONInfoPtr info  = RADEONPTR(pScrn);
    XF86VideoAdaptorPtr *adaptors, *newAdaptors = NULL;
    XF86VideoAdaptorPtr newAdaptor = NULL;
    int num_adaptors;

    if(info->accel && info->accel->FillSolidRects)
	newAdaptor = RADEONSetupImageVideo(pScreen);

    num_adaptors = xf86XVListGenericAdaptors(pScrn, &adaptors);

    if(newAdaptor) {
	if(!num_adaptors) {
	    num_adaptors = 1;
	    adaptors = &newAdaptor;
	} else {
	    newAdaptors =  /* need to free this someplace */
		xalloc((num_adaptors + 1) * sizeof(XF86VideoAdaptorPtr*));
	    if(newAdaptors) {
		memcpy(newAdaptors, adaptors, num_adaptors *
					sizeof(XF86VideoAdaptorPtr));
		newAdaptors[num_adaptors] = newAdaptor;
		adaptors = newAdaptors;
		num_adaptors++;
	    }
	}
    }

    if(num_adaptors)
	xf86XVScreenInit(pScreen, adaptors, num_adaptors);

    if(newAdaptors)
	xfree(newAdaptors);
}

/* client libraries expect an encoding */
static XF86VideoEncodingRec DummyEncoding =
{
   0,
   "XV_IMAGE",
   2048, 2048,
   {1, 1}
};

 /* the picture is interlaced - hence the half-heights */

static XF86VideoEncodingRec
InputVideoEncodings[] =
{
    { 0, "XV_IMAGE",			2048,2048,{1,1}},        
    { 1, "pal-composite",		720, 240, { 1, 50 }},
    { 2, "pal-tuner",			720, 240, { 1, 50 }},
    { 3, "pal-svideo",			720, 240, { 1, 50 }},
    { 4, "ntsc-composite",		640, 240, { 1001, 60000 }},
    { 5, "ntsc-tuner",			640, 240, { 1001, 60000 }},
    { 6, "ntsc-svideo",			640, 240, { 1001, 60000 }},
    { 7, "secam-composite",		720, 240, { 1, 50 }},
    { 8, "secam-tuner",			720, 240, { 1, 50 }},
    { 9, "secam-svideo",		720, 240, { 1, 50 }},
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


#define NUM_DEC_ATTRIBUTES 17
#define NUM_ATTRIBUTES 9

static XF86AttributeRec Attributes[NUM_DEC_ATTRIBUTES+1] =
{
   {XvSettable             , 0, 1, "XV_SET_DEFAULTS"},
   {XvSettable | XvGettable, 0, 1, "XV_AUTOPAINT_COLORKEY"},
   {XvSettable | XvGettable, 0, ~0, "XV_COLORKEY"},
   {XvSettable | XvGettable, 0, 1, "XV_DOUBLE_BUFFER"},
   {XvSettable | XvGettable, -1000, 1000, "XV_BRIGHTNESS"},
   {XvSettable | XvGettable, -1000, 1000, "XV_CONTRAST"},
   {XvSettable | XvGettable, -1000, 1000, "XV_SATURATION"},
   {XvSettable | XvGettable, -1000, 1000, "XV_COLOR"},
   {XvSettable | XvGettable, -1000, 1000, "XV_HUE"},
   {XvSettable | XvGettable, -1000, 1000, "XV_DEC_BRIGHTNESS"},
   {XvSettable | XvGettable, -1000, 1000, "XV_DEC_SATURATION"},
   {XvSettable | XvGettable, -1000, 1000, "XV_DEC_CONTRAST"},
   {XvSettable | XvGettable, -1000, 1000, "XV_DEC_HUE"},
   {XvSettable | XvGettable, 0, 12, "XV_ENCODING"},
   {XvSettable | XvGettable, 0, -1, "XV_FREQ"},
   {XvSettable | XvGettable, 0x01, 0x7F, "XV_VOLUME"},
   {XvSettable | XvGettable, 0, 1, "XV_MUTE"},
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

void RADEONLeaveVT_Video(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr   info      = RADEONPTR(pScrn);
    RADEONPortPrivPtr pPriv;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Shutting down Xvideo subsystems\n");
    if(info->adaptor==NULL)return;
    pPriv = info->adaptor->pPortPrivates[0].ptr;
    if(pPriv==NULL)return;
    if(pPriv->theatre!=NULL){
    	xf86_ShutdownTheatre(pPriv->theatre);
	}
    RADEONResetVideo(pScrn);
}

void RADEONEnterVT_Video(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr   info      = RADEONPTR(pScrn);
    RADEONPortPrivPtr pPriv;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Starting up Xvideo subsystems\n");
    if(info->adaptor==NULL)return;
    pPriv = info->adaptor->pPortPrivates[0].ptr;
    if(pPriv==NULL)return;
    RADEONResetVideo(pScrn);
}

/* Reference color space transform data */
typedef struct tagREF_TRANSFORM
{
	float RefLuma;
	float RefRCb;
	float RefRCr;
	float RefGCb;
	float RefGCr;
	float RefBCb;
	float RefBCr;
} REF_TRANSFORM;

/* Parameters for ITU-R BT.601 and ITU-R BT.709 colour spaces */
REF_TRANSFORM trans[2] =
{
	{1.1678, 0.0, 1.6007, -0.3929, -0.8154, 2.0232, 0.0}, /* BT.601 */
	{1.1678, 0.0, 1.7980, -0.2139, -0.5345, 2.1186, 0.0}  /* BT.709 */
};


/* Gamma curve definition */
typedef struct 
{
	unsigned int gammaReg;
	unsigned int gammaSlope;
	unsigned int gammaOffset;
}GAMMA_SETTINGS;

/* Recommended gamma curve parameters */
GAMMA_SETTINGS def_gamma[18] = 
{
	{RADEON_OV0_GAMMA_0_F, 0x100, 0x0000},
	{RADEON_OV0_GAMMA_10_1F, 0x100, 0x0020},
	{RADEON_OV0_GAMMA_20_3F, 0x100, 0x0040},
	{RADEON_OV0_GAMMA_40_7F, 0x100, 0x0080},
	{RADEON_OV0_GAMMA_80_BF, 0x100, 0x0100},
	{RADEON_OV0_GAMMA_C0_FF, 0x100, 0x0100},
        {RADEON_OV0_GAMMA_100_13F, 0x100, 0x0200},
	{RADEON_OV0_GAMMA_140_17F, 0x100, 0x0200},
	{RADEON_OV0_GAMMA_180_1BF, 0x100, 0x0300},
	{RADEON_OV0_GAMMA_1C0_1FF, 0x100, 0x0300},
	{RADEON_OV0_GAMMA_200_23F, 0x100, 0x0400},
	{RADEON_OV0_GAMMA_240_27F, 0x100, 0x0400},
	{RADEON_OV0_GAMMA_280_2BF, 0x100, 0x0500},
	{RADEON_OV0_GAMMA_2C0_2FF, 0x100, 0x0500},
	{RADEON_OV0_GAMMA_300_33F, 0x100, 0x0600},
	{RADEON_OV0_GAMMA_340_37F, 0x100, 0x0600},
	{RADEON_OV0_GAMMA_380_3BF, 0x100, 0x0700},
	{RADEON_OV0_GAMMA_3C0_3FF, 0x100, 0x0700}
};

/****************************************************************************
 * SetTransform                                                             *
 *  Function: Calculates and sets color space transform from supplied       *
 *            reference transform, gamma, brightness, contrast, hue and     *
 *            saturation.                                                   *
 *    Inputs: bright - brightness                                           *
 *            cont - contrast                                               *
 *            sat - saturation                                              *
 *            hue - hue                                                     *
 *            ref - index to the table of refernce transforms               *
 *   Outputs: NONE                                                          *
 ****************************************************************************/

static void RADEONSetTransform(  ScrnInfoPtr pScrn,
							   float bright, float cont, float sat, 
							   float hue, CARD32 ref)
{
	RADEONInfoPtr info = RADEONPTR(pScrn);
	unsigned char *RADEONMMIO = info->MMIO;
	float OvHueSin, OvHueCos;
	float CAdjLuma, CAdjOff;
	float CAdjRCb, CAdjRCr;
	float CAdjGCb, CAdjGCr;
	float CAdjBCb, CAdjBCr;
	float OvLuma, OvROff, OvGOff, OvBOff;
	float OvRCb, OvRCr;
	float OvGCb, OvGCr;
	float OvBCb, OvBCr;
	float Loff = 64.0;
	float Coff = 512.0f;

	CARD32 dwOvLuma, dwOvROff, dwOvGOff, dwOvBOff;
	CARD32 dwOvRCb, dwOvRCr;
	CARD32 dwOvGCb, dwOvGCr;
	CARD32 dwOvBCb, dwOvBCr;

	if (ref >= 2) return;

	OvHueSin = sin(hue);
	OvHueCos = cos(hue);

	CAdjLuma = cont * trans[ref].RefLuma;
	CAdjOff = cont * trans[ref].RefLuma * bright * 1023.0;

	CAdjRCb = sat * -OvHueSin * trans[ref].RefRCr;
	CAdjRCr = sat * OvHueCos * trans[ref].RefRCr;
	CAdjGCb = sat * (OvHueCos * trans[ref].RefGCb - OvHueSin * trans[ref].RefGCr);
	CAdjGCr = sat * (OvHueSin * trans[ref].RefGCb + OvHueCos * trans[ref].RefGCr);
	CAdjBCb = sat * OvHueCos * trans[ref].RefBCb;
	CAdjBCr = sat * OvHueSin * trans[ref].RefBCb;
    
    #if 0 /* default constants */
        CAdjLuma = 1.16455078125;

	CAdjRCb = 0.0;
	CAdjRCr = 1.59619140625;
	CAdjGCb = -0.39111328125;
	CAdjGCr = -0.8125;
	CAdjBCb = 2.01708984375;
	CAdjBCr = 0;
   #endif
	OvLuma = CAdjLuma;
	OvRCb = CAdjRCb;
	OvRCr = CAdjRCr;
	OvGCb = CAdjGCb;
	OvGCr = CAdjGCr;
	OvBCb = CAdjBCb;
	OvBCr = CAdjBCr;
	OvROff = CAdjOff -
		OvLuma * Loff - (OvRCb + OvRCr) * Coff;
	OvGOff = CAdjOff - 
		OvLuma * Loff - (OvGCb + OvGCr) * Coff;
	OvBOff = CAdjOff - 
		OvLuma * Loff - (OvBCb + OvBCr) * Coff;
   #if 0 /* default constants */
	OvROff = -888.5;
	OvGOff = 545;
	OvBOff = -1104;
   #endif 
   
	dwOvROff = ((INT32)(OvROff * 2.0)) & 0x1fff;
	dwOvGOff = (INT32)(OvGOff * 2.0) & 0x1fff;
	dwOvBOff = (INT32)(OvBOff * 2.0) & 0x1fff;
	if(!info->IsR200)
	{
		dwOvLuma =(((INT32)(OvLuma * 2048.0))&0x7fff)<<17;
		dwOvRCb = (((INT32)(OvRCb * 2048.0))&0x7fff)<<1;
		dwOvRCr = (((INT32)(OvRCr * 2048.0))&0x7fff)<<17;
		dwOvGCb = (((INT32)(OvGCb * 2048.0))&0x7fff)<<1;
		dwOvGCr = (((INT32)(OvGCr * 2048.0))&0x7fff)<<17;
		dwOvBCb = (((INT32)(OvBCb * 2048.0))&0x7fff)<<1;
		dwOvBCr = (((INT32)(OvBCr * 2048.0))&0x7fff)<<17;
	}
	else
	{
		dwOvLuma = (((INT32)(OvLuma * 256.0))&0x7ff)<<20;
		dwOvRCb = (((INT32)(OvRCb * 256.0))&0x7ff)<<4;
		dwOvRCr = (((INT32)(OvRCr * 256.0))&0x7ff)<<20;
		dwOvGCb = (((INT32)(OvGCb * 256.0))&0x7ff)<<4;
		dwOvGCr = (((INT32)(OvGCr * 256.0))&0x7ff)<<20;
		dwOvBCb = (((INT32)(OvBCb * 256.0))&0x7ff)<<4;
		dwOvBCr = (((INT32)(OvBCr * 256.0))&0x7ff)<<20;
	}

        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Red: Off=%g Y=%g Cb=%g Cr=%g\n",
		OvROff, OvLuma, OvRCb, OvRCr);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Green: Off=%g Y=%g Cb=%g Cr=%g\n",
		OvGOff, OvLuma, OvGCb, OvGCr);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Blue: Off=%g Y=%g Cb=%g Cr=%g\n",
		OvBOff, OvLuma, OvBCb, OvBCr);
	OUTREG(RADEON_OV0_LIN_TRANS_A, dwOvRCb | dwOvLuma);
	OUTREG(RADEON_OV0_LIN_TRANS_B, dwOvROff | dwOvRCr);
	OUTREG(RADEON_OV0_LIN_TRANS_C, dwOvGCb | dwOvLuma);
	OUTREG(RADEON_OV0_LIN_TRANS_D, dwOvGOff | dwOvGCr);
	OUTREG(RADEON_OV0_LIN_TRANS_E, dwOvBCb | dwOvLuma);
	OUTREG(RADEON_OV0_LIN_TRANS_F, dwOvBOff | dwOvBCr);

}


void RADEONShutdownVideo(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr   info      = RADEONPTR(pScrn);
    RADEONPortPrivPtr pPriv = info->adaptor->pPortPrivates[0].ptr;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Shutting down Xvideo subsystems\n");
    if(pPriv->i2c!=NULL){
	if(pPriv->msp3430!=NULL){
		xfree(pPriv->msp3430);
		pPriv->msp3430=NULL;
		}
	if(pPriv->fi1236!=NULL){
		xfree(pPriv->fi1236);
		pPriv->fi1236=NULL;
		}
	DestroyI2CBusRec(pPriv->i2c, TRUE, TRUE);
	pPriv->i2c=NULL;
	}
    if(pPriv->theatre!=NULL){
	xf86_ShutdownTheatre(pPriv->theatre);
	xfree(pPriv->theatre);
	pPriv->theatre=NULL;
	}
    if(pPriv->VIP!=NULL){
	xfree(pPriv->VIP);
	pPriv->VIP=NULL;
	}
}

void RADEONResetVideo(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr   info      = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    RADEONPortPrivPtr pPriv = info->adaptor->pPortPrivates[0].ptr;
    int i;

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

    xvDecBrightness   = MAKE_ATOM("XV_DEC_BRIGHTNESS");
    xvDecSaturation   = MAKE_ATOM("XV_DEC_SATURATION");
    xvDecColor        = MAKE_ATOM("XV_DEC_COLOR");
    xvDecContrast     = MAKE_ATOM("XV_DEC_CONTRAST");
    xvDecHue          = MAKE_ATOM("XV_DEC_HUE");

    xv_autopaint_colorkey = MAKE_ATOM("XV_AUTOPAINT_COLORKEY");
    xv_set_defaults = MAKE_ATOM("XV_SET_DEFAULTS");

    RADEONWaitForIdle(pScrn);
/*    RADEONWaitForFifo(pScrn, 7); */
    OUTREG(RADEON_OV0_SCALE_CNTL, 0x80000000);
    OUTREG(RADEON_OV0_AUTO_FLIP_CNTL, 0);   /* maybe */
    OUTREG(RADEON_OV0_FILTER_CNTL, 0x0000000f);
    OUTREG(RADEON_OV0_KEY_CNTL, RADEON_GRAPHIC_KEY_FN_EQ | 
                                RADEON_VIDEO_KEY_FN_FALSE |
				RADEON_CMP_MIX_OR);
    OUTREG(RADEON_OV0_TEST, 0);
    OUTREG(RADEON_FCP_CNTL, RADEON_FCP_CNTL__GND);
    OUTREG(RADEON_CAP0_TRIG_CNTL, 0);
    RADEONSetColorKey(pScrn, pPriv->colorKey);
    
    if(!info->IsR200)
	{
		OUTREG(RADEON_OV0_LIN_TRANS_A, 0x12a20000);
		OUTREG(RADEON_OV0_LIN_TRANS_B, 0x198a190e);
		OUTREG(RADEON_OV0_LIN_TRANS_C, 0x12a2f9da);
		OUTREG(RADEON_OV0_LIN_TRANS_D, 0xf2fe0442);
		OUTREG(RADEON_OV0_LIN_TRANS_E, 0x12a22046);
		OUTREG(RADEON_OV0_LIN_TRANS_F, 0x175f);
	}
	else 
	{
		OUTREG(RADEON_OV0_LIN_TRANS_A, 0x12a00000);
		OUTREG(RADEON_OV0_LIN_TRANS_B, 0x1990190e);
		OUTREG(RADEON_OV0_LIN_TRANS_C, 0x12a0f9c0);
		OUTREG(RADEON_OV0_LIN_TRANS_D, 0xf3000442);
		OUTREG(RADEON_OV0_LIN_TRANS_E, 0x12a02040);
		OUTREG(RADEON_OV0_LIN_TRANS_F, 0x175f);

		/* Default Gamma, 
		   Of 18 segments for gamma cure, all segments in R200 are programmable, 
		   while only lower 4 and upper 2 segments are programmable in Radeon*/
		if(info->IsR200)
		{
			for(i=0; i<18; i++)
			{
				OUTREG(def_gamma[i].gammaReg,
					   (def_gamma[i].gammaSlope<<16) | def_gamma[i].gammaOffset);
			}
		}
	}

    if(pPriv->VIP!=NULL){
    	RADEONVIP_reset(pScrn,pPriv);
	}
    
    if(pPriv->theatre != NULL) {
        xf86_InitTheatre(pPriv->theatre);
	xf86_ResetTheatreRegsForNoTVout(pPriv->theatre);
	}
    
    if(pPriv->i2c != NULL){
    	RADEONResetI2C(pScrn, pPriv);
	}
}



#define I2C_DONE	(1<<0)
#define I2C_NACK	(1<<1)
#define I2C_HALT	(1<<2)
#define I2C_SOFT_RST	(1<<5)
#define I2C_DRIVE_EN	(1<<6)
#define I2C_DRIVE_SEL	(1<<7)
#define I2C_START	(1<<8)
#define I2C_STOP	(1<<9)
#define I2C_RECEIVE	(1<<10)
#define I2C_ABORT	(1<<11)
#define I2C_GO		(1<<12)
#define I2C_SEL		(1<<16)
#define I2C_EN		(1<<17)


/****************************************************************************
 *  I2C_WaitForAck (void)                                                   *
 *                                                                          *
 *  Function: polls the I2C status bits, waiting for an acknowledge or      *
 *            an error condition.                                           *
 *    Inputs: NONE                                                          *
 *   Outputs: I2C_DONE - the I2C transfer was completed                     *
 *            I2C_NACK - an NACK was received from the slave                *
 *            I2C_HALT - a timeout condition has occured                    *
 ****************************************************************************/
static CARD8 RADEON_I2C_WaitForAck (ScrnInfoPtr pScrn, RADEONPortPrivPtr pPriv)
{
    CARD8 retval = 0;
    RADEONInfoPtr info = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    long counter = 0;

    usleep(1);
    while(1)
    {
        RADEONWaitForIdle(pScrn); 
        retval = INREG8(RADEON_I2C_CNTL_0);
        if (retval & I2C_HALT)
        {
            return (I2C_HALT);
        }
        if (retval & I2C_NACK)
        {
            return (I2C_NACK);
        }
        if(retval & I2C_DONE)
	{
	    return I2C_DONE;
	}	
	usleep(10);
	counter++;
	if(counter>100)
	{
	     xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Timeout condition on Radeon i2c bus\n");
	     return I2C_HALT;
	}
	
    }
}

static void RADEON_I2C_Halt (ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    CARD8    reg;
    long counter = 0;

    /* reset status flags */
    RADEONWaitForIdle(pScrn);
    reg = INREG8 (RADEON_I2C_CNTL_0 + 0) & 0xF8;
    OUTREG8 (RADEON_I2C_CNTL_0 + 0, reg);

    /* issue ABORT call */
    RADEONWaitForIdle(pScrn);
    reg = INREG8 (RADEON_I2C_CNTL_0 + 1) & 0xE7;
    OUTREG8 (RADEON_I2C_CNTL_0 + 1, (reg | 0x18));

    /* wait for GO bit to go low */
    RADEONWaitForIdle(pScrn);
    while (INREG8 (RADEON_I2C_CNTL_0 + 0) & I2C_GO)
    {
      usleep(1);
      counter++;
      if(counter>1000)return;
    }
} 


static Bool RADEONI2CWriteRead(I2CDevPtr d, I2CByte *WriteBuffer, int nWrite,
                            I2CByte *ReadBuffer, int nRead)
{
    int loop, status;
    CARD32 i2c_cntl_0, i2c_cntl_1;
    RADEONPortPrivPtr pPriv = (RADEONPortPrivPtr)(d->pI2CBus->DriverPrivate.ptr);
    ScrnInfoPtr pScrn = xf86Screens[d->pI2CBus->scrnIndex];
    RADEONInfoPtr info = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;

    status=I2C_DONE;

    RADEONWaitForIdle(pScrn);
    if(nWrite>0){
/*       RADEONWaitForFifo(pScrn, 4+nWrite); */

       /* Clear the status bits of the I2C Controller */
       OUTREG(RADEON_I2C_CNTL_0, I2C_DONE | I2C_NACK | I2C_HALT | I2C_SOFT_RST);

       /* Write the address into the buffer first */
       OUTREG(RADEON_I2C_DATA, (CARD32) (d->SlaveAddr) & ~(1));

       /* Write Value into the buffer */
       for (loop = 0; loop < nWrite; loop++)
       {
          OUTREG8(RADEON_I2C_DATA, WriteBuffer[loop]);
       }

       i2c_cntl_1 = (pPriv->radeon_i2c_timing << 24) | I2C_EN | I2C_SEL |
    			nWrite | 0x100;
       OUTREG(RADEON_I2C_CNTL_1, i2c_cntl_1);
    
       i2c_cntl_0 = (pPriv->radeon_N << 24) | (pPriv->radeon_M << 16) | 
    			I2C_GO | I2C_START | ((nRead >0)?0:I2C_STOP) | I2C_DRIVE_EN;
       OUTREG(RADEON_I2C_CNTL_0, i2c_cntl_0);
    
       while(INREG8(RADEON_I2C_CNTL_0+1) & (I2C_GO >> 8));

       status=RADEON_I2C_WaitForAck(pScrn,pPriv);

       if(status!=I2C_DONE){
       	  RADEON_I2C_Halt(pScrn);
       	  return FALSE;
	  }
    }
    
    
    if(nRead > 0) {
       RADEONWaitForFifo(pScrn, 4+nRead);
    
       OUTREG(RADEON_I2C_CNTL_0, I2C_DONE | I2C_NACK | I2C_HALT | I2C_SOFT_RST); 

       /* Write the address into the buffer first */
       OUTREG(RADEON_I2C_DATA, (CARD32) (d->SlaveAddr) | (1));

       i2c_cntl_1 = (pPriv->radeon_i2c_timing << 24) | I2C_EN | I2C_SEL | 
    			nRead | 0x100;
       OUTREG(RADEON_I2C_CNTL_1, i2c_cntl_1);
    
       i2c_cntl_0 = (pPriv->radeon_N << 24) | (pPriv->radeon_M << 16) | 
    			I2C_GO | I2C_START | I2C_STOP | I2C_DRIVE_EN | I2C_RECEIVE;
       OUTREG(RADEON_I2C_CNTL_0, i2c_cntl_0);
    
       RADEONWaitForIdle(pScrn);
       while(INREG8(RADEON_I2C_CNTL_0+1) & (I2C_GO >> 8));

       status=RADEON_I2C_WaitForAck(pScrn,pPriv);
  
       usleep(1);

       /* Write Value into the buffer */
       for (loop = 0; loop < nRead; loop++)
       {
          RADEONWaitForFifo(pScrn, 1);
	  if((status == I2C_HALT) || (status == I2C_NACK))
	  {
	  ReadBuffer[loop]=0xff;
	  } else {
          RADEONWaitForIdle(pScrn);
          ReadBuffer[loop]=INREG8(RADEON_I2C_DATA) & 0xff;
	  }
       }
    }
    
    if(status!=I2C_DONE){
       RADEON_I2C_Halt(pScrn);
       return FALSE;
       }
    return TRUE;
}

static Bool R200_I2CWriteRead(I2CDevPtr d, I2CByte *WriteBuffer, int nWrite,
                            I2CByte *ReadBuffer, int nRead)
{
    int loop, status;
    CARD32 i2c_cntl_0, i2c_cntl_1;
    RADEONPortPrivPtr pPriv = (RADEONPortPrivPtr)(d->pI2CBus->DriverPrivate.ptr);
    ScrnInfoPtr pScrn = xf86Screens[d->pI2CBus->scrnIndex];
    RADEONInfoPtr info = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;

    status=I2C_DONE;

    RADEONWaitForIdle(pScrn);
    if(nWrite>0){
/*       RADEONWaitForFifo(pScrn, 4+nWrite); */

       /* Clear the status bits of the I2C Controller */
       OUTREG(RADEON_I2C_CNTL_0, I2C_DONE | I2C_NACK | I2C_HALT | I2C_SOFT_RST);

       /* Write the address into the buffer first */
       OUTREG(RADEON_I2C_DATA, (CARD32) (d->SlaveAddr) & ~(1));

       /* Write Value into the buffer */
       for (loop = 0; loop < nWrite; loop++)
       {
          OUTREG8(RADEON_I2C_DATA, WriteBuffer[loop]);
       }

       i2c_cntl_1 = (pPriv->radeon_i2c_timing << 24) | I2C_EN | I2C_SEL |
    			nWrite | 0x010;
       OUTREG(RADEON_I2C_CNTL_1, i2c_cntl_1);
    
       i2c_cntl_0 = (pPriv->radeon_N << 24) | (pPriv->radeon_M << 16) | 
    			I2C_GO | I2C_START | ((nRead >0)?0:I2C_STOP) | I2C_DRIVE_EN;
       OUTREG(RADEON_I2C_CNTL_0, i2c_cntl_0);
    
       while(INREG8(RADEON_I2C_CNTL_0+1) & (I2C_GO >> 8));

       status=RADEON_I2C_WaitForAck(pScrn,pPriv);

       if(status!=I2C_DONE){
       	  RADEON_I2C_Halt(pScrn);
       	  return FALSE;
	  }
    }
    
    
    if(nRead > 0) {
       RADEONWaitForFifo(pScrn, 4+nRead);
    
       OUTREG(RADEON_I2C_CNTL_0, I2C_DONE | I2C_NACK | I2C_HALT | I2C_SOFT_RST); 

       /* Write the address into the buffer first */
       OUTREG(RADEON_I2C_DATA, (CARD32) (d->SlaveAddr) | (1));

       i2c_cntl_1 = (pPriv->radeon_i2c_timing << 24) | I2C_EN | I2C_SEL | 
    			nRead | 0x010;
       OUTREG(RADEON_I2C_CNTL_1, i2c_cntl_1);
    
       i2c_cntl_0 = (pPriv->radeon_N << 24) | (pPriv->radeon_M << 16) | 
    			I2C_GO | I2C_START | I2C_STOP | I2C_DRIVE_EN | I2C_RECEIVE;
       OUTREG(RADEON_I2C_CNTL_0, i2c_cntl_0);
    
       RADEONWaitForIdle(pScrn);
       while(INREG8(RADEON_I2C_CNTL_0+1) & (I2C_GO >> 8));

       status=RADEON_I2C_WaitForAck(pScrn,pPriv);
  
       usleep(1);

       /* Write Value into the buffer */
       for (loop = 0; loop < nRead; loop++)
       {
          RADEONWaitForFifo(pScrn, 1);
	  if((status == I2C_HALT) || (status == I2C_NACK))
	  {
	  ReadBuffer[loop]=0xff;
	  } else {
          RADEONWaitForIdle(pScrn);
          ReadBuffer[loop]=INREG8(RADEON_I2C_DATA) & 0xff;
	  }
       }
    }
    
    if(status!=I2C_DONE){
       RADEON_I2C_Halt(pScrn);
       return FALSE;
       }
    return TRUE;
}

static Bool RADEONProbeAddress(I2CBusPtr b, I2CSlaveAddr addr)
{
     I2CByte a;
     I2CDevRec d;
     
     d.DevName = "Probing";
     d.SlaveAddr = addr;
     d.pI2CBus = b;
     d.NextDev = NULL;
     
     return I2C_WriteRead(&d, NULL, 0, &a, 1);
}


#define I2C_CLOCK_FREQ     (80000.0)


const struct 
{
   char *name; 
   int type;
} RADEON_tuners[32] =
    {
        /* name	,index to tuner_parms table */
	{"NO TUNNER"		, -1},
	{"Philips FI1236 (or compatible)"		, TUNER_TYPE_FI1236},
	{"Philips FI1236 (or compatible)"		, TUNER_TYPE_FI1236},
	{"Philips FI1216 (or compatible)"		, TUNER_TYPE_FI1216},
	{"Philips FI1246 (or compatible)"		, -1},
	{"Philips FI1216MF (or compatible)"		, TUNER_TYPE_FI1216},
	{"Philips FI1236 (or compatible)"		, TUNER_TYPE_FI1236},
	{"Philips FI1256 (or compatible)"		, -1},
	{"Philips FI1236 (or compatible)"		, TUNER_TYPE_FI1236},
	{"Philips FI1216 (or compatible)"		, TUNER_TYPE_FI1216},
	{"Philips FI1246 (or compatible)"		, -1},
	{"Philips FI1216MF (or compatible)"		, TUNER_TYPE_FI1216},
	{"Philips FI1236 (or compatible)"		, TUNER_TYPE_FI1236},
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
	{"Microtuner MT2032"		, TUNER_TYPE_MT2032},
        {"UNKNOWN-30"		, -1},
	{"UNKNOWN-31"		, -1}
    };


static void RADEONResetI2C(ScrnInfoPtr pScrn, RADEONPortPrivPtr pPriv)
{
    RADEONInfoPtr info = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;

    RADEONWaitForFifo(pScrn, 2);
    OUTREG8(RADEON_I2C_CNTL_1+2, ((I2C_SEL | I2C_EN)>>16));
    OUTREG8(RADEON_I2C_CNTL_0+0, (I2C_DONE | I2C_NACK | I2C_HALT | I2C_SOFT_RST | I2C_DRIVE_EN | I2C_DRIVE_SEL));
}

static void RADEONInitI2C(ScrnInfoPtr pScrn, RADEONPortPrivPtr pPriv)
{
    double nm;
    RADEONInfoPtr info = RADEONPTR(pScrn);
    RADEONPLLPtr  pll = &(info->pll);
    int i;

    pPriv->fi1236 = NULL;
    pPriv->msp3430 = NULL;
    
    if(pPriv->i2c!=NULL) return;
    if(!xf86LoadSubModule(pScrn,"i2c")) 
    {
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
    pPriv->i2c=CreateI2CBusRec();
    pPriv->i2c->scrnIndex=pScrn->scrnIndex;
    pPriv->i2c->BusName="Radeon multimedia bus";
    pPriv->i2c->DriverPrivate.ptr=(pointer)pPriv;
    if(info->IsR200)
	    pPriv->i2c->I2CWriteRead=R200_I2CWriteRead;
	    else
	    pPriv->i2c->I2CWriteRead=RADEONI2CWriteRead;
    if(!I2CBusInit(pPriv->i2c))
    {
    	xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"Failed to register i2c bus\n");
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "*** %p versus %p\n", xf86CreateI2CBusRec, CreateI2CBusRec);

    nm=(pll->reference_freq * 10000.0)/(4.0 * I2C_CLOCK_FREQ);
    for(pPriv->radeon_N=1; pPriv->radeon_N<255; pPriv->radeon_N++)
          if((pPriv->radeon_N * (pPriv->radeon_N-1)) > nm)break;
    pPriv->radeon_M=pPriv->radeon_N-1;
    pPriv->radeon_i2c_timing=2*pPriv->radeon_N;

    RADEONResetI2C(pScrn, pPriv);
    
#if 0 /* I don't know whether standalone boards are supported with Radeon */
      /* looks like none of them have AMC connectors anyway */
    if(!pPriv->MM_TABLE_valid)RADEON_read_eeprom(pPriv);
#endif    
    
    /* no multimedia capabilities detected */
    if(!pPriv->MM_TABLE_valid)
    {
       xf86DrvMsg(pScrn->scrnIndex, X_INFO, "No video input capabilities detected\n");
       return;
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
	       RADEON_tuners[pPriv->MM_TABLE.tuner_type & 0x1f].name,
               FI1236_ADDR(pPriv->fi1236));
         if(pPriv->MM_TABLE_valid)xf86_FI1236_set_tuner_type(pPriv->fi1236, RADEON_tuners[pPriv->MM_TABLE.tuner_type & 0x1f].type);
	 	else {
		   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "MM_TABLE not found (standalone board ?), forcing tuner type to NTSC\n");
		    xf86_FI1236_set_tuner_type(pPriv->fi1236, TUNER_TYPE_FI1236);
		}
    }
    
    if(!xf86LoadSubModule(pScrn,"msp3430"))
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
#if 0 /* this would confuse bt829 with msp3430 */
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
       pPriv->msp3430->standard = MSP3430_NTSC;
       pPriv->msp3430->connector = MSP3430_CONNECTOR_1;
       xf86_ResetMSP3430(pPriv->msp3430);
       xf86_InitMSP3430(pPriv->msp3430);
       xf86_MSP3430SetVolume(pPriv->msp3430, pPriv->mute ? MSP3430_FAST_MUTE : pPriv->volume);
    }
    
    if(pPriv->i2c != NULL)RADEON_board_setmisc(pPriv);
    #if 1
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Scanning I2C Bus\n");
    for(i=0;i<255;i+=2)
    	if(RADEONProbeAddress(pPriv->i2c, i))
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "     found device at address 0x%02x\n", i);
    #endif
}

#define VIP_NAME      "RADEON VIP BUS"
#define VIP_TYPE      "ATI VIP BUS"

static Bool RADEONVIP_ioctl(GENERIC_BUS_Ptr b, long ioctl, long arg1, char *arg2)
{
    long count;
    switch(ioctl){
        case GB_IOCTL_GET_NAME:
	          count=strlen(VIP_NAME)+1;
		  if(count>arg1)return FALSE;
		  memcpy(arg2,VIP_NAME,count);
		  return TRUE;
		  
        case GB_IOCTL_GET_TYPE:
	          count=strlen(VIP_TYPE)+1;
		  if(count>arg1)return FALSE;
		  memcpy(arg2,VIP_TYPE,count);
		  return TRUE;
		  
        default: 
	          return FALSE;
    }
}

static CARD32 RADEONVIP_idle(GENERIC_BUS_Ptr b)
{
   ScrnInfoPtr pScrn = xf86Screens[b->scrnIndex];
   RADEONInfoPtr info = RADEONPTR(pScrn);
   unsigned char *RADEONMMIO = info->MMIO;

   CARD32 timeout;
   
   RADEONWaitForIdle(pScrn);
   timeout = INREG(VIPH_TIMEOUT_STAT);
   if(timeout & VIPH_TIMEOUT_STAT__VIPH_REG_STAT) /* lockup ?? */
   {
       RADEONWaitForFifo(pScrn, 2);
       OUTREG(VIPH_TIMEOUT_STAT, (timeout & 0xffffff00) | VIPH_TIMEOUT_STAT__VIPH_REG_AK);
       RADEONWaitForIdle(pScrn);
       return (INREG(VIPH_CONTROL) & 0x2000) ? VIP_BUSY : VIP_RESET;
   }
   RADEONWaitForIdle(pScrn);
   return (INREG(VIPH_CONTROL) & 0x2000) ? VIP_BUSY : VIP_IDLE ;
}

/* address format:
     ((device & 0x3)<<14)   | (fifo << 12) | (addr)
*/

static Bool RADEONVIP_read(GENERIC_BUS_Ptr b, CARD32 address, CARD32 count, CARD8 *buffer)
{
   ScrnInfoPtr pScrn = xf86Screens[b->scrnIndex];
   RADEONInfoPtr info = RADEONPTR(pScrn);
   unsigned char *RADEONMMIO = info->MMIO;
   CARD32 status,tmp;

   if((count!=1) && (count!=2) && (count!=4))
   {
   xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"Attempt to access VIP bus with non-stadard transaction length\n");
   return FALSE;
   }
   
   RADEONWaitForFifo(pScrn, 2);
   OUTREG(VIPH_REG_ADDR, address | 0x2000);
   while(VIP_BUSY == (status = RADEONVIP_idle(b)));
   if(VIP_IDLE != status) return FALSE;
   
/*
         disable VIPH_REGR_DIS to enable VIP cycle.
         The LSB of VIPH_TIMEOUT_STAT are set to 0
         because 1 would have acknowledged various VIP
         interrupts unexpectedly 
*/	
   RADEONWaitForIdle(pScrn);
   OUTREG(VIPH_TIMEOUT_STAT, INREG(VIPH_TIMEOUT_STAT) & (0xffffff00 & ~VIPH_TIMEOUT_STAT__VIPH_REGR_DIS) );
/*
         the value returned here is garbage.  The read merely initiates
         a register cycle
*/
    RADEONWaitForIdle(pScrn);
    INREG(VIPH_REG_DATA);
    
    while(VIP_BUSY == (status = RADEONVIP_idle(b)));
    if(VIP_IDLE != status) return FALSE;
/*
        set VIPH_REGR_DIS so that the read won't take too long.
*/
    RADEONWaitForIdle(pScrn);
    tmp=INREG(VIPH_TIMEOUT_STAT);
    OUTREG(VIPH_TIMEOUT_STAT, (tmp & 0xffffff00) | VIPH_TIMEOUT_STAT__VIPH_REGR_DIS);	      
    RADEONWaitForIdle(pScrn);
    switch(count){
        case 1:
	     *buffer=(CARD8)(INREG(VIPH_REG_DATA) & 0xff);
	     break;
	case 2:
	     *(CARD16 *)buffer=(CARD16) (INREG(VIPH_REG_DATA) & 0xffff);
	     break;
	case 4:
	     *(CARD32 *)buffer=(CARD32) ( INREG(VIPH_REG_DATA) & 0xffffffff);
	     break;
	}
     while(VIP_BUSY == (status = RADEONVIP_idle(b)));
     if(VIP_IDLE != status) return FALSE;
 /*	
 so that reading VIPH_REG_DATA would not trigger unnecessary vip cycles.
*/
     OUTREG(VIPH_TIMEOUT_STAT, (INREG(VIPH_TIMEOUT_STAT) & 0xffffff00) | VIPH_TIMEOUT_STAT__VIPH_REGR_DIS);
     return TRUE;
}

static Bool RADEONVIP_write(GENERIC_BUS_Ptr b, CARD32 address, CARD32 count, CARD8 *buffer)
{
    ScrnInfoPtr pScrn = xf86Screens[b->scrnIndex];
    RADEONInfoPtr info = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    
    CARD32 status;


    if((count!=4))
    {
    xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Attempt to access VIP bus with non-stadard transaction length\n");
    return FALSE;
    }
    
    RADEONWaitForFifo(pScrn, 2);
    OUTREG(VIPH_REG_ADDR, address & (~0x2000));
    while(VIP_BUSY == (status = RADEONVIP_idle(b)));
    
    if(VIP_IDLE != status) return FALSE;
    
    RADEONWaitForFifo(pScrn, 2);
    switch(count){
        case 4:
	     OUTREG(VIPH_REG_DATA, *(CARD32 *)buffer);
	     break;
	}
    while(VIP_BUSY == (status = RADEONVIP_idle(b)));
    if(VIP_IDLE != status) return FALSE;
    return TRUE;
}

void RADEONVIP_reset(ScrnInfoPtr pScrn, RADEONPortPrivPtr pPriv)
{
    RADEONInfoPtr info = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;


    RADEONWaitForIdle(pScrn);
    OUTREG(VIPH_CONTROL, 0x003F0004); /* slowest, timeout in 16 phases */
    OUTREG(VIPH_TIMEOUT_STAT, (INREG(VIPH_TIMEOUT_STAT) & 0xFFFFFF00) | VIPH_TIMEOUT_STAT__VIPH_REGR_DIS);
    OUTREG(VIPH_DV_LAT, 0x444400FF); /* set timeslice */
    OUTREG(VIPH_BM_CHUNK, 0x151);
    OUTREG(RADEON_TEST_DEBUG_CNTL, INREG(RADEON_TEST_DEBUG_CNTL) & (~TEST_DEBUG_CNTL__TEST_DEBUG_OUT_EN));
}

static void RADEONVIP_init(ScrnInfoPtr pScrn, RADEONPortPrivPtr pPriv)
{
    pPriv->VIP=xcalloc(1,sizeof(GENERIC_BUS_Rec));
    pPriv->VIP->scrnIndex=pScrn->scrnIndex;
    pPriv->VIP->DriverPrivate.ptr=pPriv;
    pPriv->VIP->ioctl=RADEONVIP_ioctl;
    pPriv->VIP->read=RADEONVIP_read;
    pPriv->VIP->write=RADEONVIP_write;
    
    RADEONVIP_reset(pScrn, pPriv);
}

static int RADEON_eeprom_addresses[] = { 0xA8, 0x70, 0x40, 0x78, 0x72, 0x42, 0};

static void RADEON_read_eeprom(RADEONPortPrivPtr pPriv)
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

   for(i=0;RADEON_eeprom_addresses[i];i++)
   {
     d.SlaveAddr = RADEON_eeprom_addresses[i];
     data[0]=0x00;
     if(!I2C_WriteRead(&d, data, 1, NULL, 0))continue;
     if(!I2C_WriteRead(&d, NULL, 0, data, 5))continue;
     if(!memcmp(data, "ATI", 3))
     {
        pPriv->EEPROM_present = TRUE;
	pPriv->EEPROM_addr = RADEON_eeprom_addresses[i];
	break;
     }
     xf86DrvMsg(pPriv->i2c->scrnIndex, X_INFO, "Device at eeprom addr 0x%02x found, returned 0x%02x-0x%02x-0x%02x-0x%02x-0x%02x\n",
         d.SlaveAddr,
     	 data[0], data[1], data[2], data[3], data[4]);
   }


}



static void RADEONReadMM_TABLE(ScrnInfoPtr pScrn, RADEONPortPrivPtr pPriv)
{
     RADEONInfoPtr info = RADEONPTR(pScrn);
     CARD16 mm_table;
     CARD16 bios_header;

     if(info->VBIOS==NULL){
     	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Cannot access BIOS: info->VBIOS==NULL.\n");
     	}

     xf86DrvMsg(pScrn->scrnIndex, X_INFO, "0x%02x 0x%02x\n", info->VBIOS[0],
               info->VBIOS[1]);	
     bios_header=info->VBIOS[0x48];
     bios_header+=(((int)info->VBIOS[0x49]+0)<<8);	     
	
     mm_table=info->VBIOS[bios_header+0x38];
     if(mm_table==0)
     {
         xf86DrvMsg(pScrn->scrnIndex,X_INFO,"No MM_TABLE found",bios_header,mm_table);
	 pPriv->MM_TABLE_valid = FALSE;
	 return;
     }    
     mm_table+=(((int)info->VBIOS[bios_header+0x39]+0)<<8)-2;

     xf86DrvMsg(pScrn->scrnIndex,X_INFO,"VIDEO BIOS TABLE OFFSETS: bios_header=0x%04x mm_table=0x%04x\n",bios_header,mm_table);

     if(mm_table>0)
     {
	 memcpy(&(pPriv->MM_TABLE), &(info->VBIOS[mm_table]), sizeof(_MM_TABLE));
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
         xf86DrvMsg(pScrn->scrnIndex,X_INFO,"No MM_TABLE found\n",bios_header,mm_table);
	 pPriv->MM_TABLE_valid = FALSE;
     }    
}

static Bool RADEONSetupTheatre(ScrnInfoPtr pScrn, RADEONPortPrivPtr pPriv, TheatrePtr t)
{
    RADEONInfoPtr info = RADEONPTR(pScrn);
    RADEONPLLPtr  pll = &(info->pll);

    CARD8 a;
    CARD16 bios_header;
    CARD16 pll_info_block;
    int i;
	
     xf86DrvMsg(pScrn->scrnIndex, X_INFO, "0x%02x 0x%02x\n", info->VBIOS[0],
               info->VBIOS[1]);	
     bios_header=info->VBIOS[0x48];
     bios_header+=(((int)info->VBIOS[0x49]+0)<<8);	     
	
     pll_info_block=info->VBIOS[bios_header+0x30];
     pll_info_block+=(((int)info->VBIOS[bios_header+0x31]+0)<<8);
       
     t->video_decoder_type=info->VBIOS[pll_info_block+0x08];
     t->video_decoder_type+=(((int)info->VBIOS[pll_info_block+0x09]+0)<<8);
	
     xf86DrvMsg(t->VIP->scrnIndex,X_INFO,"video decoder type is 0x%04x versus 0x%04x\n",t->video_decoder_type,
		pll->xclk);
	
     if(!pPriv->MM_TABLE_valid)
     {
       xf86DrvMsg(t->VIP->scrnIndex, X_INFO, "no multimedia table present, not using Rage Theatre for input\n");
       free(pPriv->theatre);
       pPriv->theatre = NULL;
       return FALSE;
     }
     for(i=0;i<5;i++){
		a=pPriv->MM_TABLE.input[i];
		
		switch(a & 0x3){
			case 1:
				t->wTunerConnector=i;
				xf86DrvMsg(t->VIP->scrnIndex,X_INFO,"Tuner is on port %d\n",i);
				break;
			case 2:  if(a & 0x4){
				   t->wComp0Connector=RT_COMP2;
				   } else {
				   t->wComp0Connector=RT_COMP1;
				   }
				xf86DrvMsg(t->VIP->scrnIndex,X_INFO,"Composite connector is port %d\n",t->wComp0Connector);
				  break;
			case 3:  if(a & 0x4){
				   t->wSVideo0Connector=RT_YCR_COMP4;
				   } else {
				   t->wSVideo0Connector=RT_YCF_COMP4;
				   }
				xf86DrvMsg(t->VIP->scrnIndex,X_INFO,"SVideo connector is port %d\n",t->wSVideo0Connector);
				   break;
			default:
				break;
			}
		}

	/* this is fiction, we should read MM table instead 
	t->wTunerConnector=0;
	t->wComp0Connector=5;
	t->wSVideo0Connector=3;
	 */
	
	switch(pll->reference_freq){
		case 2700:
			t->video_decoder_type=RT_FREF_2700;
			break;
		case 2950:
			t->video_decoder_type=RT_FREF_2950;
			break;
		default:
			xf86DrvMsg(t->VIP->scrnIndex,X_INFO,
				"Unsupported reference clock frequency, Rage Theatre disabled\n");
			t->theatre_num=-1;
			return FALSE;
		}
	xf86DrvMsg(t->VIP->scrnIndex,X_INFO,"video decoder type used: 0x%04x\n",t->video_decoder_type);
	return TRUE;
}


static XF86VideoAdaptorPtr
RADEONAllocAdaptor(ScrnInfoPtr pScrn)
{
    XF86VideoAdaptorPtr adapt;
    RADEONInfoPtr info = RADEONPTR(pScrn);
    RADEONPortPrivPtr pPriv;
    unsigned char *RADEONMMIO = info->MMIO;

    if(!(adapt = xf86XVAllocateVideoAdaptorRec(pScrn)))
	return NULL;

    if(!(pPriv = xcalloc(1, sizeof(RADEONPortPrivRec) + sizeof(DevUnion))))
    {
	xfree(adapt);
	return NULL;
    }

    adapt->pPortPrivates = (DevUnion*)(&pPriv[1]);
    adapt->pPortPrivates[0].ptr = (pointer)pPriv;

    pPriv->colorKey = info->videoKey;
    pPriv->doubleBuffer = TRUE;
    pPriv->videoStatus = 0;
    
    pPriv->transform_index = 0;
    pPriv->brightness = 0;
    pPriv->saturation = 0;
    pPriv->contrast = 0;
    pPriv->hue = 0;

    pPriv->dec_brightness = 0;
    pPriv->dec_saturation = 0;
    pPriv->dec_contrast = 0;
    pPriv->dec_hue = 0;

    pPriv->currentBuffer = 0;

    pPriv->video_stream_active = FALSE;
    pPriv->encoding = 4;
    pPriv->frequency = 1000;
    pPriv->volume = 0x01;
    pPriv->mute = TRUE;
    pPriv->v=0;

    pPriv->autopaint_colorkey = TRUE;

    /* Unlike older Mach64 chips, RADEON has only two ECP settings: 0 for PIXCLK < 175Mhz, and 1 (divide by 2)
       for higher clocks, sure makes life nicer */
    if(info->ModeReg.dot_clock_freq < 17500) pPriv->ecp_div = 0;
    	   else pPriv->ecp_div = 1;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Dotclock is %g Mhz, setting ecp_div to %d\n", info->ModeReg.dot_clock_freq/100.0, pPriv->ecp_div);
 
    OUTPLL(RADEON_VCLK_ECP_CNTL, (INPLL(pScrn, RADEON_VCLK_ECP_CNTL) & 0xfffffCff) | (pPriv->ecp_div << 8));

    return adapt;
}

static XF86VideoAdaptorPtr
RADEONSetupImageVideo(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    RADEONInfoPtr info = RADEONPTR(pScrn);
    RADEONPortPrivPtr pPriv;
    XF86VideoAdaptorPtr adapt;

    if(info->adaptor != NULL){
    	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Reinitializing Xvideo subsystems\n");
    	RADEONResetVideo(pScrn);
    	return info->adaptor;
	}

    if(!(adapt = RADEONAllocAdaptor(pScrn)))
	return NULL;

    pPriv = (RADEONPortPrivPtr)(adapt->pPortPrivates[0].ptr);
    
    RADEONReadMM_TABLE(pScrn,pPriv);
    RADEONInitI2C(pScrn,pPriv);
    RADEONVIP_init(pScrn,pPriv);

    if(!xf86LoadSubModule(pScrn,"theatre")) 
    {
    	xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"Unable to initialize i2c bus\n");
	pPriv->i2c=NULL;
	return NULL;
    } 
    xf86LoaderReqSymbols(TheatreSymbolsList, NULL);
    pPriv->theatre=xf86_DetectTheatre(pPriv->VIP);
    if((pPriv->theatre!=NULL) && !RADEONSetupTheatre(pScrn,pPriv,pPriv->theatre))
    {
    	free(pPriv->theatre);
	pPriv->theatre=NULL;
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to initialize Rage Theatre, chip disabled\n");
    }
    
    if(pPriv->theatre != NULL) 
    {
       xf86_InitTheatre(pPriv->theatre);
       xf86_ResetTheatreRegsForNoTVout(pPriv->theatre);
       xf86_RT_SetTint(pPriv->theatre, pPriv->dec_hue);
       xf86_RT_SetSaturation(pPriv->theatre, pPriv->dec_saturation);
       xf86_RT_SetSharpness(pPriv->theatre, RT_NORM_SHARPNESS);
       xf86_RT_SetContrast(pPriv->theatre, pPriv->dec_contrast);
       xf86_RT_SetBrightness(pPriv->theatre, pPriv->dec_brightness);  
       RADEON_RT_SetEncoding(pPriv);
    }
    
    adapt->type = XvWindowMask | XvInputMask | XvImageMask | XvVideoMask;
    adapt->flags = VIDEO_OVERLAID_IMAGES | VIDEO_CLIP_TO_VIEWPORT;
    adapt->name = "ATI Radeon Video Adapter";
    if(pPriv->theatre != NULL)
    {
       adapt->nEncodings = 13;
       adapt->pEncodings = InputVideoEncodings;
    } else 
    {
       adapt->nEncodings = 1;
       adapt->pEncodings = &DummyEncoding;
    }
    adapt->nFormats = NUM_FORMATS;
    adapt->pFormats = Formats;
    adapt->nPorts = 1;
    if(pPriv->theatre==NULL)
	    adapt->nAttributes = NUM_ATTRIBUTES;
	    else
	    adapt->nAttributes = NUM_DEC_ATTRIBUTES;
    adapt->pAttributes = Attributes;
    adapt->nImages = NUM_IMAGES;
    adapt->pImages = Images;
    adapt->PutVideo = RADEONPutVideo;
    adapt->PutStill = NULL;
    adapt->GetVideo = NULL;
    adapt->GetStill = NULL;
    adapt->StopVideo = RADEONStopVideo;
    adapt->SetPortAttribute = RADEONSetPortAttribute;
    adapt->GetPortAttribute = RADEONGetPortAttribute;
    adapt->QueryBestSize = RADEONQueryBestSize;
    adapt->PutImage = RADEONPutImage;
    adapt->QueryImageAttributes = RADEONQueryImageAttributes;

    info->adaptor = adapt;

    REGION_INIT(pScreen, &(pPriv->clip), NullBox, 0);

    RADEONResetVideo(pScrn);

    if(info->VBIOS){
    	xfree(info->VBIOS);
	info->VBIOS=NULL;
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

    dataA = (pointer)REGION_RECTS(A);
    dataB = (pointer)REGION_RECTS(B);

    while(num--) {
	if((dataA[0] != dataB[0]) || (dataA[1] != dataB[1]))
	   return FALSE;
	dataA += 2;
	dataB += 2;
    }

    return TRUE;
}


/* RADEONClipVideo -

   Takes the dst box in standard X BoxRec form (top and left
   edges inclusive, bottom and right exclusive).  The new dst
   box is returned.  The source boundaries are given (xa, ya
   inclusive, xb, yb exclusive) and returned are the new source
   boundaries in 16.16 fixed point.
*/

#define DummyScreen screenInfo.screens[0]

static Bool
RADEONClipVideo(
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
RADEONStopVideo(ScrnInfoPtr pScrn, pointer data, Bool cleanup)
{
  RADEONInfoPtr info = RADEONPTR(pScrn);
  unsigned char *RADEONMMIO = info->MMIO;
  RADEONPortPrivPtr pPriv = (RADEONPortPrivPtr)data;

  REGION_EMPTY(pScrn->pScreen, &pPriv->clip);

  if(cleanup) {
     if(pPriv->videoStatus & CLIENT_VIDEO_ON) {
	RADEONWaitForFifo(pScrn, 2); 
	OUTREG(RADEON_OV0_SCALE_CNTL, 0);
     }
     if(pPriv->video_stream_active) {
	RADEONWaitForFifo(pScrn, 2); 
        OUTREG(RADEON_FCP_CNTL, RADEON_FCP_CNTL__GND);
	OUTREG(RADEON_CAP0_TRIG_CNTL, 0);
	RADEONResetVideo(pScrn);
	pPriv->video_stream_active = FALSE;
	if(pPriv->msp3430 != NULL) xf86_MSP3430SetVolume(pPriv->msp3430, MSP3430_FAST_MUTE);
	if(pPriv->i2c != NULL) RADEON_board_setmisc(pPriv);
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
RADEONSetPortAttribute(
  ScrnInfoPtr pScrn,
  Atom attribute,
  INT32 value,
  pointer data
){
  RADEONPortPrivPtr pPriv = (RADEONPortPrivPtr)data;

#define RTFSaturation(a)   (1.0 + ((a)*1.0)/1000.0)
#define RTFBrightness(a)   (((a)*1.0)/2000.0)
#define RTFContrast(a)   (1.0 + ((a)*1.0)/1000.0)
#define RTFHue(a)   (((a)*3.1416)/1000.0)

  if(attribute == xv_autopaint_colorkey) {
  	pPriv->autopaint_colorkey = value;
  } else
  if(attribute == xv_set_defaults) {
        RADEONSetPortAttribute(pScrn, xv_autopaint_colorkey, TRUE, data);
        RADEONSetPortAttribute(pScrn, xvBrightness, 0, data);
        RADEONSetPortAttribute(pScrn, xvSaturation, 0, data);
        RADEONSetPortAttribute(pScrn, xvContrast,   0, data);
        RADEONSetPortAttribute(pScrn, xvHue,   0, data);

        RADEONSetPortAttribute(pScrn, xvDecBrightness, 0, data);
        RADEONSetPortAttribute(pScrn, xvDecSaturation, 0, data);
        RADEONSetPortAttribute(pScrn, xvDecContrast,   0, data);
        RADEONSetPortAttribute(pScrn, xvDecHue,   0, data);

        RADEONSetPortAttribute(pScrn, xvVolume,   0, data);
        RADEONSetPortAttribute(pScrn, xvMute,   1, data);
        RADEONSetPortAttribute(pScrn, xvDoubleBuffer,   0, data);
  } else
  if(attribute == xvBrightness) {
	pPriv->brightness = value;
	RADEONSetTransform(pScrn, RTFBrightness(pPriv->brightness), RTFContrast(pPriv->contrast), 
		RTFSaturation(pPriv->saturation), RTFHue(pPriv->hue), pPriv->transform_index);
  } else
  if((attribute == xvSaturation) || (attribute == xvColor)) {
  	if(value<-1000)value = -1000;
	if(value>1000)value = 1000;
	pPriv->saturation = value;
	RADEONSetTransform(pScrn, RTFBrightness(pPriv->brightness), RTFContrast(pPriv->contrast), 
		RTFSaturation(pPriv->saturation), RTFHue(pPriv->hue), pPriv->transform_index);
  } else
  if(attribute == xvContrast) {
	pPriv->contrast = value;
	RADEONSetTransform(pScrn, RTFBrightness(pPriv->brightness), RTFContrast(pPriv->contrast), 
		RTFSaturation(pPriv->saturation), RTFHue(pPriv->hue), pPriv->transform_index);
  } else
  if(attribute == xvHue) {
	pPriv->hue = value;
	RADEONSetTransform(pScrn, RTFBrightness(pPriv->brightness), RTFContrast(pPriv->contrast), 
		RTFSaturation(pPriv->saturation), RTFHue(pPriv->hue), pPriv->transform_index);
  } else
  if(attribute == xvDecBrightness) {
	pPriv->dec_brightness = value;
	if(pPriv->theatre!=NULL) xf86_RT_SetBrightness(pPriv->theatre, pPriv->dec_brightness);	
  } else
  if((attribute == xvDecSaturation) || (attribute == xvDecColor)) {
  	if(value<-1000)value = -1000;
	if(value>1000)value = 1000;
	pPriv->dec_saturation = value;
	if(pPriv->theatre != NULL)xf86_RT_SetSaturation(pPriv->theatre, value);
  } else
  if(attribute == xvDecContrast) {
	pPriv->dec_contrast = value;
	if(pPriv->theatre != NULL)xf86_RT_SetContrast(pPriv->theatre, value);
  } else
  if(attribute == xvDecHue) {
	pPriv->dec_hue = value;
	if(pPriv->theatre != NULL)xf86_RT_SetTint(pPriv->theatre, value);
  } else
  if(attribute == xvDoubleBuffer) {
	if((value < 0) || (value > 1))
	   return BadValue;
	pPriv->doubleBuffer = value;
  } else
  if(attribute == xvColorKey) {
	pPriv->colorKey = value;
	RADEONSetColorKey(pScrn, pPriv->colorKey);

	REGION_EMPTY(pScrn->pScreen, &pPriv->clip);
  } else 
  if(attribute == xvEncoding) {
	pPriv->encoding = value;
	if(pPriv->video_stream_active)
	{
	   if(pPriv->theatre != NULL) RADEON_RT_SetEncoding(pPriv);
	   if(pPriv->msp3430 != NULL) RADEON_MSP_SetEncoding(pPriv);
	   if(pPriv->i2c != NULL) RADEON_board_setmisc(pPriv);
	/* put more here to actually change it */
	}
  } else 
  if(attribute == xvFrequency) {
        pPriv->frequency = value;
  	if(pPriv->fi1236 != NULL) xf86_FI1236_tune(pPriv->fi1236, value);
/*        if(pPriv->theatre != NULL) RADEON_RT_SetEncoding(pPriv);  */
	if((pPriv->msp3430 != NULL) && (pPriv->msp3430->recheck))
		xf86_InitMSP3430(pPriv->msp3430);
  } else 
  if(attribute == xvMute) {
        pPriv->mute = value;
        if(pPriv->msp3430 != NULL) xf86_MSP3430SetVolume(pPriv->msp3430, pPriv->mute ? MSP3430_FAST_MUTE : pPriv->volume);
	if(pPriv->i2c != NULL) RADEON_board_setmisc(pPriv);
  } else 
  if(attribute == xvVolume) {
  	if(value<0x01)value = 0x01;
	if(value>0x7f)value = 0x7F;
        pPriv->volume = value;	
	pPriv->mute = FALSE;
        if(pPriv->msp3430 != NULL) xf86_MSP3430SetVolume(pPriv->msp3430, value);
	if(pPriv->i2c != NULL) RADEON_board_setmisc(pPriv);
  } else 
     return BadMatch;

  return Success;
}

static int
RADEONGetPortAttribute(
  ScrnInfoPtr pScrn,
  Atom attribute,
  INT32 *value,
  pointer data
){
  RADEONPortPrivPtr pPriv = (RADEONPortPrivPtr)data;

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
  if(attribute == xvDecBrightness) {
	*value = pPriv->dec_brightness;
  } else
  if((attribute == xvDecSaturation) || (attribute == xvDecColor)) {
	*value = pPriv->dec_saturation;
  } else
  if(attribute == xvDecContrast) {
	*value = pPriv->dec_contrast;
  } else
  if(attribute == xvDecHue) {
	*value = pPriv->dec_hue;
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
RADEONQueryBestSize(
  ScrnInfoPtr pScrn,
  Bool motion,
  short vid_w, short vid_h,
  short drw_w, short drw_h,
  unsigned int *p_w, unsigned int *p_h,
  pointer data
){
   if(vid_w > (drw_w << 4))
	drw_w = vid_w >> 4;
   if(vid_h > (drw_h << 4))
	drw_h = vid_h >> 4;
	
  *p_w = drw_w;
  *p_h = drw_h;
}


static void
RADEONCopyData(
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
RADEONCopyMungedData(
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
	dst = (pointer)dst1;
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
RADEONAllocateMemory(
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

void RADEONSetColorKey(ScrnInfoPtr pScrn, CARD32 pixel)
{
    RADEONInfoPtr info = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    CARD8 R, G, B;

    R = (pixel & pScrn->mask.red) >> pScrn->offset.red;
    G = (pixel & pScrn->mask.green) >> pScrn->offset.green;
    B = (pixel & pScrn->mask.blue) >> pScrn->offset.blue;

    RADEONWaitForFifo(pScrn, 2);
    if(pScrn->offset.green == 5)
    {  /* 5.6.5 mode, 
          note that these values depend on DAC_CNTL.EXPAND_MODE setting */
       R = (R<<3);
       G = (G<<2);
       B = (B<<3);
       OUTREG(RADEON_OV0_GRAPHICS_KEY_CLR_HIGH, ((R | 0x7)<<16) | ((G | 0x3) <<8) | (B | 0x7) | (0xff << 24));
    } else 
    {
       OUTREG(RADEON_OV0_GRAPHICS_KEY_CLR_HIGH, ((R)<<16) | ((G) <<8) | (B) | (0xff << 24));
    }
    OUTREG(RADEON_OV0_GRAPHICS_KEY_CLR_LOW, (R<<16) | (G<<8) | (B) | (0x00 << 24));
}

static void
RADEONDisplayVideo(
    ScrnInfoPtr pScrn,
    RADEONPortPrivPtr pPriv, 
    int id,
    int offset1, int offset2,
    short width, short height,
    int pitch,
    int left, int right, int top,
    BoxPtr dstBox,
    short src_w, short src_h,
    short drw_w, short drw_h
){
    RADEONInfoPtr info = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    int v_inc, h_inc, step_by, tmp;
    int p1_h_accum_init, p23_h_accum_init;
    int p1_v_accum_init;

    /* Unlike older Mach64 chips, RADEON has only two ECP settings: 0 for PIXCLK < 175Mhz, and 1 (divide by 2)
       for higher clocks, sure makes life nicer 
       
       Here we need to find ecp_div again, as the user may have switched resolutions */
    if(info->ModeReg.dot_clock_freq < 17500) pPriv->ecp_div = 0;
    	   else pPriv->ecp_div = 1;
 
    OUTPLL(RADEON_VCLK_ECP_CNTL, (INPLL(pScrn, RADEON_VCLK_ECP_CNTL) & 0xfffffCff) | (pPriv->ecp_div << 8));

    v_inc = (src_h << (20
		+ ((pScrn->currentMode->Flags & V_INTERLACE)?1:0)
		- ((pScrn->currentMode->Flags & V_DBLSCAN)?1:0))) / drw_h;
    h_inc = ((src_w << (12
    		+ pPriv->ecp_div)) / drw_w);
    step_by = 1;

    while(h_inc >= (2 << 12)) {
	step_by++;
	h_inc >>= 1;
    }

    /* keep everything in 16.16 */

    offset1 += ((left >> 16) & ~7) << 1;
    offset2 += ((left >> 16) & ~7) << 1;

    tmp = (left & 0x0003ffff) + 0x00028000 + (h_inc << 3);
    p1_h_accum_init = ((tmp <<  4) & 0x000f8000) |
		      ((tmp << 12) & 0xf0000000);

    tmp = ((left >> 1) & 0x0001ffff) + 0x00028000 + (h_inc << 2);
    p23_h_accum_init = ((tmp <<  4) & 0x000f8000) |
		       ((tmp << 12) & 0x70000000);

    tmp = (top & 0x0000ffff) + 0x00018000;
    p1_v_accum_init = ((tmp << 4) & 0x03ff8000) | 0x00000001;

    left = (left >> 16) & 7;


    RADEONWaitForFifo(pScrn, 2);
    OUTREG(RADEON_OV0_REG_LOAD_CNTL, 1);
    RADEONWaitForIdle(pScrn);
    while(!(INREG(RADEON_OV0_REG_LOAD_CNTL) & (1 << 3)));

    RADEONWaitForFifo(pScrn, 15);
    OUTREG(RADEON_OV0_H_INC, h_inc | ((h_inc >> 1) << 16));
    OUTREG(RADEON_OV0_STEP_BY, step_by | (step_by << 8));
    OUTREG(RADEON_OV0_Y_X_START, dstBox->x1 | ((dstBox->y1*((pScrn->currentMode->Flags & V_DBLSCAN)?2:1)) << 16));
    OUTREG(RADEON_OV0_Y_X_END,   dstBox->x2 | ((dstBox->y2*((pScrn->currentMode->Flags & V_DBLSCAN)?2:1)) << 16));
    OUTREG(RADEON_OV0_V_INC, v_inc);
    OUTREG(RADEON_OV0_P1_BLANK_LINES_AT_TOP, 0x00000fff | ((src_h - 1) << 16));
    OUTREG(RADEON_OV0_VID_BUF_PITCH0_VALUE, pitch);
    OUTREG(RADEON_OV0_VID_BUF_PITCH1_VALUE, pitch);
    OUTREG(RADEON_OV0_P1_X_START_END, (src_w + left - 1) | (left << 16));
    left >>= 1; src_w >>= 1;
    OUTREG(RADEON_OV0_P2_X_START_END, (src_w + left - 1) | (left << 16));
    OUTREG(RADEON_OV0_P3_X_START_END, (src_w + left - 1) | (left << 16));
    OUTREG(RADEON_OV0_VID_BUF0_BASE_ADRS, offset1 & 0xfffffff0);
    OUTREG(RADEON_OV0_VID_BUF1_BASE_ADRS, offset2 & 0xfffffff0);
    OUTREG(RADEON_OV0_VID_BUF2_BASE_ADRS, offset1 & 0xfffffff0);

    RADEONWaitForFifo(pScrn, 9);
    OUTREG(RADEON_OV0_VID_BUF3_BASE_ADRS, offset2 & 0xfffffff0);
    OUTREG(RADEON_OV0_VID_BUF4_BASE_ADRS, offset1 & 0xfffffff0);
    OUTREG(RADEON_OV0_VID_BUF5_BASE_ADRS, offset2 & 0xfffffff0);
    OUTREG(RADEON_OV0_P1_V_ACCUM_INIT, p1_v_accum_init);
    OUTREG(RADEON_OV0_P1_H_ACCUM_INIT, p1_h_accum_init);
    OUTREG(RADEON_OV0_P23_H_ACCUM_INIT, p23_h_accum_init);

/*  older, original magic, for reference. Note that it is wrong, as register bits have changed since r128.
    if(id == FOURCC_UYVY)
       OUTREG(RADEON_OV0_SCALE_CNTL, 0x41008C03);
    else
       OUTREG(RADEON_OV0_SCALE_CNTL, 0x41008B03);
*/

    if(id == FOURCC_UYVY)
       OUTREG(RADEON_OV0_SCALE_CNTL, RADEON_SCALER_SOURCE_YVYU422 \
       	       | RADEON_SCALER_ADAPTIVE_DEINT \
	       | RADEON_SCALER_SMART_SWITCH \
	       | RADEON_SCALER_DOUBLE_BUFFER \
	       | RADEON_SCALER_ENABLE);
    else
       OUTREG(RADEON_OV0_SCALE_CNTL,  RADEON_SCALER_SOURCE_VYUY422 \
       	       | RADEON_SCALER_ADAPTIVE_DEINT \
	       | RADEON_SCALER_SMART_SWITCH \
	       | RADEON_SCALER_DOUBLE_BUFFER \
	       | RADEON_SCALER_ENABLE);

    OUTREG(RADEON_OV0_REG_LOAD_CNTL, 0);
}


static int
RADEONPutImage(
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
   RADEONInfoPtr info = RADEONPTR(pScrn);
   RADEONPortPrivPtr pPriv = (RADEONPortPrivPtr)data;
   unsigned char *RADEONMMIO = info->MMIO;
   INT32 xa, xb, ya, yb;
   unsigned char *dst_start;
   int pitch, new_size, offset, s2offset, s3offset;
   int srcPitch, srcPitch2, dstPitch;
   int top, left, npixels, nlines, bpp;
   BoxRec dstBox;
   CARD32 tmp;

   /* if capture was active shutdown it first */
   if(pPriv->video_stream_active)
   {
	RADEONWaitForFifo(pScrn, 2); 
        OUTREG(RADEON_FCP_CNTL, RADEON_FCP_CNTL__GND);
	OUTREG(RADEON_CAP0_TRIG_CNTL, 0);
	pPriv->video_stream_active = FALSE;
	if(pPriv->msp3430 != NULL) xf86_MSP3430SetVolume(pPriv->msp3430, MSP3430_FAST_MUTE);
   }

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

   if(!RADEONClipVideo(&dstBox, &xa, &xb, &ya, &yb, clipBoxes, width, height))
	return Success;

   dstBox.x1 -= pScrn->frameX0;
   dstBox.x2 -= pScrn->frameX0;
   dstBox.y1 -= pScrn->frameY0;
   dstBox.y2 -= pScrn->frameY0;

   bpp = pScrn->bitsPerPixel >> 3;
   pitch = bpp * pScrn->displayWidth;

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

   if(!(pPriv->linear = RADEONAllocateMemory(pScrn, pPriv->linear,
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
   dst_start = info->FB + offset;

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
	RADEONCopyMungedData(buf + (top * srcPitch) + left, buf + s2offset,
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
	RADEONCopyData(buf, dst_start, srcPitch, dstPitch, nlines, npixels);
	break;
    }


    /* update cliplist */
    if(!RegionsEqual(&pPriv->clip, clipBoxes)) {
	REGION_COPY(pScreen, &pPriv->clip, clipBoxes);
	/* draw these */
	if(pPriv->autopaint_colorkey)(*info->accel->FillSolidRects)(pScrn, pPriv->colorKey, GXcopy,
					(CARD32)~0,
					REGION_NUM_RECTS(clipBoxes),
					REGION_RECTS(clipBoxes));
    }

    RADEONDisplayVideo(pScrn, pPriv, id, offset, offset, width, height, dstPitch,
		     xa, xb, ya, &dstBox, src_w, src_h, drw_w, drw_h);

    pPriv->videoStatus = CLIENT_VIDEO_ON;

    info->VideoTimerCallback = RADEONVideoTimerCallback;

    return Success;
}


static int
RADEONQueryImageAttributes(
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

void RADEON_board_setmisc(RADEONPortPrivPtr pPriv)
{
    /* Adjust PAL/SECAM constants for FI1216MF tuner */
    if((((pPriv->board_info & 0xf)==5) ||
        ((pPriv->board_info & 0xf)==11)||
        ((pPriv->board_info & 0xf)==14))
	&& (pPriv->fi1236!=NULL))
    {
        if((pPriv->encoding>=1)&&(pPriv->encoding<=3)) /*PAL*/
	{
    	   pPriv->fi1236->parm.band_low = 0xA1;
	   pPriv->fi1236->parm.band_mid = 0x91;
	   pPriv->fi1236->parm.band_high = 0x31;
	}
        if((pPriv->encoding>=7)&&(pPriv->encoding<=9)) /*SECAM*/
	{
    	   pPriv->fi1236->parm.band_low = 0xA3;
	   pPriv->fi1236->parm.band_mid = 0x93;
	   pPriv->fi1236->parm.band_high = 0x33;
	}
    }
}

void RADEON_RT_SetEncoding(RADEONPortPrivPtr pPriv)
{
switch(pPriv->encoding){
	case 1:
		xf86_RT_SetStandard(pPriv->theatre,DEC_PAL | extPAL);
		xf86_RT_SetConnector(pPriv->theatre,DEC_COMPOSITE, 0);
		pPriv->v=24;
		break;
	case 2:
		xf86_RT_SetStandard(pPriv->theatre,DEC_PAL | extPAL);
		xf86_RT_SetConnector(pPriv->theatre,DEC_TUNER,0);
		pPriv->v=24;
		break;
	case 3:
		xf86_RT_SetStandard(pPriv->theatre,DEC_PAL | extPAL);
		xf86_RT_SetConnector(pPriv->theatre,DEC_SVIDEO,0);
		pPriv->v=24;
		break;
	case 4:
		xf86_RT_SetStandard(pPriv->theatre,DEC_NTSC | extNONE);
		xf86_RT_SetConnector(pPriv->theatre, DEC_COMPOSITE,0);
		pPriv->v=23;
		break;
	case 5:
		xf86_RT_SetStandard(pPriv->theatre,DEC_NTSC | extNONE);
		xf86_RT_SetConnector(pPriv->theatre, DEC_TUNER, 0);
		pPriv->v=23;
		break;
	case 6:
		xf86_RT_SetStandard(pPriv->theatre,DEC_NTSC | extNONE);
		xf86_RT_SetConnector(pPriv->theatre, DEC_SVIDEO, 0);
		pPriv->v=23;
		break;
	case 7:
		xf86_RT_SetStandard(pPriv->theatre,DEC_SECAM | extNONE);
		xf86_RT_SetConnector(pPriv->theatre, DEC_COMPOSITE, 0);
		pPriv->v=25;
		break;
	case 8:
		xf86_RT_SetStandard(pPriv->theatre,DEC_SECAM | extNONE);
		xf86_RT_SetConnector(pPriv->theatre, DEC_TUNER, 0);
		pPriv->v=25;
		break;
	case 9:
		xf86_RT_SetStandard(pPriv->theatre,DEC_SECAM | extNONE);
		xf86_RT_SetConnector(pPriv->theatre, DEC_SVIDEO, 0);
		pPriv->v=25;
		break;
	case 10:
		xf86_RT_SetStandard(pPriv->theatre,DEC_PAL | extPAL_60);
		xf86_RT_SetConnector(pPriv->theatre,DEC_COMPOSITE, 0);
		pPriv->v=24;
		break;
	case 11:
		xf86_RT_SetStandard(pPriv->theatre,DEC_PAL | extPAL_60);
		xf86_RT_SetConnector(pPriv->theatre,DEC_TUNER,0);
		pPriv->v=24;
		break;
	case 12:
		xf86_RT_SetStandard(pPriv->theatre,DEC_PAL | extPAL_60);
		xf86_RT_SetConnector(pPriv->theatre,DEC_SVIDEO,0);
		pPriv->v=24;
		break;
	default:
	        pPriv->v=0;
		return;
	}	
}

void RADEON_MSP_SetEncoding(RADEONPortPrivPtr pPriv)
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
xf86DrvMsg(0, X_INFO, "test--\n");
xf86_InitMSP3430(pPriv->msp3430);
xf86_MSP3430SetVolume(pPriv->msp3430, pPriv->mute ? MSP3430_FAST_MUTE : pPriv->volume);
}

/* capture config constants */
#define BUF_TYPE_FIELD		0
#define BUF_TYPE_ALTERNATING	1
#define BUF_TYPE_FRAME		2


#define BUF_MODE_SINGLE		0
#define BUF_MODE_DOUBLE		1
#define BUF_MODE_TRIPLE		2
/* CAP0_CONFIG values */

#define FORMAT_BROOKTREE	0
#define FORMAT_CCIR656		1
#define FORMAT_ZV		2
#define FORMAT_VIP16		3
#define FORMAT_TRANSPORT	4

#define ENABLE_RADEON_CAPTURE_WEAVE (RADEON_CAP0_CONFIG_CONTINUOS \
			| (BUF_MODE_SINGLE <<7) \
			| (BUF_TYPE_FRAME << 4) \
			| ( (pPriv->theatre !=NULL)?(FORMAT_CCIR656<<23):(FORMAT_BROOKTREE<<23)) \
			| RADEON_CAP0_CONFIG_HORZ_DECIMATOR \
			| RADEON_CAP0_CONFIG_VIDEO_IN_VYUY422)

#define ENABLE_RADEON_CAPTURE_BOB (RADEON_CAP0_CONFIG_CONTINUOS \
			| (BUF_MODE_SINGLE <<7)  \
			| (BUF_TYPE_ALTERNATING << 4) \
			| ( (pPriv->theatre !=NULL)?(FORMAT_CCIR656<<23):(FORMAT_BROOKTREE<<23)) \
			| RADEON_CAP0_CONFIG_HORZ_DECIMATOR \
			| RADEON_CAP0_CONFIG_VIDEO_IN_VYUY422)

static int
RADEONPutVideo(
  ScrnInfoPtr pScrn,
  short src_x, short src_y,
  short drw_x, short drw_y,
  short src_w, short src_h,
  short drw_w, short drw_h,
  RegionPtr clipBoxes, pointer data
){
   RADEONInfoPtr info = RADEONPTR(pScrn);
   RADEONPortPrivPtr pPriv = (RADEONPortPrivPtr)data;
   unsigned char *RADEONMMIO = info->MMIO;
   INT32 xa, xb, ya, yb, top;
   int pitch, new_size, offset1, offset2, s2offset, s3offset;
   int srcPitch, srcPitch2, dstPitch;
   int bpp;
   BoxRec dstBox;
   CARD32 id, display_base;
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

   width = InputVideoEncodings[pPriv->encoding].width;
   height = InputVideoEncodings[pPriv->encoding].height;
        
   if(!RADEONClipVideo(&dstBox, &xa, &xb, &ya, &yb, clipBoxes, width, height))
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

   new_size = new_size + 0x1f; /* for aligning */

   if(!(pPriv->linear = RADEONAllocateMemory(pScrn, pPriv->linear, new_size*2)))
   {
	return BadAlloc;
   }

   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "PutVideo Checkpoint 1\n");

/* I have suspicion that capture engine must be active _before_ Rage Theatre
   is being manipulated with.. */

   
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "PutVideo Checkpoint 2\n");

   RADEONWaitForIdle(pScrn);
   display_base=INREG(RADEON_DISPLAY_BASE_ADDR);   

/*   RADEONWaitForFifo(pScrn, 15); */

   offset1 = (pPriv->linear->offset*bpp+0xf) & (~0xf);
   offset2 = ((pPriv->linear->offset+new_size)*bpp + 0x1f) & (~0xf);

   OUTREG(RADEON_CAP0_BUF0_OFFSET, offset1+display_base);
   OUTREG(RADEON_CAP0_BUF0_EVEN_OFFSET, offset2+display_base);
   OUTREG(RADEON_CAP0_ONESHOT_BUF_OFFSET, offset1+display_base);
   OUTREG(RADEON_CAP0_BUF1_OFFSET, offset1+display_base);
   OUTREG(RADEON_CAP0_BUF1_EVEN_OFFSET, offset2+display_base);
   
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "PutVideo Checkpoint 3\n");

   OUTREG(RADEON_CAP0_BUF_PITCH, width*2);
   OUTREG(RADEON_CAP0_H_WINDOW, (2*width)<<16);
   OUTREG(RADEON_CAP0_V_WINDOW, (((height)+pPriv->v-1)<<16)|(pPriv->v));
   OUTREG(RADEON_CAP0_CONFIG, ENABLE_RADEON_CAPTURE_BOB);
   OUTREG(RADEON_CAP0_DEBUG, 0);
   
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "PutVideo Checkpoint 4\n");

   OUTREG(RADEON_VID_BUFFER_CONTROL, (1<<16) | 0x01);
   OUTREG(RADEON_TEST_DEBUG_CNTL, 0);
   
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "PutVideo Checkpoint 5\n");

   
   if(! pPriv->video_stream_active)
   {

      RADEONWaitForIdle(pScrn);
      xf86DrvMsg(pScrn->scrnIndex, X_INFO, "RADEON_VIDEOMUX_CNT=0x%08x\n", INREG(RADEON_VIDEOMUX_CNTL));
      OUTREG(RADEON_VIDEOMUX_CNTL, INREG(RADEON_VIDEOMUX_CNTL)|1 ); 
      OUTREG(RADEON_CAP0_PORT_MODE_CNTL, (pPriv->theatre!=NULL)? 1: 0);
/* Rage128 setting seems to induce a lockup. What gives ? 
   we need this to activate capture unit 

   Explanation: FCP_CNTL is now apart from PLL registers   
*/   

      OUTREG(RADEON_FCP_CNTL, RADEON_FCP_CNTL__PCLK);
      OUTREG(RADEON_CAP0_TRIG_CNTL, 0x11);
      if(pPriv->theatre != NULL) 
      {
         xf86_RT_SetInterlace(pPriv->theatre, 1);
         RADEON_RT_SetEncoding(pPriv); 
         xf86_RT_SetOutputVideoSize(pPriv->theatre, width, height*2, 0, 0);   
      }
      if(pPriv->msp3430 != NULL) RADEON_MSP_SetEncoding(pPriv);
      if(pPriv->i2c != NULL)RADEON_board_setmisc(pPriv);
   }

   
   /* update cliplist */
   if(!RegionsEqual(&pPriv->clip, clipBoxes)) {
	REGION_COPY(pScreen, &pPriv->clip, clipBoxes);
	/* draw these */
	if(pPriv->autopaint_colorkey)(*info->accel->FillSolidRects)(pScrn, pPriv->colorKey, GXcopy, ~0,
					REGION_NUM_RECTS(clipBoxes),
					REGION_RECTS(clipBoxes));
   }

   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "PutVideo Checkpoint 6\n");

   RADEONDisplayVideo(pScrn, pPriv, id, offset1+top*srcPitch, offset2+top*srcPitch, width, height, dstPitch,
		     xa, xb, ya, &dstBox, src_w, src_h, drw_w, drw_h);

   RADEONWaitForFifo(pScrn, 1);
   OUTREG(RADEON_OV0_REG_LOAD_CNTL,  RADEON_REG_LD_CTL_LOCK);
   RADEONWaitForIdle(pScrn);
   while(!(INREG(RADEON_OV0_REG_LOAD_CNTL) & RADEON_REG_LD_CTL_LOCK_READBACK));

   OUTREG(RADEON_OV0_AUTO_FLIP_CNTL, RADEON_OV0_AUTO_FLIP_CNTL_SOFT_BUF_ODD);

   OUTREG(RADEON_OV0_DEINTERLACE_PATTERN, 0xAAAAA);
   
   RADEONWaitForIdle(pScrn);
   OUTREG (RADEON_OV0_AUTO_FLIP_CNTL, (INREG (RADEON_OV0_AUTO_FLIP_CNTL) ^ RADEON_OV0_AUTO_FLIP_CNTL_SOFT_EOF_TOGGLE ));
   OUTREG (RADEON_OV0_AUTO_FLIP_CNTL, (INREG (RADEON_OV0_AUTO_FLIP_CNTL) ^ RADEON_OV0_AUTO_FLIP_CNTL_SOFT_EOF_TOGGLE ));

   OUTREG(RADEON_OV0_REG_LOAD_CNTL, 0);

   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "OV0_FLAG_CNTL=0x%08x\n", INREG(RADEON_OV0_FLAG_CNTL));
/*   OUTREG(RADEON_OV0_FLAG_CNTL, 8); */
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "OV0_VID_BUFFER_CNTL=0x%08x\n", INREG(RADEON_VID_BUFFER_CONTROL));
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CAP0_BUF_STATUS=0x%08x\n", INREG(RADEON_CAP0_BUF_STATUS));

/*   OUTREG(RADEON_OV0_SCALE_CNTL, 0x417f1B00); */

   pPriv->videoStatus = CLIENT_VIDEO_ON;
   pPriv->video_stream_active = TRUE;

   info->VideoTimerCallback = RADEONVideoTimerCallback;

   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "PutVideo Checkpoint 7\n");

   return Success;
}


static void
RADEONVideoTimerCallback(ScrnInfoPtr pScrn, Time now)
{
    RADEONInfoPtr info = RADEONPTR(pScrn);
    RADEONPortPrivPtr pPriv = info->adaptor->pPortPrivates[0].ptr;

    if(pPriv->videoStatus & TIMER_MASK) {
	if(pPriv->videoStatus & OFF_TIMER) {
	    if(pPriv->offTime < now) {
		unsigned char *RADEONMMIO = info->MMIO;
                RADEONWaitForFifo(pScrn, 2);
		OUTREG(RADEON_OV0_SCALE_CNTL, 0);
		pPriv->videoStatus = FREE_TIMER;
		pPriv->freeTime = now + FREE_DELAY;
	    }
	} else {  /* FREE_TIMER */
	    if(pPriv->freeTime < now) {
		if(pPriv->linear) {
		   xf86FreeOffscreenLinear(pPriv->linear);
		   pPriv->linear = NULL;
		}
		pPriv->videoStatus = 0;
		info->VideoTimerCallback = NULL;
	    }
	}
    } else  /* shouldn't get here */
	info->VideoTimerCallback = NULL;
}


#endif  /* !XvExtension */
