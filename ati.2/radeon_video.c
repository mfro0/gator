/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/ati/radeon_video.c,v 1.20 2002/11/01 06:08:36 keithp Exp $ */

#include "radeon.h"
#include "radeon_reg.h"

#include "xf86.h"
#include "dixstruct.h"

#include "Xv.h"
#include "fourcc.h"

#include "xf86PciInfo.h"

#include "xf86i2c.h"
#include "fi1236.h"
#include "msp3430.h"
#include "generic_bus.h"
#include "theatre_reg.h"
#include "theatre.h"
#include "i2c_def.h"
#include "tda9885.h"
#include "saa7114.h"

#define OFF_DELAY       250  /* milliseconds */
#define FREE_DELAY      15000

#define OFF_TIMER       0x01
#define FREE_TIMER      0x02
#define CLIENT_VIDEO_ON 0x04

#define TIMER_MASK      (OFF_TIMER | FREE_TIMER)

#ifndef XvExtension
void RADEONInitVideo(ScreenPtr pScreen) {}
#else

static void RADEONInitOffscreenImages(ScreenPtr);

static XF86VideoAdaptorPtr RADEONSetupImageVideo(ScreenPtr);
static int  RADEONSetPortAttribute(ScrnInfoPtr, Atom, INT32, pointer);
static int  RADEONGetPortAttribute(ScrnInfoPtr, Atom ,INT32 *, pointer);
static void RADEONStopVideo(ScrnInfoPtr, pointer, Bool);
static void RADEONQueryBestSize(ScrnInfoPtr, Bool, short, short, short, short,
			unsigned int *, unsigned int *, pointer);
static int  RADEONPutImage(ScrnInfoPtr, short, short, short, short, short,
			short, short, short, int, unsigned char*, short,
			short, Bool, RegionPtr, pointer);
static int  RADEONQueryImageAttributes(ScrnInfoPtr, int, unsigned short *,
			unsigned short *,  int *, int *);


static void RADEONResetVideo(ScrnInfoPtr);

static void RADEONVideoTimerCallback(ScrnInfoPtr pScrn, Time now);
static int RADEONPutVideo(ScrnInfoPtr pScrn, short src_x, short src_y, short drw_x, short drw_y,
                        short src_w, short src_h, short drw_w, short drw_h, 
			RegionPtr clipBoxes, pointer data);
  
#define MAKE_ATOM(a) MakeAtom(a, sizeof(a) - 1, TRUE)

static Atom xvBrightness, xvColorKey, xvSaturation, xvDoubleBuffer;
static Atom xvRedIntensity, xvGreenIntensity, xvBlueIntensity;
static Atom xvContrast, xvHue, xvColor, xvAutopaintColorkey, xvSetDefaults,
	    xvEncoding, xvFrequency, xvVolume, xvMute,
	     xvDecBrightness, xvDecContrast, xvDecHue, xvDecColor, xvDecSaturation,
	     xvTunerStatus, xvSAP, xvOverlayDeinterlacingMethod,
	     xvLocationID, xvDeviceID, xvInstanceID;

typedef struct
{
    BYTE table_revision;
    BYTE table_size;
    BYTE tuner_type;
    BYTE audio_chip;
    BYTE product_id;
    BYTE tuner_voltage_teletext_fm;
    BYTE i2s_config; /* configuration of the sound chip */
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
   int           red_intensity;
   int           green_intensity;
   int           blue_intensity;
   int		 ecp_div;   

   int           dec_brightness;
   int           dec_saturation;
   int           dec_hue;
   int           dec_contrast;

   Bool          doubleBuffer;
   unsigned char currentBuffer;
   RegionRec     clip;
   CARD32        colorKey;
   CARD32        videoStatus;
   Time          offTime;
   Time          freeTime;
   Bool          autopaint_colorkey;

   I2CBusPtr     i2c;
   CARD32        radeon_i2c_timing;
   CARD32        radeon_M;
   CARD32        radeon_N;
   CARD32        i2c_status;
   CARD32        i2c_cntl;
   
   FI1236Ptr     fi1236;
   MSP3430Ptr    msp3430;
   TDA9885Ptr    tda9885;
   SAA7114Ptr    saa7114;
   
   GENERIC_BUS_Ptr  VIP;
   TheatrePtr       theatre;
   
   Bool          video_stream_active;
   int           encoding;
   CARD32        frequency;
   int           volume;
   Bool          mute;
   int           sap_channel;
   int           v;

#define METHOD_BOB      0
#define METHOD_SINGLE   1
#define METHOD_WEAVE    2
#define METHOD_ADAPTIVE 3

   int           overlay_deinterlacing_method;
   
   int           capture_vbi_data;

   Bool          MM_TABLE_valid;
   _MM_TABLE     MM_TABLE;

   Bool          EEPROM_present;
   int           EEPROM_addr;

   Bool          addon_board;
   CARD8         board_info;
   int           board_control;
   Atom          device_id, location_id, instance_id;
} RADEONPortPrivRec, *RADEONPortPrivPtr;

void RADEON_RT_SetEncoding(RADEONPortPrivPtr pPriv);
void RADEON_board_setmisc(RADEONPortPrivPtr pPriv);
void RADEON_MSP_SetEncoding(RADEONPortPrivPtr pPriv);
void RADEONSetColorKey(ScrnInfoPtr pScrn, CARD32 pixel);
static void RADEONResetI2C(ScrnInfoPtr pScrn, RADEONPortPrivPtr pPriv);
void RADEONShutdownVideo(ScrnInfoPtr pScrn);
void RADEONVIP_reset(ScrnInfoPtr pScrn, RADEONPortPrivPtr pPriv);



#define GET_PORT_PRIVATE(pScrn) \
   (RADEONPortPrivPtr)((RADEONPTR(pScrn))->adaptor->pPortPrivates[0].ptr)

void RADEONInitVideo(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    RADEONInfoPtr info  = RADEONPTR(pScrn);
    XF86VideoAdaptorPtr *adaptors, *newAdaptors = NULL;
    XF86VideoAdaptorPtr newAdaptor = NULL;
    int num_adaptors;

    if(info->accel && info->accel->FillSolidRects) 
    {
	newAdaptor = RADEONSetupImageVideo(pScreen);
	RADEONInitOffscreenImages(pScreen);
    }
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

#define NUM_DEC_ATTRIBUTES 19+4+3
#define NUM_ATTRIBUTES 9+4+3

static XF86AttributeRec Attributes[NUM_DEC_ATTRIBUTES+1] =
{
   {             XvGettable, 0, ~0, "XV_DEVICE_ID"},
   {             XvGettable, 0, ~0, "XV_LOCATION_ID"},
   {             XvGettable, 0, ~0, "XV_INSTANCE_ID"},
   {XvSettable             , 0, 1, "XV_SET_DEFAULTS"},
   {XvSettable | XvGettable, 0, 1, "XV_AUTOPAINT_COLORKEY"},
   {XvSettable | XvGettable, 0, ~0, "XV_COLORKEY"},
   {XvSettable | XvGettable, 0, 1, "XV_DOUBLE_BUFFER"},
   {XvSettable | XvGettable, -1000, 1000, "XV_BRIGHTNESS"},
   {XvSettable | XvGettable, -1000, 1000, "XV_CONTRAST"},
   {XvSettable | XvGettable, -1000, 1000, "XV_SATURATION"},
   {XvSettable | XvGettable, -1000, 1000, "XV_COLOR"},
   {XvSettable | XvGettable, -1000, 1000, "XV_HUE"},
   {XvSettable | XvGettable, -1000, 1000, "XV_RED_INTENSITY"},
   {XvSettable | XvGettable, -1000, 1000, "XV_GREEN_INTENSITY"},
   {XvSettable | XvGettable, -1000, 1000, "XV_BLUE_INTENSITY"},
   {XvSettable | XvGettable, -1000, 1000, "XV_DEC_BRIGHTNESS"},
   {XvSettable | XvGettable, -1000, 1000, "XV_DEC_CONTRAST"},
   {XvSettable | XvGettable, -1000, 1000, "XV_DEC_SATURATION"},
   {XvSettable | XvGettable, -1000, 1000, "XV_DEC_HUE"},
   {XvSettable | XvGettable, 0, 2, "XV_OVERLAY_DEINTERLACING_METHOD"},
   {XvSettable | XvGettable, 0, 12, "XV_ENCODING"},
   {XvSettable | XvGettable, 0, -1, "XV_FREQ"},
   {XvGettable, -1000, 1000, "XV_TUNER_STATUS"},
   {XvSettable | XvGettable, -1000, 1000, "XV_VOLUME"},
   {XvSettable | XvGettable, 0, 1, "XV_MUTE"},
   {XvSettable | XvGettable, 0, 1, "XV_SAP"},
   { 0, 0, 0, NULL}  /* just a place holder so I don't have to be fancy with commas */
};

#define INCLUDE_RGB_FORMATS 1

#if INCLUDE_RGB_FORMATS

#define NUM_IMAGES 8

/* Note: GUIDs are bogus... - but nothing uses them anyway */

#define FOURCC_RGBA32   0x41424752

#define XVIMAGE_RGBA32(byte_order)   \
        { \
                FOURCC_RGBA32, \
                XvRGB, \
                byte_order, \
                { 'R', 'G', 'B', 'A', \
                  0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
                32, \
                XvPacked, \
                1, \
                32, 0x00FF0000, 0x0000FF00, 0x000000FF, \
                0, 0, 0, 0, 0, 0, 0, 0, 0, \
                {'A', 'R', 'G', 'B', \
                  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
                XvTopToBottom \
        }               

#define FOURCC_RGB24    0x00000000

#define XVIMAGE_RGB24(byte_order)   \
        { \
                FOURCC_RGB24, \
                XvRGB, \
                byte_order, \
                { 'R', 'G', 'B', 0, \
                  0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
                24, \
                XvPacked, \
                1, \
                24, 0x00FF0000, 0x0000FF00, 0x000000FF, \
                0, 0, 0, 0, 0, 0, 0, 0, 0, \
                { 'R', 'G', 'B', \
                  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
                XvTopToBottom \
        }

#define FOURCC_RGBT16   0x54424752

#define XVIMAGE_RGBT16(byte_order)   \
        { \
                FOURCC_RGBT16, \
                XvRGB, \
                byte_order, \
                { 'R', 'G', 'B', 'T', \
                  0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
                16, \
                XvPacked, \
                1, \
                16, 0x00007C00, 0x000003E0, 0x0000001F, \
                0, 0, 0, 0, 0, 0, 0, 0, 0, \
                {'A', 'R', 'G', 'B', \
                  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
                XvTopToBottom \
        }               

#define FOURCC_RGB16    0x32424752

#define XVIMAGE_RGB16(byte_order)   \
        { \
                FOURCC_RGB16, \
                XvRGB, \
                byte_order, \
                { 'R', 'G', 'B', 0x00, \
                  0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
                16, \
                XvPacked, \
                1, \
                16, 0x0000F800, 0x000007E0, 0x0000001F, \
                0, 0, 0, 0, 0, 0, 0, 0, 0, \
                {'R', 'G', 'B', \
                  0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
                XvTopToBottom \
        }               

static XF86ImageRec Images[NUM_IMAGES] =
{
#if X_BYTE_ORDER == X_BIG_ENDIAN
        XVIMAGE_RGBA32(MSBFirst),
        XVIMAGE_RGB24(MSBFirst),
        XVIMAGE_RGBT16(MSBFirst),
        XVIMAGE_RGB16(MSBFirst),
#else
        XVIMAGE_RGBA32(LSBFirst),
        XVIMAGE_RGB24(LSBFirst),
        XVIMAGE_RGBT16(LSBFirst),
        XVIMAGE_RGB16(LSBFirst),
#endif
        XVIMAGE_YUY2,
        XVIMAGE_UYVY,
        XVIMAGE_YV12,
        XVIMAGE_I420
};

#else

#define NUM_IMAGES 4

static XF86ImageRec Images[NUM_IMAGES] =
{
        XVIMAGE_YUY2,
        XVIMAGE_UYVY,
        XVIMAGE_YV12,
        XVIMAGE_I420
};

#endif

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
    float   RefLuma;
    float   RefRCb;
    float   RefRCr;
    float   RefGCb;
    float   RefGCr;
    float   RefBCb;
    float   RefBCr;
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
} GAMMA_SETTINGS;

/* Recommended gamma curve parameters */
GAMMA_SETTINGS def_gamma[18] = 
{
    {RADEON_OV0_GAMMA_000_00F, 0x100, 0x0000},
    {RADEON_OV0_GAMMA_010_01F, 0x100, 0x0020},
    {RADEON_OV0_GAMMA_020_03F, 0x100, 0x0040},
    {RADEON_OV0_GAMMA_040_07F, 0x100, 0x0080},
    {RADEON_OV0_GAMMA_080_0BF, 0x100, 0x0100},
    {RADEON_OV0_GAMMA_0C0_0FF, 0x100, 0x0100},
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
 *            red_intensity - intensity of red component                    *
 *            green_intensity - intensity of green component                *
 *            blue_intensity - intensity of blue component                  *
 *            ref - index to the table of refernce transforms               *
 *   Outputs: NONE                                                          *
 ****************************************************************************/

static void RADEONSetTransform (ScrnInfoPtr pScrn,
				float	    bright,
				float	    cont,
				float	    sat, 
				float	    hue,
				float	    red_intensity, 
				float	    green_intensity, 
				float	    blue_intensity,
				CARD32	    ref)
{
    RADEONInfoPtr    info = RADEONPTR(pScrn);
    unsigned char   *RADEONMMIO = info->MMIO;
    float	    OvHueSin, OvHueCos;
    float	    CAdjLuma, CAdjOff;
    float	    CAdjRCb, CAdjRCr;
    float	    CAdjGCb, CAdjGCr;
    float	    CAdjBCb, CAdjBCr;
    float	    RedAdj,GreenAdj,BlueAdj;
    float	    OvLuma, OvROff, OvGOff, OvBOff;
    float	    OvRCb, OvRCr;
    float	    OvGCb, OvGCr;
    float	    OvBCb, OvBCr;
    float	    Loff = 64.0;
    float	    Coff = 512.0f;

    CARD32	    dwOvLuma, dwOvROff, dwOvGOff, dwOvBOff;
    CARD32	    dwOvRCb, dwOvRCr;
    CARD32	    dwOvGCb, dwOvGCr;
    CARD32	    dwOvBCb, dwOvBCr;

    if (ref >= 2) 
	return;

    OvHueSin = sin(hue);
    OvHueCos = cos(hue);

    CAdjLuma = cont * trans[ref].RefLuma;
    CAdjOff = cont * trans[ref].RefLuma * bright * 1023.0;
    RedAdj = cont * trans[ref].RefLuma * red_intensity * 1023.0;
    GreenAdj = cont * trans[ref].RefLuma * green_intensity * 1023.0;
    BlueAdj = cont * trans[ref].RefLuma * blue_intensity * 1023.0;

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
    OvROff = RedAdj + CAdjOff -
    OvLuma * Loff - (OvRCb + OvRCr) * Coff;
    OvGOff = GreenAdj + CAdjOff - 
    OvLuma * Loff - (OvGCb + OvGCr) * Coff;
    OvBOff = BlueAdj + CAdjOff - 
    OvLuma * Loff - (OvBCb + OvBCr) * Coff;
#if 0 /* default constants */
    OvROff = -888.5;
    OvGOff = 545;
    OvBOff = -1104;
#endif 

    dwOvROff = ((INT32)(OvROff * 2.0)) & 0x1fff;
    dwOvGOff = ((INT32)(OvGOff * 2.0)) & 0x1fff;
    dwOvBOff = ((INT32)(OvBOff * 2.0)) & 0x1fff;
    /*
     * Whatever docs say about R200 having 3.8 format instead of 3.11
     * as in Radeon is a lie 
     * Or more precisely the location of bit fields is a lie 
     */
    if(1 || info->ChipFamily < CHIP_FAMILY_R200)
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
    OUTREG(RADEON_OV0_LIN_TRANS_A, dwOvRCb | dwOvLuma);
    OUTREG(RADEON_OV0_LIN_TRANS_B, dwOvROff | dwOvRCr);
    OUTREG(RADEON_OV0_LIN_TRANS_C, dwOvGCb | dwOvLuma);
    OUTREG(RADEON_OV0_LIN_TRANS_D, dwOvGOff | dwOvGCr);
    OUTREG(RADEON_OV0_LIN_TRANS_E, dwOvBCb | dwOvLuma);
    OUTREG(RADEON_OV0_LIN_TRANS_F, dwOvBOff | dwOvBCr);
}

static void RADEONSetColorKey(ScrnInfoPtr pScrn, CARD32 colorKey)
{
    RADEONInfoPtr info = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    CARD32 min, max;
    CARD8 r, g, b;

    if (info->CurrentLayout.depth > 8)
    {
	CARD32	rbits, gbits, bbits;

	rbits = (colorKey & pScrn->mask.red) >> pScrn->offset.red;
	gbits = (colorKey & pScrn->mask.green) >> pScrn->offset.green;
	bbits = (colorKey & pScrn->mask.blue) >> pScrn->offset.blue;

	r = rbits << (8 - pScrn->weight.red);
	g = gbits << (8 - pScrn->weight.green);
	b = bbits << (8 - pScrn->weight.blue);
    }
    else
    {
	CARD32	bits;

	bits = colorKey & ((1 << info->CurrentLayout.depth) - 1);
	r = bits;
	g = bits;
	b = bits;
    }
    min = (r << 16) | (g << 8) | (b);
    max = (0xff << 24) | (r << 16) | (g << 8) | (b);

    RADEONWaitForFifo(pScrn, 2);
    OUTREG(RADEON_OV0_GRAPHICS_KEY_CLR_HIGH, max);
    OUTREG(RADEON_OV0_GRAPHICS_KEY_CLR_LOW, min);
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
        if(pPriv->tda9885!=NULL){
                xfree(pPriv->tda9885);
                pPriv->tda9885=NULL;
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

static void
RADEONResetVideo(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr   info      = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    RADEONPortPrivPtr pPriv = info->adaptor->pPortPrivates[0].ptr;
    char tmp[200];

    if (info->accelOn) info->accel->Sync(pScrn);

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
    xvTunerStatus  = MAKE_ATOM("XV_TUNER_STATUS");
    xvVolume       = MAKE_ATOM("XV_VOLUME");
    xvMute         = MAKE_ATOM("XV_MUTE");
    xvSAP         = MAKE_ATOM("XV_SAP");
    xvHue          = MAKE_ATOM("XV_HUE");
    xvRedIntensity   = MAKE_ATOM("XV_RED_INTENSITY");
    xvGreenIntensity = MAKE_ATOM("XV_GREEN_INTENSITY");
    xvBlueIntensity  = MAKE_ATOM("XV_BLUE_INTENSITY");

    xvDecBrightness   = MAKE_ATOM("XV_DEC_BRIGHTNESS");
    xvDecSaturation   = MAKE_ATOM("XV_DEC_SATURATION");
    xvDecColor        = MAKE_ATOM("XV_DEC_COLOR");
    xvDecContrast     = MAKE_ATOM("XV_DEC_CONTRAST");
    xvDecHue          = MAKE_ATOM("XV_DEC_HUE");

    xvOverlayDeinterlacingMethod = MAKE_ATOM("XV_OVERLAY_DEINTERLACING_METHOD");
    
    xvAutopaintColorkey = MAKE_ATOM("XV_AUTOPAINT_COLORKEY");
    xvSetDefaults = MAKE_ATOM("XV_SET_DEFAULTS");
    
    xvInstanceID = MAKE_ATOM("XV_INSTANCE_ID");
    xvDeviceID = MAKE_ATOM("XV_DEVICE_ID");
    xvLocationID = MAKE_ATOM("XV_LOCATION_ID");
    
    sprintf(tmp, "RXXX:%d.%d.%d", info->PciInfo->vendor, info->PciInfo->chipType, info->PciInfo->chipRev);
    pPriv->device_id = MAKE_ATOM(tmp);
    sprintf(tmp, "PCI:%02d:%02d.%d", info->PciInfo->bus, info->PciInfo->device, info->PciInfo->func);
    pPriv->location_id = MAKE_ATOM(tmp);
    sprintf(tmp, "INSTANCE:%d", pScrn->scrnIndex);
    pPriv->instance_id = MAKE_ATOM(tmp);

    RADEONWaitForIdleMMIO(pScrn);
    OUTREG(RADEON_OV0_SCALE_CNTL, 0x80000000);
    OUTREG(RADEON_OV0_AUTO_FLIP_CNTL, 0);   /* maybe */
    OUTREG(RADEON_OV0_EXCLUSIVE_HORZ, 0);
    OUTREG(RADEON_OV0_FILTER_CNTL, 0x0000000f);
    OUTREG(RADEON_OV0_KEY_CNTL, RADEON_GRAPHIC_KEY_FN_EQ |
				RADEON_VIDEO_KEY_FN_FALSE |
				RADEON_CMP_MIX_OR);
    OUTREG(RADEON_OV0_TEST, 0);
    OUTREG(RADEON_FCP_CNTL, RADEON_FCP0_SRC_GND);
    OUTREG(RADEON_CAP0_TRIG_CNTL, 0);
    RADEONSetColorKey(pScrn, pPriv->colorKey);
    
    if (info->ChipFamily == CHIP_FAMILY_R200 ||
	info->ChipFamily == CHIP_FAMILY_R300) {
	OUTREG(RADEON_OV0_LIN_TRANS_A, 0x12a20000);
	OUTREG(RADEON_OV0_LIN_TRANS_B, 0x198a190e);
	OUTREG(RADEON_OV0_LIN_TRANS_C, 0x12a2f9da);
	OUTREG(RADEON_OV0_LIN_TRANS_D, 0xf2fe0442);
	OUTREG(RADEON_OV0_LIN_TRANS_E, 0x12a22046);
	OUTREG(RADEON_OV0_LIN_TRANS_F, 0x175f);
    } else {
	int i;

	OUTREG(RADEON_OV0_LIN_TRANS_A, 0x12a00000);
	OUTREG(RADEON_OV0_LIN_TRANS_B, 0x1990190e);
	OUTREG(RADEON_OV0_LIN_TRANS_C, 0x12a0f9c0);
	OUTREG(RADEON_OV0_LIN_TRANS_D, 0xf3000442);
	OUTREG(RADEON_OV0_LIN_TRANS_E, 0x12a02040);
	OUTREG(RADEON_OV0_LIN_TRANS_F, 0x175f);

	/*
	 * Set default Gamma ramp:
	 *
	 * Of 18 segments for gamma curve, all segments in R200 (and
	 * newer) are programmable, while only lower 4 and upper 2
	 * segments are programmable in the older Radeons.
	 */
	for (i = 0; i < 18; i++) {
	    OUTREG(def_gamma[i].gammaReg,
		   (def_gamma[i].gammaSlope<<16) | def_gamma[i].gammaOffset);
	}
    }
    if(pPriv->VIP!=NULL){
        RADEONVIP_reset(pScrn,pPriv);
        }
    
    if(pPriv->theatre != NULL) {
        xf86_InitTheatre(pPriv->theatre);
/*      xf86_ResetTheatreRegsForNoTVout(pPriv->theatre); */
        }
    
    if(pPriv->i2c != NULL){
        RADEONResetI2C(pScrn, pPriv);
        }
}

#define I2C_DONE        (1<<0)
#define I2C_NACK        (1<<1)
#define I2C_HALT        (1<<2)
#define I2C_SOFT_RST    (1<<5)
#define I2C_DRIVE_EN    (1<<6)
#define I2C_DRIVE_SEL   (1<<7)
#define I2C_START       (1<<8)
#define I2C_STOP        (1<<9)
#define I2C_RECEIVE     (1<<10)
#define I2C_ABORT       (1<<11)
#define I2C_GO          (1<<12)
#define I2C_SEL         (1<<16)
#define I2C_EN          (1<<17)



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

    usleep(1000);
    while(1)
    {
        RADEONWaitForIdleMMIO(pScrn); 
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
        usleep(10000);
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
    RADEONWaitForIdleMMIO(pScrn);
    reg = INREG8 (RADEON_I2C_CNTL_0 + 0) & 0xF8;
    OUTREG8 (RADEON_I2C_CNTL_0 + 0, reg);

    /* issue ABORT call */
    RADEONWaitForIdleMMIO(pScrn);
    reg = INREG8 (RADEON_I2C_CNTL_0 + 1) & 0xE7;
    OUTREG8 (RADEON_I2C_CNTL_0 + 1, (reg | 0x18));

    /* wait for GO bit to go low */
    RADEONWaitForIdleMMIO(pScrn);
    while (INREG8 (RADEON_I2C_CNTL_0 + 0) & I2C_GO)
    {
      usleep(1000);
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

    RADEONWaitForIdleMMIO(pScrn);
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
    
       RADEONWaitForIdleMMIO(pScrn);
       while(INREG8(RADEON_I2C_CNTL_0+1) & (I2C_GO >> 8));

       status=RADEON_I2C_WaitForAck(pScrn,pPriv);
  
       usleep(1000);

       /* Write Value into the buffer */
       for (loop = 0; loop < nRead; loop++)
       {
          RADEONWaitForFifo(pScrn, 1);
          if((status == I2C_HALT) || (status == I2C_NACK))
          {
          ReadBuffer[loop]=0xff;
          } else {
          RADEONWaitForIdleMMIO(pScrn);
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

    RADEONWaitForIdleMMIO(pScrn);
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
    
       RADEONWaitForIdleMMIO(pScrn);
       while(INREG8(RADEON_I2C_CNTL_0+1) & (I2C_GO >> 8));

       status=RADEON_I2C_WaitForAck(pScrn,pPriv);
  
       usleep(1000);

       /* Write Value into the buffer */
       for (loop = 0; loop < nRead; loop++)
       {
          RADEONWaitForFifo(pScrn, 1);
          if((status == I2C_HALT) || (status == I2C_NACK))
          {
          ReadBuffer[loop]=0xff;
          } else {
          RADEONWaitForIdleMMIO(pScrn);
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

#define I2C_CLOCK_FREQ     (60000.0)


const struct 
{
   char *name; 
   int type;
} RADEON_tuners[32] =
    {
        /* name ,index to tuner_parms table */
        {"NO TUNNER"            , -1},
        {"Philips FI1236 (or compatible)"               , TUNER_TYPE_FI1236},
        {"Philips FI1236 (or compatible)"               , TUNER_TYPE_FI1236},
        {"Philips FI1216 (or compatible)"               , TUNER_TYPE_FI1216},
        {"Philips FI1246 (or compatible)"               , TUNER_TYPE_FI1246},
        {"Philips FI1216MF (or compatible)"             , TUNER_TYPE_FI1216},
        {"Philips FI1236 (or compatible)"               , TUNER_TYPE_FI1236},
        {"Philips FI1256 (or compatible)"               , TUNER_TYPE_FI1256},
        {"Philips FI1236 (or compatible)"               , TUNER_TYPE_FI1236},
        {"Philips FI1216 (or compatible)"               , TUNER_TYPE_FI1216},
        {"Philips FI1246 (or compatible)"               , TUNER_TYPE_FI1246},
        {"Philips FI1216MF (or compatible)"             , TUNER_TYPE_FI1216},
        {"Philips FI1236 (or compatible)"               , TUNER_TYPE_FI1236},
        {"TEMIC-FN5AL"          , TUNER_TYPE_TEMIC_FN5AL},
        {"FQ1216ME/P"           , TUNER_TYPE_FI1216},
        {"FI1236W"              , TUNER_TYPE_FI1236},
        {"Alps TSBH5"           , -1},
        {"Alps TSCxx"           , -1},
        {"Alps TSCH5 FM"        , -1},
        {"UNKNOWN-19"           , -1},
        {"UNKNOWN-20"           , -1},
        {"UNKNOWN-21"           , -1},
        {"UNKNOWN-22"           , -1},
        {"UNKNOWN-23"           , -1},
        {"UNKNOWN-24"           , -1},
        {"UNKNOWN-25"           , -1},
        {"UNKNOWN-26"           , -1},
        {"UNKNOWN-27"           , -1},
        {"UNKNOWN-28"           , -1},
        {"Microtuner MT2032"            , TUNER_TYPE_MT2032},
        {"Microtuner MT2032"            , TUNER_TYPE_MT2032},
        {"UNKNOWN-31"           , -1}
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
    unsigned char *RADEONMMIO = info->MMIO;

    pPriv->fi1236 = NULL;
    pPriv->msp3430 = NULL;
    pPriv->tda9885 = NULL;
    pPriv->saa7114 = NULL;
    
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
    switch(info->ChipFamily){
    	case CHIP_FAMILY_R300:
    	case CHIP_FAMILY_R200:
	case CHIP_FAMILY_RV200:
            	pPriv->i2c->I2CWriteRead=R200_I2CWriteRead;
            	xf86DrvMsg(pScrn->scrnIndex,X_INFO,"Using R200 i2c bus access method\n");
		break;
	default:
            	pPriv->i2c->I2CWriteRead=RADEONI2CWriteRead;
            	xf86DrvMsg(pScrn->scrnIndex,X_INFO,"Using Radeon bus access method\n");
        }
    if(!I2CBusInit(pPriv->i2c))
    {
        xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"Failed to register i2c bus\n");
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "*** %p versus %p\n", xf86CreateI2CBusRec, CreateI2CBusRec);

#if 1
    switch(info->ChipFamily){
	case CHIP_FAMILY_RV200:
            nm=(pll->reference_freq * 40000.0)/(1.0*I2C_CLOCK_FREQ);
	    break;
    	case CHIP_FAMILY_R300:
    	case CHIP_FAMILY_R200:
    	    if(pPriv->MM_TABLE_valid && (RADEON_tuners[pPriv->MM_TABLE.tuner_type & 0x1f].type==TUNER_TYPE_MT2032)){
                nm=(pll->reference_freq * 40000.0)/(4.0*I2C_CLOCK_FREQ);
	        break;
                } 
	default:
            nm=(pll->reference_freq * 10000.0)/(4.0*I2C_CLOCK_FREQ);
        }
#else
    nm=(pll->xclk * 40000.0)/(1.0*I2C_CLOCK_FREQ);         
#endif
    for(pPriv->radeon_N=1; pPriv->radeon_N<255; pPriv->radeon_N++)
          if((pPriv->radeon_N * (pPriv->radeon_N-1)) > nm)break;
    pPriv->radeon_M=pPriv->radeon_N-1;
    pPriv->radeon_i2c_timing=2*pPriv->radeon_N;


    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "ref=%d M=0x%02x N=0x%02x timing=0x%02x\n", pll->reference_freq, pPriv->radeon_M, pPriv->radeon_N, pPriv->radeon_i2c_timing);
#if 0
    pPriv->radeon_M=0x32;
    pPriv->radeon_N=0x33;
    pPriv->radeon_i2c_timing=2*pPriv->radeon_N;
#endif
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
    
    if(pPriv->MM_TABLE_valid && (RADEON_tuners[pPriv->MM_TABLE.tuner_type & 0x1f].type==TUNER_TYPE_MT2032)){
    if(!xf86LoadSubModule(pScrn,"tda9885"))
    {
       xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Unable to initialize tda9885 driver\n");
    }
    else    
    {
    xf86LoaderReqSymbols(TDA9885SymbolsList, NULL);
    if(pPriv->tda9885 == NULL)
    {
        pPriv->tda9885 = xf86_Detect_tda9885(pPriv->i2c, TDA9885_ADDR_1);
    }
    if(pPriv->tda9885 == NULL)
    {
        pPriv->tda9885 = xf86_Detect_tda9885(pPriv->i2c, TDA9885_ADDR_2);
    }
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
       xf86_MSP3430SetVolume(pPriv->msp3430, pPriv->mute ? MSP3430_FAST_MUTE : MSP3430_VOLUME(pPriv->volume));
    }
    
    if(!xf86LoadSubModule(pScrn,"saa7114"))
    {
       xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Unable to initialize saa7114 driver\n");
    } 
    else 
    {
    xf86LoaderReqSymbols(SAA7114SymbolsList, NULL);
    if(pPriv->saa7114 == NULL)
    {
       pPriv->saa7114 = xf86_DetectSAA7114(pPriv->i2c, SAA7114_ADDR_1);
    }
    if(pPriv->saa7114 == NULL)
    {
       pPriv->saa7114 = xf86_DetectSAA7114(pPriv->i2c, SAA7114_ADDR_2);
    }
    }
    if(pPriv->saa7114 != NULL)
    {
       xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Detected SAA7114 at 0x%02x\n", 
                 pPriv->saa7114->d.SlaveAddr);
       xf86_InitSAA7114(pPriv->saa7114);
    }

    if(pPriv->i2c != NULL)RADEON_board_setmisc(pPriv);
    #if 0
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
   
   RADEONWaitForIdleMMIO(pScrn);
   timeout = INREG(VIPH_TIMEOUT_STAT);
   if(timeout & VIPH_TIMEOUT_STAT__VIPH_REG_STAT) /* lockup ?? */
   {
       RADEONWaitForFifo(pScrn, 2);
       OUTREG(VIPH_TIMEOUT_STAT, (timeout & 0xffffff00) | VIPH_TIMEOUT_STAT__VIPH_REG_AK);
       RADEONWaitForIdleMMIO(pScrn);
       return (INREG(VIPH_CONTROL) & 0x2000) ? VIP_BUSY : VIP_RESET;
   }
   RADEONWaitForIdleMMIO(pScrn);
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
   RADEONWaitForIdleMMIO(pScrn);
   OUTREG(VIPH_TIMEOUT_STAT, INREG(VIPH_TIMEOUT_STAT) & (0xffffff00 & ~VIPH_TIMEOUT_STAT__VIPH_REGR_DIS) );
/*
         the value returned here is garbage.  The read merely initiates
         a register cycle
*/
    RADEONWaitForIdleMMIO(pScrn);
    INREG(VIPH_REG_DATA);
    
    while(VIP_BUSY == (status = RADEONVIP_idle(b)));
    if(VIP_IDLE != status) return FALSE;
/*
        set VIPH_REGR_DIS so that the read won't take too long.
*/
    RADEONWaitForIdleMMIO(pScrn);
    tmp=INREG(VIPH_TIMEOUT_STAT);
    OUTREG(VIPH_TIMEOUT_STAT, (tmp & 0xffffff00) | VIPH_TIMEOUT_STAT__VIPH_REGR_DIS);         
    RADEONWaitForIdleMMIO(pScrn);
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


    RADEONWaitForIdleMMIO(pScrn);
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

/* Radeon AIW 7500 has i2s_config 2b not 29 as Radeon AIW */
/* Radeon AIW 7500:

(--) RADEON(0): Chipset: "ATI Radeon 7500 QW (AGP)" (ChipID = 0x5157)
(II) RADEON(0): VIDEO BIOS TABLE OFFSETS: bios_header=0x011c mm_table=0x04ae
(II) RADEON(0): MM_TABLE: 01-0c-06-18-06-80-2b-66-02-05-00-06-00-07
*/

static void RADEONReadMM_TABLE(ScrnInfoPtr pScrn, RADEONPortPrivPtr pPriv)
{
     RADEONInfoPtr info = RADEONPTR(pScrn);
     CARD16 mm_table;
     CARD16 bios_header;

     if((info->VBIOS==NULL)||(info->VBIOS[0]!=0x55)||(info->VBIOS[1]!=0xaa)){
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Cannot access BIOS or it is not valid.\n"
                "\t\tYou will need to specify options RageTheatreCrystal, RageTheatreTunerPort, \n"
                "\t\tRageTheatreSVideoPort and TunerType in /etc/XF86Config.\n"
                );
        pPriv->MM_TABLE_valid = FALSE;
        } else {

     bios_header=info->VBIOS[0x48];
     bios_header+=(((int)info->VBIOS[0x49]+0)<<8);           
        
     mm_table=info->VBIOS[bios_header+0x38];
     if(mm_table==0)
     {
         xf86DrvMsg(pScrn->scrnIndex,X_INFO,"No MM_TABLE found",bios_header,mm_table);
         pPriv->MM_TABLE_valid = FALSE;
         goto forced_settings;
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
             pPriv->MM_TABLE.i2s_config,
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

     forced_settings:     
     if(info->tunerType>=0){
                pPriv->MM_TABLE.tuner_type=info->tunerType;
                pPriv->board_info=info->tunerType;
                }
     /* enough information was provided in the options */
     
     if(!pPriv->MM_TABLE_valid && (info->tunerType>=0) && (info->RageTheatreCrystal>=0) &&
            (info->RageTheatreTunerPort>=0) && (info->RageTheatreCompositePort>=0) &&
            (info->RageTheatreSVideoPort>=0) ) {
                pPriv->MM_TABLE_valid = TRUE;
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

        xf86DrvMsg(t->VIP->scrnIndex,X_INFO,"Rage Theatre: Connectors (detected): tuner=%d, composite=%d, svideo=%d\n",t->wTunerConnector, t->wComp0Connector, t->wSVideo0Connector);
        
        if(info->RageTheatreTunerPort>=0)t->wTunerConnector=info->RageTheatreTunerPort;
        if(info->RageTheatreCompositePort>=0)t->wComp0Connector=info->RageTheatreCompositePort;
        if(info->RageTheatreSVideoPort>=0)t->wSVideo0Connector=info->RageTheatreSVideoPort;
        
        xf86DrvMsg(t->VIP->scrnIndex,X_INFO,"RageTheatre: Connectors (using): tuner=%d, composite=%d, svideo=%d\n",t->wTunerConnector, t->wComp0Connector, t->wSVideo0Connector);

        switch((info->RageTheatreCrystal>=0)?info->RageTheatreCrystal:pll->reference_freq){
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
    pPriv->brightness = 0;
    pPriv->transform_index = 0;
    pPriv->saturation = 0;
    pPriv->contrast = 0;
    pPriv->red_intensity = 0;
    pPriv->green_intensity = 0;
    pPriv->blue_intensity = 0;
    pPriv->hue = 0;
    pPriv->currentBuffer = 0;
    pPriv->autopaint_colorkey = TRUE;

    /*
     * Unlike older Mach64 chips, RADEON has only two ECP settings: 
     * 0 for PIXCLK < 175Mhz, and 1 (divide by 2)
     * for higher clocks, sure makes life nicer 
     */
    if(info->ModeReg.dot_clock_freq < 17500) 
	pPriv->ecp_div = 0;
    else
	pPriv->ecp_div = 1;

#if 0
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Dotclock is %g Mhz, setting ecp_div to %d\n", info->ModeReg.dot_clock_freq/100.0, pPriv->ecp_div);
#endif
 
    OUTPLL(RADEON_VCLK_ECP_CNTL, (INPLL(pScrn, RADEON_VCLK_ECP_CNTL) & 
				  0xfffffCff) | (pPriv->ecp_div << 8));

    info->adaptor = adapt;

    return adapt;
}

static XF86VideoAdaptorPtr
RADEONSetupImageVideo(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    RADEONPortPrivPtr pPriv;
    XF86VideoAdaptorPtr adapt;
    RADEONInfoPtr info = RADEONPTR(pScrn);

    info->accel->Sync(pScrn);

    if(info->adaptor != NULL){
    	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Reinitializing Xvideo subsystems\n");
    	RADEONResetVideo(pScrn);
    	return info->adaptor;
	}

    if(!(adapt = RADEONAllocAdaptor(pScrn)))
	return NULL;

    pPriv = (RADEONPortPrivPtr)(adapt->pPortPrivates[0].ptr);
    
    RADEONReadMM_TABLE(pScrn,pPriv);
    RADEONVIP_init(pScrn,pPriv);

    if(!xf86LoadSubModule(pScrn,"theatre")) 
    {
    	xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"Unable to initialize Rage Theatre\n");
	pPriv->i2c=NULL;
	return NULL;
    } 
    xf86LoaderReqSymbols(TheatreSymbolsList, NULL);
    pPriv->i2c=NULL;
    pPriv->theatre=NULL;
    /* do not try to access i2c bus or Rage Theatre on radeon mobility */
    switch(info->Chipset){
    	case PCI_CHIP_RADEON_LY:
	case PCI_CHIP_RADEON_LZ:
	        xf86DrvMsg(pScrn->scrnIndex,X_INFO,"Detected Radeon Mobility M6, disabling i2c and Rage Theatre\n");
		break;
	case PCI_CHIP_RADEON_LW:
	        xf86DrvMsg(pScrn->scrnIndex,X_INFO,"Detected Radeon Mobility M7, disabling i2c and Rage Theatre\n");
		break;
	default:
	    pPriv->theatre=xf86_DetectTheatre(pPriv->VIP);
	    RADEONInitI2C(pScrn,pPriv);
	}
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

    pPriv = (RADEONPortPrivPtr)(adapt->pPortPrivates[0].ptr);
    REGION_INIT(pScreen, &(pPriv->clip), NullBox, 0);
        
    RADEONResetVideo(pScrn);

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
	xf86ForceHWCursor (pScrn->pScreen, FALSE);
     }
     if(info->videoLinear) {
	xf86FreeOffscreenLinear(info->videoLinear);
	info->videoLinear = NULL;
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
RADEONSetPortAttribute(ScrnInfoPtr  pScrn,
		       Atom	    attribute,
		       INT32	    value,
		       pointer	    data)
{
    RADEONInfoPtr	info = RADEONPTR(pScrn);
    RADEONPortPrivPtr	pPriv = (RADEONPortPrivPtr)data;
    Bool		setTransform = FALSE;
    unsigned char *RADEONMMIO = info->MMIO;

    info->accel->Sync(pScrn);

#define RTFSaturation(a)   (1.0 + ((a)*1.0)/1000.0)
#define RTFBrightness(a)   (((a)*1.0)/2000.0)
#define RTFIntensity(a)   (((a)*1.0)/2000.0)
#define RTFContrast(a)   (1.0 + ((a)*1.0)/1000.0)
#define RTFHue(a)   (((a)*3.1416)/1000.0)
#define ClipValue(v,min,max) ((v) < (min) ? (min) : (v) > (max) ? (max) : (v))

    if(attribute == xvAutopaintColorkey) 
    {
	pPriv->autopaint_colorkey = ClipValue (value, 0, 1);
    }
    else if(attribute == xvSetDefaults) 
    {
	pPriv->autopaint_colorkey = TRUE;
	pPriv->brightness = 0;
	pPriv->saturation = 0;
	pPriv->contrast = 0;
	pPriv->hue = 0;
	pPriv->red_intensity = 0;
	pPriv->green_intensity = 0;
	pPriv->blue_intensity = 0;
        RADEONSetPortAttribute(pScrn, xvDecBrightness, 0, data);
        RADEONSetPortAttribute(pScrn, xvDecSaturation, 0, data);
        RADEONSetPortAttribute(pScrn, xvDecContrast,   0, data);
        RADEONSetPortAttribute(pScrn, xvDecHue,   0, data);

        RADEONSetPortAttribute(pScrn, xvVolume,   -1000, data);
        RADEONSetPortAttribute(pScrn, xvMute,   1, data);
        RADEONSetPortAttribute(pScrn, xvSAP,   0, data);
        RADEONSetPortAttribute(pScrn, xvDoubleBuffer,   1, data);
	setTransform = TRUE;
    } 
    else if(attribute == xvBrightness) 
    {
	pPriv->brightness = ClipValue (value, -1000, 1000);
	setTransform = TRUE;
    } 
    else if((attribute == xvSaturation) || (attribute == xvColor)) 
    {
	pPriv->saturation = ClipValue (value, -1000, 1000);
	setTransform = TRUE;
    } 
    else if(attribute == xvContrast) 
    {
	pPriv->contrast = ClipValue (value, -1000, 1000);
	setTransform = TRUE;
    } 
    else if(attribute == xvHue) 
    {
	pPriv->hue = ClipValue (value, -1000, 1000);
	setTransform = TRUE;
    } 
    else if(attribute == xvRedIntensity) 
    {
	pPriv->red_intensity = ClipValue (value, -1000, 1000);
	setTransform = TRUE;
    } 
    else if(attribute == xvGreenIntensity) 
    {
	pPriv->green_intensity = ClipValue (value, -1000, 1000);
	setTransform = TRUE;
    } 
    else if(attribute == xvBlueIntensity) 
    {
	pPriv->blue_intensity = ClipValue (value, -1000, 1000);
	setTransform = TRUE;
    } 
    else if(attribute == xvDoubleBuffer) 
    {
	pPriv->doubleBuffer = ClipValue (value, 0, 1);
	pPriv->doubleBuffer = value;
    } 
    else if(attribute == xvColorKey) 
    {
	pPriv->colorKey = value;
	RADEONSetColorKey (pScrn, pPriv->colorKey);
	REGION_EMPTY(pScrn->pScreen, &pPriv->clip);
    }  else
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
  if(attribute == xvEncoding) {
        pPriv->encoding = value;
        if(pPriv->video_stream_active)
        {
           if(pPriv->theatre != NULL) RADEON_RT_SetEncoding(pPriv);
           if(pPriv->msp3430 != NULL) RADEON_MSP_SetEncoding(pPriv);
           if(pPriv->tda9885 != NULL) RADEON_TDA9885_SetEncoding(pPriv);
           if(pPriv->i2c != NULL) RADEON_board_setmisc(pPriv);
        /* put more here to actually change it */
        }
  } else 
  if(attribute == xvFrequency) {
        pPriv->frequency = value;
        /* mute volume if it was not muted before */
        if((pPriv->msp3430!=NULL)&& !pPriv->mute)xf86_MSP3430SetVolume(pPriv->msp3430, MSP3430_FAST_MUTE);
        if(pPriv->fi1236 != NULL) xf86_TUNER_set_frequency(pPriv->fi1236, value);
/*        if(pPriv->theatre != NULL) RADEON_RT_SetEncoding(pPriv);  */
        if((pPriv->msp3430 != NULL) && (pPriv->msp3430->recheck))
                xf86_InitMSP3430(pPriv->msp3430);
        if((pPriv->msp3430 != NULL)&& !pPriv->mute) xf86_MSP3430SetVolume(pPriv->msp3430, MSP3430_VOLUME(pPriv->volume));
  } else 
  if(attribute == xvMute) {
        pPriv->mute = value;
        if(pPriv->msp3430 != NULL) xf86_MSP3430SetVolume(pPriv->msp3430, pPriv->mute ? MSP3430_FAST_MUTE : MSP3430_VOLUME(pPriv->volume));
        if(pPriv->i2c != NULL) RADEON_board_setmisc(pPriv);
  } else 
  if(attribute == xvSAP) {
        pPriv->sap_channel = value;
        if(pPriv->msp3430 != NULL) xf86_MSP3430SetSAP(pPriv->msp3430, pPriv->sap_channel?4:3);
  } else 
  if(attribute == xvVolume) {
        if(value<-1000)value = -1000;
        if(value>1000)value = 1000;
        pPriv->volume = value;  
        pPriv->mute = FALSE;
        if(pPriv->msp3430 != NULL) xf86_MSP3430SetVolume(pPriv->msp3430, MSP3430_VOLUME(value));
        if(pPriv->i2c != NULL) RADEON_board_setmisc(pPriv);
  } else 
  if(attribute == xvOverlayDeinterlacingMethod) {
        if(value<0)value = 0;
        if(value>2)value = 2;
        pPriv->overlay_deinterlacing_method = value;    
        switch(pPriv->overlay_deinterlacing_method){
                case METHOD_BOB:
                        OUTREG(RADEON_OV0_DEINTERLACE_PATTERN, 0xAAAAA);
                        break;
                case METHOD_SINGLE:
                        OUTREG(RADEON_OV0_DEINTERLACE_PATTERN, 0xEEEEE | (9<<28));
                        break;
                case METHOD_WEAVE:
                        OUTREG(RADEON_OV0_DEINTERLACE_PATTERN, 0x0);
                        break;
                default:
                        OUTREG(RADEON_OV0_DEINTERLACE_PATTERN, 0xAAAAA);
                }                       
  }
    else 
	return BadMatch;

    if (setTransform)
    {
	RADEONSetTransform(pScrn, 
			   RTFBrightness(pPriv->brightness), 
			   RTFContrast(pPriv->contrast), 
			   RTFSaturation(pPriv->saturation), 
			   RTFHue(pPriv->hue),
			   RTFIntensity(pPriv->red_intensity),
			   RTFIntensity(pPriv->green_intensity),
			   RTFIntensity(pPriv->blue_intensity),
			   pPriv->transform_index);
    }
	
    return Success;
}

static int
RADEONGetPortAttribute(ScrnInfoPtr  pScrn,
		       Atom	    attribute,
		       INT32	    *value,
		       pointer	    data)
{
    RADEONInfoPtr	info = RADEONPTR(pScrn);
    RADEONPortPrivPtr	pPriv = (RADEONPortPrivPtr)data;

    if (info->accelOn) info->accel->Sync(pScrn);

    if(attribute == xvAutopaintColorkey)
	*value = pPriv->autopaint_colorkey;
    else if(attribute == xvBrightness)
	*value = pPriv->brightness;
    else if((attribute == xvSaturation) || (attribute == xvColor))
	*value = pPriv->saturation;
    else if(attribute == xvContrast)
	*value = pPriv->contrast;
    else if(attribute == xvHue)
	*value = pPriv->hue;
    else if(attribute == xvRedIntensity)
	*value = pPriv->red_intensity;
    else if(attribute == xvGreenIntensity)
	*value = pPriv->green_intensity;
    else if(attribute == xvBlueIntensity)
	*value = pPriv->blue_intensity;
    else if(attribute == xvDoubleBuffer)
	*value = pPriv->doubleBuffer ? 1 : 0;
    else if(attribute == xvColorKey)
	*value = pPriv->colorKey;
     else
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
  }  else 
  if(attribute == xvEncoding) {
        *value = pPriv->encoding;
  } else 
  if(attribute == xvFrequency) {
        *value = pPriv->frequency;
  } else 
  if(attribute == xvTunerStatus) {
        if(pPriv->fi1236==NULL){
                *value=TUNER_OFF;
                } else
                {
                *value = xf86_TUNER_get_afc_hint(pPriv->fi1236);
                }
  } else 
  if(attribute == xvMute) {
        *value = pPriv->mute;
  } else 
  if(attribute == xvSAP) {
        *value = pPriv->sap_channel;
  } else 
  if(attribute == xvVolume) {
        *value = pPriv->volume;
  } else 
  if(attribute == xvOverlayDeinterlacingMethod) {
        *value = pPriv->overlay_deinterlacing_method;
  } else 
  if(attribute == xvDeviceID) {
        *value = pPriv->device_id;
  } else 
  if(attribute == xvLocationID) {
        *value = pPriv->location_id;
  } else 
  if(attribute == xvInstanceID) {
        *value = pPriv->instance_id;
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
RADEONCopyRGB24Data(
  unsigned char *src,
  unsigned char *dst,
  int srcPitch,
  int dstPitch,
  int h,
  int w
){
    CARD32 *dptr;
    CARD8 *sptr;
    int i,j;
    

    for(j=0;j<h;j++){
	dptr=(CARD32 *)(dst+j*dstPitch);
        sptr=src+j*srcPitch;
    
    	for(i=w;i>0;i--){
	dptr[0]=((sptr[0])<<24)|((sptr[1])<<16)|(sptr[2]);
	dptr++;
	sptr+=3;
	}
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

static void
RADEONDisplayVideo(
    ScrnInfoPtr pScrn,
    RADEONPortPrivPtr pPriv, 
    int id,
    int offset1, int offset2,
    int offset3, int offset4,
    short width, short height,
    int pitch,
    int left, int right, int top,
    BoxPtr dstBox,
    short src_w, short src_h,
    short drw_w, short drw_h,
    int deinterlacing_method
){
    RADEONInfoPtr info = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    unsigned int v_inc, h_inc, h_inc_uv, step_by_y, step_by_uv, tmp;
    double v_inc_d;
    int p1_h_accum_init, p23_h_accum_init;
    int p1_v_accum_init;
    int ecp_div;
    int v_inc_shift;
    int y_mult;
    int x_off;
    int is_rgb;
    CARD32 scaler_src;
    CARD32 scale_cntl;
  
    is_rgb=0;
    switch(id){
        case FOURCC_RGBA32:
        case FOURCC_RGB24:
        case FOURCC_RGBT16:
        case FOURCC_RGB16:
            is_rgb=1;
            break;
        default:
        }

   /* Unlike older Mach64 chips, RADEON has only two ECP settings: 0 for PIXCLK < 175Mhz, and 1 (divide by 2)
       for higher clocks, sure makes life nicer 
       
       Here we need to find ecp_div again, as the user may have switched resolutions */
    if(info->ModeReg.dot_clock_freq < 17500) 
	ecp_div = 0;
    else
	ecp_div = 1;
 
    OUTPLL(RADEON_VCLK_ECP_CNTL, (INPLL(pScrn, RADEON_VCLK_ECP_CNTL) & 0xfffffCff) | (ecp_div << 8));

    v_inc_shift = 20;
    if (pScrn->currentMode->Flags & V_INTERLACE)
	v_inc_shift++;
    if (pScrn->currentMode->Flags & V_DBLSCAN)
	v_inc_shift--;
    v_inc = (src_h << v_inc_shift) / drw_h;
    v_inc_d = src_h * pScrn->currentMode->VDisplay;
    if(info->DisplayType==MT_LCD)
        v_inc_d = v_inc_d/(drw_h*info->PanelYRes);
        else
        v_inc_d = v_inc_d/(drw_h*pScrn->currentMode->VDisplay);

    v_inc = v_inc_d * v_inc;

    h_inc = ((src_w << (12 + ecp_div)) / drw_w);
    step_by_y = 1;
    step_by_uv = step_by_y;

    /* we could do a tad better  - but why
       bother when this concerns downscaling and the code is so much more
       hairy */
    while(h_inc >= (2 << 12)) {
        if(!is_rgb && ((h_inc+h_inc/2)<(2<<12))){
                step_by_uv = step_by_y+1;
                break;
                }
        step_by_y++;
        step_by_uv = step_by_y;
        h_inc >>= 1;
        }
    h_inc_uv = h_inc>>(step_by_uv-step_by_y);

    /* 1536 is magic number - maximum line length the overlay scaler can fit 
       in the buffer for 2 tap filtering */
    /* the only place it is documented in is in ATI source code */
    /* we need twice as much space for 4 tap filtering.. */
    /* under special circumstances turn on 4 tap filtering */
    if(!is_rgb && (step_by_y==1) && (step_by_uv==1) && (h_inc < (1<<12)) && (deinterlacing_method!=METHOD_WEAVE) 
       && (drw_w*2 < 1536)){
        step_by_y=0;
        step_by_uv=1;
        h_inc_uv = h_inc;
        }

    /* keep everything in 16.16 */

    offset1 += ((left >> 16) & ~7) << 1;
    offset2 += ((left >> 16) & ~7) << 1;
    offset3 += ((left >> 16) & ~7) << 1;
    offset4 += ((left >> 16) & ~7) << 1;
    
    tmp = (left & 0x0003ffff) + 0x00028000 + (h_inc << 3);
    p1_h_accum_init = ((tmp <<  4) & 0x000f8000) |
		      ((tmp << 12) & 0xf0000000);

    tmp = ((left >> 1) & 0x0001ffff) + 0x00028000 + (h_inc_uv << 2);
    p23_h_accum_init = ((tmp <<  4) & 0x000f8000) |
		       ((tmp << 12) & 0x70000000);

    tmp = (top & 0x0000ffff) + 0x00018000;
    p1_v_accum_init = ((tmp << 4) & 0x03ff8000) | (((deinterlacing_method!=METHOD_WEAVE)&&!is_rgb)?0x03:0x01);

    left = (left >> 16) & 7;

    RADEONWaitForFifo(pScrn, 2);
    OUTREG(RADEON_OV0_REG_LOAD_CNTL, 1);
    if (info->accelOn) info->accel->Sync(pScrn);
    while(!(INREG(RADEON_OV0_REG_LOAD_CNTL) & (1 << 3)));

    RADEONWaitForFifo(pScrn, 1);
    is_rgb=0;
    switch(id){
        case FOURCC_RGBA32:
        case FOURCC_RGB24:
            OUTREG(RADEON_OV0_H_INC, (h_inc>>0) | ((h_inc>>0)<<16));
            is_rgb=1;
            break;
        case FOURCC_RGBT16:
        case FOURCC_RGB16:
            OUTREG(RADEON_OV0_H_INC, h_inc | (h_inc << 16));
            is_rgb=1;
            break;
        default:
            OUTREG(RADEON_OV0_H_INC, h_inc | ((h_inc_uv >> 1) << 16));
        }
    RADEONWaitForFifo(pScrn, 15);
    OUTREG(RADEON_OV0_STEP_BY, step_by_y | (step_by_uv << 8));

    y_mult = 1;
    if (pScrn->currentMode->Flags & V_DBLSCAN)
	y_mult = 2;
    x_off = 8;
    if (info->ChipFamily == CHIP_FAMILY_R200 ||
	info->ChipFamily == CHIP_FAMILY_R300)
	x_off = 0;

    /* Put the hardware overlay on CRTC2:
     * For now, the CRTC2 overlay is only implemented for clone mode.
     * Xinerama 2nd head will be similar, but there are other issues.
     *
     * Since one hardware overlay can not be displayed on two heads 
     * at the same time, we might need to consider using software
     * rendering for the second head (do we really need it?).
     */
    if (info->Clone && info->OverlayOnCRTC2) {
	x_off = 0;
	OUTREG(RADEON_OV1_Y_X_START, ((dstBox->x1
				       + x_off
				       - info->CloneFrameX0
				       + pScrn->frameX0) |
				      ((dstBox->y1*y_mult -
					info->CloneFrameY0
					+ pScrn->frameY0) << 16)));
	OUTREG(RADEON_OV1_Y_X_END,   ((dstBox->x2
				       + x_off
				       - info->CloneFrameX0
				       + pScrn->frameX0) |
				      ((dstBox->y2*y_mult
					- info->CloneFrameY0
					+ pScrn->frameY0) << 16)));
	scaler_src = (1 << 14);
    } else {
	OUTREG(RADEON_OV0_Y_X_START, ((dstBox->x1 + x_off) |
				      ((dstBox->y1*y_mult) << 16)));
	OUTREG(RADEON_OV0_Y_X_END,   ((dstBox->x2 + x_off) |
				      ((dstBox->y2*y_mult) << 16)));
	scaler_src = 0;
    }

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
    OUTREG(RADEON_OV0_VID_BUF2_BASE_ADRS, offset3 & 0xfffffff0);

    RADEONWaitForFifo(pScrn, 9);
    OUTREG(RADEON_OV0_VID_BUF3_BASE_ADRS, offset4 & 0xfffffff0);
    OUTREG(RADEON_OV0_VID_BUF4_BASE_ADRS, offset1 & 0xfffffff0);
    OUTREG(RADEON_OV0_VID_BUF5_BASE_ADRS, offset2 & 0xfffffff0);
    OUTREG(RADEON_OV0_P1_V_ACCUM_INIT, p1_v_accum_init);
    OUTREG(RADEON_OV0_P1_H_ACCUM_INIT, p1_h_accum_init);
    OUTREG(RADEON_OV0_P23_H_ACCUM_INIT, p23_h_accum_init);

#if 0
    if(id == FOURCC_UYVY)
       OUTREG(RADEON_OV0_SCALE_CNTL, 0x41008C03);
    else
       OUTREG(RADEON_OV0_SCALE_CNTL, 0x41008B03);
#endif

   scale_cntl = RADEON_SCALER_ADAPTIVE_DEINT | RADEON_SCALER_DOUBLE_BUFFER 
        | RADEON_SCALER_ENABLE | RADEON_SCALER_SMART_SWITCH | (0x7f<<16);
   switch(id){
        case FOURCC_UYVY:
                OUTREG(RADEON_OV0_SCALE_CNTL, RADEON_SCALER_SOURCE_YVYU422 | scale_cntl);
                break;
        case FOURCC_RGB24:
        case FOURCC_RGBA32:
                OUTREG(RADEON_OV0_SCALE_CNTL, RADEON_SCALER_SOURCE_32BPP | scale_cntl | 0x10000000);
                break;
        case FOURCC_RGBT16:
                OUTREG(RADEON_OV0_SCALE_CNTL, RADEON_SCALER_SOURCE_16BPP 
                        | 0x10000000 
                        | scale_cntl);
                break;
        case FOURCC_RGB16:
                OUTREG(RADEON_OV0_SCALE_CNTL, RADEON_SCALER_SOURCE_16BPP 
                        | 0x10000000 
                        | scale_cntl);
                break;
        case FOURCC_YUY2:
        case FOURCC_YV12:
        case FOURCC_I420:
        default:
                OUTREG(RADEON_OV0_SCALE_CNTL,  RADEON_SCALER_SOURCE_VYUY422 
                        | ((info->ChipFamily>=CHIP_FAMILY_R200) ? RADEON_SCALER_TEMPORAL_DEINT :0) 
                        | scale_cntl);
        }
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

   info->accel->Sync(pScrn);

   /* if capture was active shutdown it first */
   if(pPriv->video_stream_active)
   {
	RADEONWaitForFifo(pScrn, 2); 
        OUTREG(RADEON_FCP_CNTL, RADEON_FCP0_SRC_GND);
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
   case FOURCC_RGB24:
   	dstPitch=(width*4+0x0f)&(~0x0f);
	srcPitch=width*3;
	new_size=(dstPitch*height+bpp-1)/bpp;
	break;
   case FOURCC_RGBA32:
   	dstPitch=(width*4+0x0f)&(~0x0f);
	srcPitch=width*4;
	new_size=(dstPitch*height+bpp-1)/bpp;
	break;
   case FOURCC_RGBT16:
   	dstPitch=(width*2+0x0f)&(~0x0f);
	srcPitch=(width*2+3)&(~0x03);
	new_size=(dstPitch*height+bpp-1)/bpp;
	break;
   case FOURCC_RGB16:
   	dstPitch=(width*2+0x0f)&(~0x0f);
	srcPitch=(width*2+3)&(~0x03);
	new_size=(dstPitch*height+bpp-1)/bpp;
	break;
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

   if(!(info->videoLinear = RADEONAllocateMemory(pScrn, info->videoLinear,
		pPriv->doubleBuffer ? (new_size << 1) : new_size)))
   {
	return BadAlloc;
   }

   pPriv->currentBuffer ^= 1;

    /* copy data */
   top = ya >> 16;
   left = (xa >> 16) & ~1;
   npixels = ((((xb + 0xffff) >> 16) + 1) & ~1) - left;

   offset = (info->videoLinear->offset * bpp) + (top * dstPitch);
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
	{

#if X_BYTE_ORDER == X_BIG_ENDIAN
       unsigned char *RADEONMMIO = info->MMIO;
	CARD32 surface_cntl;

	surface_cntl = INREG(RADEON_SURFACE_CNTL);
	OUTREG(RADEON_SURFACE_CNTL, (surface_cntl | 
		RADEON_NONSURF_AP0_SWP_32BPP) & ~RADEON_NONSURF_AP0_SWP_16BPP);
#endif
	RADEONCopyMungedData(buf + (top * srcPitch) + left, buf + s2offset,
			   buf + s3offset, dst_start, srcPitch, srcPitch2,
			   dstPitch, nlines, npixels);
#if X_BYTE_ORDER == X_BIG_ENDIAN
	/* restore byte swapping */
	OUTREG(RADEON_SURFACE_CNTL, surface_cntl);
#endif
	}
	break;
    case FOURCC_RGBA32:
	buf += (top * srcPitch) + left*4;
	nlines = ((yb + 0xffff) >> 16) - top;
	dst_start += left*4;
	RADEONCopyData(buf, dst_start, srcPitch, dstPitch, nlines, 2*npixels); 
    	break;
    case FOURCC_RGB24:
	buf += (top * srcPitch) + left*3;
	nlines = ((yb + 0xffff) >> 16) - top;
	dst_start += left*4;
	RADEONCopyRGB24Data(buf, dst_start, srcPitch, dstPitch, nlines, npixels); 
    	break;
    case FOURCC_UYVY:
    case FOURCC_YUY2:
    case FOURCC_RGBT16:
    case FOURCC_RGB16:
    default:
	left <<= 1;
	buf += (top * srcPitch) + left;
	nlines = ((yb + 0xffff) >> 16) - top;
	dst_start += left;
	RADEONCopyData(buf, dst_start, srcPitch, dstPitch, nlines, npixels);
	break;
    }


    /* update cliplist */
    if(!RegionsEqual(&pPriv->clip, clipBoxes)) 
    {
	REGION_COPY(pScreen, &pPriv->clip, clipBoxes);
	/* draw these */
	if(pPriv->autopaint_colorkey)
	    (*info->accel->FillSolidRects)(pScrn, pPriv->colorKey, GXcopy,
					   (CARD32)~0,
					   REGION_NUM_RECTS(clipBoxes),
					   REGION_RECTS(clipBoxes));
    }

    if (!(pPriv->videoStatus & CLIENT_VIDEO_ON))
	xf86ForceHWCursor (pScrn->pScreen, TRUE);

    RADEONDisplayVideo(pScrn, pPriv, id, offset, offset, offset, offset, width, height, dstPitch,
		     xa, xb, ya, &dstBox, src_w, src_h, drw_w, drw_h, METHOD_BOB);

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
    case FOURCC_RGBA32:
	size = *w << 2;
	if(pitches) pitches[0] = size;
	size *= *h;
    	break;
    case FOURCC_RGB24:
	size = (*w) *3;
	if(pitches) pitches[0] = size;
	size *= *h;
    	break;
    case FOURCC_UYVY:
    case FOURCC_YUY2:
    case FOURCC_RGBT16:
    case FOURCC_RGB16:
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
xf86_InitMSP3430(pPriv->msp3430);
xf86_MSP3430SetVolume(pPriv->msp3430, pPriv->mute ? MSP3430_FAST_MUTE : MSP3430_VOLUME(pPriv->volume));
}

void RADEON_TDA9885_SetEncoding(RADEONPortPrivPtr pPriv)
{
TDA9885Ptr t=pPriv->tda9885;
t->sound_trap=0;
t->auto_mute_fm=0; /* ? */
t->carrier_mode=0; /* ??? */
t->modulation=2; /* negative FM */
t->forced_mute_audio=0;
t->port1=1;
t->port2=1;
t->top_adjustment=0x10;
t->deemphasis=1; 
t->audio_gain=0;
t->minimum_gain=0;
t->gating=0; 
t->vif_agc=1; /* set to 1 ? - depends on design */
t->gating=1; 
t->top_adjustment=0x10;

switch(pPriv->encoding){
                /* PAL */
        case 1:
        case 2:
        case 3:
                t->standard_video_if=0;
                t->standard_sound_carrier=3;
                break;
                /* NTSC */
        case 4:
        case 5:
        case 6:
                t->standard_video_if=1;
                t->standard_sound_carrier=0;
                break;
                /* SECAM */
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
                t->standard_video_if=0;
                t->standard_sound_carrier=3;
                t->modulation=0; /* positive AM */
                break;
        default:
                return;
        }       
xf86_tda9885_setparameters(pPriv->tda9885); 
xf86_tda9885_getstatus(pPriv->tda9885);
xf86_tda9885_dumpstatus(pPriv->tda9885);
}

/* capture config constants */
#define BUF_TYPE_FIELD          0
#define BUF_TYPE_ALTERNATING    1
#define BUF_TYPE_FRAME          2


#define BUF_MODE_SINGLE         0
#define BUF_MODE_DOUBLE         1
#define BUF_MODE_TRIPLE         2
/* CAP0_CONFIG values */

#define FORMAT_BROOKTREE        0
#define FORMAT_CCIR656          1
#define FORMAT_ZV               2
#define FORMAT_VIP16            3
#define FORMAT_TRANSPORT        4

#define ENABLE_RADEON_CAPTURE_WEAVE (RADEON_CAP0_CONFIG_CONTINUOS \
                        | (BUF_MODE_DOUBLE <<7) \
                        | (BUF_TYPE_FRAME << 4) \
                        | ( (pPriv->theatre !=NULL)?(FORMAT_CCIR656<<23):(FORMAT_BROOKTREE<<23)) \
                        | RADEON_CAP0_CONFIG_HORZ_DECIMATOR \
                        | (pPriv->capture_vbi_data ? RADEON_CAP0_CONFIG_VBI_EN : 0) \
                        | RADEON_CAP0_CONFIG_VIDEO_IN_VYUY422)

#define ENABLE_RADEON_CAPTURE_BOB (RADEON_CAP0_CONFIG_CONTINUOS \
                        | (BUF_MODE_SINGLE <<7)  \
                        | (BUF_TYPE_ALTERNATING << 4) \
                        | ( (pPriv->theatre !=NULL)?(FORMAT_CCIR656<<23):(FORMAT_BROOKTREE<<23)) \
                        | RADEON_CAP0_CONFIG_HORZ_DECIMATOR \
                        | (pPriv->capture_vbi_data ? RADEON_CAP0_CONFIG_VBI_EN : 0) \
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
   unsigned int pitch, new_size, offset1, offset2, offset3, offset4, s2offset, s3offset, vbi_offset0, vbi_offset1;
   int srcPitch, srcPitch2, dstPitch;
   int bpp;
   BoxRec dstBox;
   CARD32 id, display_base;
   int width, height;
   int mult;

   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "PutVideo %dx%d+%d+%d\n", drw_w,drw_h,drw_x,drw_y);
   info->accel->Sync(pScrn);
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

   switch(pPriv->overlay_deinterlacing_method){
        case METHOD_BOB:
        case METHOD_SINGLE:
                mult=2;
                break;
        case METHOD_WEAVE:
        case METHOD_ADAPTIVE:
                mult=4;
                break;
        default:
                xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Internal error: PutVideo\n");
                mult=4;
        }

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
        dstPitch = ((width<<1) + 15) & ~15;
        new_size = ((dstPitch * height) + bpp - 1) / bpp;
        srcPitch = (width<<1);
        break;
   }

   new_size = new_size + 0x1f; /* for aligning */
   if(!(info->videoLinear = RADEONAllocateMemory(pScrn, info->videoLinear, new_size*mult+(pPriv->capture_vbi_data?2*dstPitch*20:0))))
   {
        return BadAlloc;
   }

   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "PutVideo Checkpoint 1\n");

/* I have suspicion that capture engine must be active _before_ Rage Theatre
   is being manipulated with.. */

   
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "PutVideo Checkpoint 2\n");

   RADEONWaitForIdleMMIO(pScrn);
   display_base=INREG(RADEON_DISPLAY_BASE_ADDR);   

/*   RADEONWaitForFifo(pScrn, 15); */

   switch(pPriv->overlay_deinterlacing_method){
        case METHOD_BOB:
        case METHOD_SINGLE:
           offset1 = (info->videoLinear->offset*bpp+0xf) & (~0xf);
           offset2 = ((info->videoLinear->offset+new_size)*bpp + 0x1f) & (~0xf);
           offset3 = offset1;
           offset4 = offset2;
           break;
        case METHOD_WEAVE:
           offset1 = (info->videoLinear->offset*bpp+0xf) & (~0xf);
           offset2 = offset1+dstPitch;
           offset3 = ((info->videoLinear->offset+2*new_size)*bpp + 0x1f) & (~0xf);
           offset4 = offset3+dstPitch;
           break;
        default:
           offset1 = (info->videoLinear->offset*bpp+0xf) & (~0xf);
           offset2 = ((info->videoLinear->offset+new_size)*bpp + 0x1f) & (~0xf);
           offset3 = offset1;
           offset4 = offset2;
        }

   OUTREG(RADEON_CAP0_BUF0_OFFSET, offset1+display_base);
   OUTREG(RADEON_CAP0_BUF0_EVEN_OFFSET, offset2+display_base);
   OUTREG(RADEON_CAP0_BUF1_OFFSET, offset3+display_base);
   OUTREG(RADEON_CAP0_BUF1_EVEN_OFFSET, offset4+display_base);

   OUTREG(RADEON_CAP0_ONESHOT_BUF_OFFSET, offset1+display_base);

   if(pPriv->capture_vbi_data){
        vbi_offset0 = ((info->videoLinear->offset+mult*new_size)*bpp+0xf) & (~0xf);
        vbi_offset1 = vbi_offset0 + dstPitch*20;
        OUTREG(RADEON_CAP0_VBI0_OFFSET, vbi_offset0+display_base);
        OUTREG(RADEON_CAP0_VBI1_OFFSET, vbi_offset1+display_base);
        OUTREG(RADEON_CAP0_VBI_V_WINDOW, 9 | ((pPriv->v-1)<<16));
        OUTREG(RADEON_CAP0_VBI_H_WINDOW, 0 | (2*width)<<16);
        }
   
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "PutVideo Checkpoint 3\n");

   OUTREG(RADEON_CAP0_BUF_PITCH, dstPitch*mult/2);
   OUTREG(RADEON_CAP0_H_WINDOW, (2*width)<<16);
   OUTREG(RADEON_CAP0_V_WINDOW, (((height)+pPriv->v-1)<<16)|(pPriv->v));
   if(mult==2){
           OUTREG(RADEON_CAP0_CONFIG, ENABLE_RADEON_CAPTURE_BOB);
           } else {
           OUTREG(RADEON_CAP0_CONFIG, ENABLE_RADEON_CAPTURE_WEAVE);
           }
   OUTREG(RADEON_CAP0_DEBUG, 0);
   
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "PutVideo Checkpoint 4\n");

   OUTREG(RADEON_VID_BUFFER_CONTROL, (1<<16) | 0x01);
   OUTREG(RADEON_TEST_DEBUG_CNTL, 0);
   
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "PutVideo Checkpoint 5\n");

   
   if(! pPriv->video_stream_active)
   {

      RADEONWaitForIdleMMIO(pScrn);
      xf86DrvMsg(pScrn->scrnIndex, X_INFO, "RADEON_VIDEOMUX_CNT=0x%08x\n", INREG(RADEON_VIDEOMUX_CNTL));
      OUTREG(RADEON_VIDEOMUX_CNTL, INREG(RADEON_VIDEOMUX_CNTL)|1 ); 
      OUTREG(RADEON_CAP0_PORT_MODE_CNTL, (pPriv->theatre!=NULL)? 1: 0);
/* Rage128 setting seems to induce a lockup. What gives ? 
   we need this to activate capture unit 

   Explanation: FCP_CNTL is now apart from PLL registers   
*/   

      OUTREG(RADEON_FCP_CNTL, RADEON_FCP0_SRC_PCLK);
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

   RADEONDisplayVideo(pScrn, pPriv, id, offset1+top*srcPitch, offset2+top*srcPitch, offset3+top*srcPitch, offset4+top*srcPitch, width, height, dstPitch*mult/2,
                     xa, xb, ya, &dstBox, src_w, src_h*mult/2, drw_w, drw_h, pPriv->overlay_deinterlacing_method);

   RADEONWaitForFifo(pScrn, 1);
   OUTREG(RADEON_OV0_REG_LOAD_CNTL,  RADEON_REG_LD_CTL_LOCK);
   RADEONWaitForIdleMMIO(pScrn);
   while(!(INREG(RADEON_OV0_REG_LOAD_CNTL) & RADEON_REG_LD_CTL_LOCK_READBACK));


   switch(pPriv->overlay_deinterlacing_method){
        case METHOD_BOB:
           OUTREG(RADEON_OV0_DEINTERLACE_PATTERN, 0xAAAAA);
           OUTREG(RADEON_OV0_AUTO_FLIP_CNTL,0 /*| RADEON_OV0_AUTO_FLIP_CNTL_SOFT_BUF_ODD*/
                |RADEON_OV0_AUTO_FLIP_CNTL_SHIFT_ODD_DOWN);
           break;
        case METHOD_SINGLE:
           OUTREG(RADEON_OV0_DEINTERLACE_PATTERN, 0xEEEEE | (9<<28));
           OUTREG(RADEON_OV0_AUTO_FLIP_CNTL, RADEON_OV0_AUTO_FLIP_CNTL_SOFT_BUF_ODD
                |RADEON_OV0_AUTO_FLIP_CNTL_SHIFT_ODD_DOWN);
           break;
        case METHOD_WEAVE:
           OUTREG(RADEON_OV0_DEINTERLACE_PATTERN, 0x11111 | (9<<28));
           OUTREG(RADEON_OV0_AUTO_FLIP_CNTL, 0  |RADEON_OV0_AUTO_FLIP_CNTL_SOFT_BUF_ODD 
                | RADEON_OV0_AUTO_FLIP_CNTL_P1_FIRST_LINE_EVEN 
                /* |RADEON_OV0_AUTO_FLIP_CNTL_SHIFT_ODD_DOWN */
                /*|RADEON_OV0_AUTO_FLIP_CNTL_SHIFT_EVEN_DOWN */
                |RADEON_OV0_AUTO_FLIP_CNTL_FIELD_POL_SOURCE);
           break;
        default:
           OUTREG(RADEON_OV0_DEINTERLACE_PATTERN, 0xAAAAA);
           OUTREG(RADEON_OV0_AUTO_FLIP_CNTL, RADEON_OV0_AUTO_FLIP_CNTL_SOFT_BUF_ODD
                |RADEON_OV0_AUTO_FLIP_CNTL_SHIFT_ODD_DOWN);
        }
                
   
   RADEONWaitForIdleMMIO(pScrn);
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
		OUTREG(RADEON_OV0_SCALE_CNTL, 0);
		if (pPriv->videoStatus & CLIENT_VIDEO_ON)
		    xf86ForceHWCursor (pScrn->pScreen, FALSE);
		pPriv->videoStatus = FREE_TIMER;
		pPriv->freeTime = now + FREE_DELAY;
	    }
	} else {  /* FREE_TIMER */
	    if(pPriv->freeTime < now) {
		if(info->videoLinear) {
		   xf86FreeOffscreenLinear(info->videoLinear);
		   info->videoLinear = NULL;
		}
		if (pPriv->videoStatus & CLIENT_VIDEO_ON)
		    xf86ForceHWCursor (pScrn->pScreen, FALSE);
		pPriv->videoStatus = 0;
		info->VideoTimerCallback = NULL;
	    }
	}
    } else  /* shouldn't get here */
	info->VideoTimerCallback = NULL;
}

/****************** Offscreen stuff ***************/
typedef struct {
  FBLinearPtr linear;
  Bool isOn;
} OffscreenPrivRec, * OffscreenPrivPtr;

static int 
RADEONAllocateSurface(
    ScrnInfoPtr pScrn,
    int id,
    unsigned short w, 	
    unsigned short h,
    XF86SurfacePtr surface
){
    FBLinearPtr linear;
    int pitch, fbpitch, size, bpp;
    OffscreenPrivPtr pPriv;
    if((w > 1024) || (h > 1024))
	return BadAlloc;

    w = (w + 1) & ~1;
    pitch = ((w << 1) + 15) & ~15;
    bpp = pScrn->bitsPerPixel >> 3;
    fbpitch = bpp * pScrn->displayWidth;
    size = ((pitch * h) + bpp - 1) / bpp;

    if(!(linear = RADEONAllocateMemory(pScrn, NULL, size)))
	return BadAlloc;

    surface->width = w;
    surface->height = h;

    if(!(surface->pitches = xalloc(sizeof(int)))) {
	xf86FreeOffscreenLinear(linear);
	return BadAlloc;
    }
    if(!(surface->offsets = xalloc(sizeof(int)))) {
	xfree(surface->pitches);
	xf86FreeOffscreenLinear(linear);
	return BadAlloc;
    }
    if(!(pPriv = xalloc(sizeof(OffscreenPrivRec)))) {
	xfree(surface->pitches);
	xfree(surface->offsets);
	xf86FreeOffscreenLinear(linear);
	return BadAlloc;
    }

    pPriv->linear = linear;
    pPriv->isOn = FALSE;

    surface->pScrn = pScrn;
    surface->id = id;   
    surface->pitches[0] = pitch;
    surface->offsets[0] = linear->offset * bpp;
    surface->devPrivate.ptr = (pointer)pPriv;

    return Success;
}

static int 
RADEONStopSurface(
    XF86SurfacePtr surface
){
  OffscreenPrivPtr pPriv = (OffscreenPrivPtr)surface->devPrivate.ptr;
  RADEONInfoPtr info = RADEONPTR(surface->pScrn);
  unsigned char *RADEONMMIO = info->MMIO;

  if(pPriv->isOn) {
	OUTREG(RADEON_OV0_SCALE_CNTL, 0);
	pPriv->isOn = FALSE;
  }
  return Success;
}


static int 
RADEONFreeSurface(
    XF86SurfacePtr surface
){
    OffscreenPrivPtr pPriv = (OffscreenPrivPtr)surface->devPrivate.ptr;

    if(pPriv->isOn)
	RADEONStopSurface(surface);
    xf86FreeOffscreenLinear(pPriv->linear);
    xfree(surface->pitches);
    xfree(surface->offsets);
    xfree(surface->devPrivate.ptr);

    return Success;
}

static int
RADEONGetSurfaceAttribute(
    ScrnInfoPtr pScrn,
    Atom attribute,
    INT32 *value
){
   return RADEONGetPortAttribute(pScrn, attribute, value, 
   		(pointer)(GET_PORT_PRIVATE(pScrn)));
}

static int
RADEONSetSurfaceAttribute(
    ScrnInfoPtr pScrn,
    Atom attribute,
    INT32 value
){
   return RADEONSetPortAttribute(pScrn, attribute, value, 
   		(pointer)(GET_PORT_PRIVATE(pScrn)));
}


static int 
RADEONDisplaySurface(
    XF86SurfacePtr surface,
    short src_x, short src_y, 
    short drw_x, short drw_y,
    short src_w, short src_h, 
    short drw_w, short drw_h,
    RegionPtr clipBoxes
){
    OffscreenPrivPtr pPriv = (OffscreenPrivPtr)surface->devPrivate.ptr;
    ScrnInfoPtr pScrn = surface->pScrn;

    RADEONInfoPtr info = RADEONPTR(pScrn);
    RADEONPortPrivPtr portPriv = info->adaptor->pPortPrivates[0].ptr;

    INT32 xa, ya, xb, yb;
    BoxRec dstBox;
	
    if (src_w > (drw_w << 4))
	drw_w = src_w >> 4;
    if (src_h > (drw_h << 4))
	drw_h = src_h >> 4;

    xa = src_x;
    xb = src_x + src_w;
    ya = src_y;
    yb = src_y + src_h;

    dstBox.x1 = drw_x;
    dstBox.x2 = drw_x + drw_w;
    dstBox.y1 = drw_y;
    dstBox.y2 = drw_y + drw_h;

    if (!RADEONClipVideo(&dstBox, &xa, &xb, &ya, &yb, clipBoxes, 
			surface->width, surface->height))
	return Success;

    dstBox.x1 -= pScrn->frameX0;
    dstBox.x2 -= pScrn->frameX0;
    dstBox.y1 -= pScrn->frameY0;
    dstBox.y2 -= pScrn->frameY0;

    RADEONResetVideo(pScrn);

    RADEONDisplayVideo(pScrn, pPriv, surface->id,
		       surface->offsets[0], surface->offsets[0],
		       surface->offsets[0], surface->offsets[0],
		       surface->width, surface->height, surface->pitches[0],
		       xa, xb, ya, &dstBox, src_w, src_h, drw_w, drw_h, METHOD_BOB);

    if (portPriv->autopaint_colorkey)
	(*info->accel->FillSolidRects)(pScrn, portPriv->colorKey, GXcopy,
				       (CARD32)~0,
				       REGION_NUM_RECTS(clipBoxes),
				       REGION_RECTS(clipBoxes));

    pPriv->isOn = TRUE;
    /* we've prempted the XvImage stream so set its free timer */
    if (portPriv->videoStatus & CLIENT_VIDEO_ON) {
	REGION_EMPTY(pScrn->pScreen, &portPriv->clip);   
	UpdateCurrentTime();
	xf86ForceHWCursor (pScrn->pScreen, FALSE);
	portPriv->videoStatus = FREE_TIMER;
	portPriv->freeTime = currentTime.milliseconds + FREE_DELAY;
	info->VideoTimerCallback = RADEONVideoTimerCallback;
    }

    return Success;
}


static void 
RADEONInitOffscreenImages(ScreenPtr pScreen)
{
/*  ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    RADEONInfoPtr info = RADEONPTR(pScrn); */
    XF86OffscreenImagePtr offscreenImages;
    /* need to free this someplace */

    if (!(offscreenImages = xalloc(sizeof(XF86OffscreenImageRec))))
	return;

    offscreenImages[0].image = &Images[0];
    offscreenImages[0].flags = VIDEO_OVERLAID_IMAGES | 
			       VIDEO_CLIP_TO_VIEWPORT;
    offscreenImages[0].alloc_surface = RADEONAllocateSurface;
    offscreenImages[0].free_surface = RADEONFreeSurface;
    offscreenImages[0].display = RADEONDisplaySurface;
    offscreenImages[0].stop = RADEONStopSurface;
    offscreenImages[0].setAttribute = RADEONSetSurfaceAttribute;
    offscreenImages[0].getAttribute = RADEONGetSurfaceAttribute;
    offscreenImages[0].max_width = 1024;
    offscreenImages[0].max_height = 1024;
    offscreenImages[0].num_attributes = NUM_ATTRIBUTES;
    offscreenImages[0].attributes = Attributes;

    xf86XVRegisterOffscreenImages(pScreen, offscreenImages, 1);
}

#endif  /* !XvExtension */
