/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/ati/r128_video.c,v 1.23 2002/11/01 06:08:36 keithp Exp $ */

#include "r128.h"
#include "r128_reg.h"

#ifdef XF86DRI
#include "r128_common.h"
#include "r128_sarea.h"
#endif

#include "xf86.h"
#include "dixstruct.h"
#include "xf86PciInfo.h"
#include "xf86i2c.h"
#include "fi1236.h"
#include "msp3430.h"
#include "bt829.h"
#include "tda9850.h"
#include "tda8425.h"
#include "generic_bus.h"
#include "theatre_reg.h"
#include "theatre.h"
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
void R128InitVideo(ScreenPtr pScreen) {}
#else

static XF86VideoAdaptorPtr R128SetupImageVideo(ScreenPtr);
static int  R128SetPortAttribute(ScrnInfoPtr, Atom, INT32, pointer);
static int  R128GetPortAttribute(ScrnInfoPtr, Atom ,INT32 *, pointer);
static void R128StopVideo(ScrnInfoPtr, pointer, Bool);
static void R128QueryBestSize(ScrnInfoPtr, Bool, short, short, short, short,
			unsigned int *, unsigned int *, pointer);
static int  R128PutImage(ScrnInfoPtr, short, short, short, short, short,
			short, short, short, int, unsigned char*, short,
			short, Bool, RegionPtr, pointer);
static int R128PutVideo(ScrnInfoPtr pScrn, short src_x, short src_y, short drw_x, short drw_y,
                        short src_w, short src_h, short drw_w, short drw_h, 
			RegionPtr clipBoxes, pointer data);
static int  R128QueryImageAttributes(ScrnInfoPtr, int, unsigned short *,
			unsigned short *,  int *, int *);


static void R128ResetVideo(ScrnInfoPtr);

static void R128VideoTimerCallback(ScrnInfoPtr pScrn, Time now);


#define MAKE_ATOM(a) MakeAtom(a, sizeof(a) - 1, TRUE)

static Atom xvBrightness, xvColorKey, xvSaturation, xvColor, xvDoubleBuffer, 
          xvEncoding, xvVolume, xvMute, xvFrequency, xvContrast, xvHue,
	  xv_autopaint_colorkey, xv_set_defaults, xvTunerStatus, xvSAP,
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


typedef struct _R128PortPrivRec {
   int           brightness;
   int           saturation;
   int           contrast;
   int           hue;
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
   CARD32        r128_M;
   CARD32        r128_N;
   
   FI1236Ptr     fi1236;
   MSP3430Ptr    msp3430;
   BT829Ptr	 bt829;
   TDA9850Ptr	 tda9850;
   TDA8425Ptr    tda8425;
   
   GENERIC_BUS_Ptr VIP;
   TheatrePtr      theatre;
   
   Bool          video_stream_active;
   int           encoding;
   CARD32        frequency;
   int           volume;
   Bool		 mute;
   int		 sap_channel;
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
   Atom		 device_id, location_id, instance_id;
} R128PortPrivRec, *R128PortPrivPtr;

static void R128MuteAudio(R128PortPrivPtr pPriv, Bool mute);
void R128_detect_addon(R128PortPrivPtr pPriv);
Bool R128SetupTheatre(ScrnInfoPtr pScrn, R128PortPrivPtr pPriv, TheatrePtr t);
void R128_RT_SetEncoding(R128PortPrivPtr pPriv);
void R128_MSP_SetEncoding(R128PortPrivPtr pPriv);
void R128_BT_SetEncoding(R128PortPrivPtr pPriv);
void R128_board_setmisc(R128PortPrivPtr pPriv);
void R128VIP_init(ScrnInfoPtr pScrn, R128PortPrivPtr pPriv);
void R128VIP_reset(ScrnInfoPtr pScrn, R128PortPrivPtr pPriv);

void R128InitVideo(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    R128InfoPtr info  = R128PTR(pScrn);
    XF86VideoAdaptorPtr *adaptors, *newAdaptors = NULL;
    XF86VideoAdaptorPtr newAdaptor = NULL;
    int num_adaptors;

    if(info->accel && info->accel->FillSolidRects)
	newAdaptor = R128SetupImageVideo(pScreen);

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

#define MAXWIDTH 2048
#define MAXHEIGHT 2048

/* client libraries expect an encoding */
static XF86VideoEncodingRec DummyEncoding =
{
   0,
   "XV_IMAGE",
   MAXWIDTH, MAXHEIGHT,
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
    { 12,"pal_60-svideo",		768, 288, { 1, 50 }},
    { 13, "pal_m-composite",           640, 240, { 1001, 60000 }},
    { 14, "pal_m-tuner",               640, 240, { 1001, 60000 }},
    { 15, "pal_m-svideo",              640, 240, { 1001, 60000 }}
};

#define NUM_FORMATS 12

static XF86VideoFormatRec Formats[NUM_FORMATS] =
{
   {8, TrueColor}, {8, DirectColor}, {8, PseudoColor},
   {8, GrayScale}, {8, StaticGray}, {8, StaticColor},
   {15, TrueColor}, {16, TrueColor}, {24, TrueColor},
   {15, DirectColor}, {16, DirectColor}, {24, DirectColor}
};


#define NUM_ATTRIBUTES 18

static XF86AttributeRec Attributes[NUM_ATTRIBUTES+1] =
{
   {             XvGettable, 0, ~0, "XV_DEVICE_ID"},
   {             XvGettable, 0, ~0, "XV_LOCATION_ID"},
   {             XvGettable, 0, ~0, "XV_INSTANCE_ID"},
   {XvSettable             , 0, 1, "XV_SET_DEFAULTS"},
   {XvSettable | XvGettable, 0, 1, "XV_AUTOPAINT_COLORKEY"},
   {XvSettable | XvGettable, 0, ~0, "XV_COLORKEY"},
   {XvSettable | XvGettable, 0, 1, "XV_DOUBLE_BUFFER"},
   {XvSettable | XvGettable, 0, 12, "XV_ENCODING"},
   {XvSettable | XvGettable, 0, -1, "XV_FREQ"},
   {XvGettable, -1000, 1000, "XV_TUNER_STATUS"},
   {XvSettable | XvGettable, -1000, 1000, "XV_BRIGHTNESS"},
   {XvSettable | XvGettable, -1000, 1000, "XV_CONTRAST"},
   {XvSettable | XvGettable, -1000, 1000, "XV_SATURATION"},
   {XvSettable | XvGettable, -1000, 1000, "XV_COLOR"},
   {XvSettable | XvGettable, -1000, 1000, "XV_HUE"},
   {XvSettable | XvGettable, 0, 1, "XV_MUTE"},
   {XvSettable | XvGettable, 0, 1, "XV_SAP" },
   {XvSettable | XvGettable, -1000, 1000, "XV_VOLUME"},
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

static void R128MuteAudio(R128PortPrivPtr pPriv, Bool mute)
{
  pPriv->mute=mute;
  if (pPriv->msp3430 != NULL) xf86_MSP3430SetVolume(pPriv->msp3430, mute ? MSP3430_FAST_MUTE : MSP3430_VOLUME(pPriv->volume));
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

void R128LeaveVT_Video(ScrnInfoPtr pScrn)
{
    R128InfoPtr   info      = R128PTR(pScrn);
    R128PortPrivPtr pPriv;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "LeaveVT: Shutting down Xvideo subsystems\n");

    if(info->adaptor==NULL)return;
    pPriv = info->adaptor->pPortPrivates[0].ptr;
    R128ResetVideo(pScrn);
    if(pPriv==NULL)return;
    if(pPriv->theatre!=NULL){
    	xf86_ShutdownTheatre(pPriv->theatre);
	}
}

void R128EnterVT_Video(ScrnInfoPtr pScrn)
{
    R128InfoPtr   info      = R128PTR(pScrn);
    R128PortPrivPtr pPriv;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Starting up Xvideo subsystems\n");
    if(info->adaptor==NULL)return;
    pPriv = info->adaptor->pPortPrivates[0].ptr;
    if(pPriv==NULL)return;
    R128ResetVideo(pScrn);
}

void R128ShutdownVideo(ScrnInfoPtr pScrn)
{
    R128InfoPtr   info      = R128PTR(pScrn);
    R128PortPrivPtr pPriv = info->adaptor->pPortPrivates[0].ptr;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "** Shutting down Xvideo subsystems\n");
    if(pPriv->theatre!=NULL){
	xf86_ShutdownTheatre(pPriv->theatre);
	}
    if(pPriv->i2c!=NULL){
	if(pPriv->msp3430!=NULL){
		xfree(pPriv->msp3430);
		pPriv->msp3430=NULL;
		}
	if(pPriv->fi1236!=NULL){
		xfree(pPriv->fi1236);
		pPriv->fi1236=NULL;
		}
	if(pPriv->bt829!=NULL){
		xfree(pPriv->bt829);
		pPriv->bt829=NULL;
		}
	if(pPriv->tda9850!=NULL){
		xfree(pPriv->tda9850);
		pPriv->tda9850=NULL;
		}
	if(pPriv->tda8425!=NULL){
		xfree(pPriv->tda8425);
		pPriv->tda8425=NULL;
		}
	DestroyI2CBusRec(pPriv->i2c, TRUE, TRUE);
	pPriv->i2c=NULL;
	}
    if(pPriv->VIP!=NULL){
	xfree(pPriv->VIP);
	pPriv->VIP=NULL;
	}
    if(pPriv->theatre!=NULL){
	xfree(pPriv->theatre);
	pPriv->theatre=NULL;
	}
}

void
R128ResetVideo(ScrnInfoPtr pScrn)
{
    R128InfoPtr   info      = R128PTR(pScrn);
    unsigned char *R128MMIO = info->MMIO;
    R128PortPrivPtr pPriv = info->adaptor->pPortPrivates[0].ptr;
    char tmp[200];

    /* this is done here because each time the server is reset these
       could change.. Otherwise they remain constant */

    xvBrightness   = MAKE_ATOM("XV_BRIGHTNESS");
    xvSaturation   = MAKE_ATOM("XV_SATURATION");
    xvColor        = MAKE_ATOM("XV_COLOR");
    xvContrast     = MAKE_ATOM("XV_CONTRAST");
    xvColorKey     = MAKE_ATOM("XV_COLORKEY");
    xvDoubleBuffer = MAKE_ATOM("XV_DOUBLE_BUFFER");
    xvEncoding     = MAKE_ATOM("XV_ENCODING");
    xvTunerStatus  = MAKE_ATOM("XV_TUNER_STATUS");
    xvFrequency    = MAKE_ATOM("XV_FREQ");
    xvVolume       = MAKE_ATOM("XV_VOLUME");
    xvMute         = MAKE_ATOM("XV_MUTE");
    xvSAP          = MAKE_ATOM("XV_SAP");
    xvHue          = MAKE_ATOM("XV_HUE");
    xv_autopaint_colorkey = MAKE_ATOM("XV_AUTOPAINT_COLORKEY");
    xv_set_defaults = MAKE_ATOM("XV_SET_DEFAULTS");

    xvInstanceID = MAKE_ATOM("XV_INSTANCE_ID");
    xvDeviceID = MAKE_ATOM("XV_DEVICE_ID");
    xvLocationID = MAKE_ATOM("XV_LOCATION_ID");
    
    sprintf(tmp, "RXXX:%d.%d.%d", info->PciInfo->vendor, info->PciInfo->chipType, info->PciInfo->chipRev);
    pPriv->device_id = MAKE_ATOM(tmp);
    sprintf(tmp, "PCI:%02d:%02d.%d", info->PciInfo->bus, info->PciInfo->device, info->PciInfo->func);
    pPriv->location_id = MAKE_ATOM(tmp);
    sprintf(tmp, "INSTANCE:%d", pScrn->scrnIndex);
    pPriv->instance_id = MAKE_ATOM(tmp);


    R128WaitForFifo(pScrn, 11); 
    OUTREG(R128_OV0_SCALE_CNTL, 0x80000000);
    OUTREG(R128_OV0_EXCLUSIVE_HORZ, 0);
    OUTREG(R128_OV0_AUTO_FLIP_CNTL, 0);   /* maybe */
    OUTREG(R128_OV0_FILTER_CNTL, 0x0000000f);
    OUTREG(R128_OV0_COLOUR_CNTL, (((pPriv->brightness*64)/1000) & 0x7f) |
				     (((pPriv->saturation*31+31000)/2000) << 8) |
				     (((pPriv->saturation*31+31000)/2000) << 16));
    OUTREG(R128_OV0_GRAPHICS_KEY_MSK, (1 << pScrn->depth) - 1);
    OUTREG(R128_OV0_GRAPHICS_KEY_CLR, pPriv->colorKey);
    OUTREG(R128_OV0_KEY_CNTL, R128_GRAPHIC_KEY_FN_NE);
    OUTREG(R128_OV0_TEST, 0);
    OUTPLL(R128_FCP_CNTL, 0x404);
    OUTREG(R128_CAP0_TRIG_CNTL, 0x0);
    
    if(pPriv->VIP!=NULL){
    	R128VIP_init(pScrn, pPriv);
	}
    
    if(pPriv->theatre != NULL){
    	xf86_InitTheatre(pPriv->theatre);
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
static CARD8 R128_I2C_WaitForAck (ScrnInfoPtr pScrn, R128PortPrivPtr pPriv)
{
    CARD8 retval = 0;
    R128InfoPtr info = R128PTR(pScrn);
    unsigned char *R128MMIO = info->MMIO;
    long counter = 0;

    usleep(1000);
    while(1)
    {
        retval = INREG8(R128_I2C_CNTL_0);
        if (retval & I2C_HALT)
        {
            return (I2C_HALT);
        }
        if (retval & I2C_NACK)
        {
            return (I2C_NACK);
        }
	if (retval & I2C_DONE)
	{
	    return (I2C_DONE);
	}
	counter++;
	if(counter>1000000)
	{
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Timeout condition on rage128 i2c bus\n");
		return (I2C_HALT);
	}
		
    }
}

static void R128_I2C_Halt (ScrnInfoPtr pScrn)
{
    R128InfoPtr info = R128PTR(pScrn);
    unsigned char *R128MMIO = info->MMIO;
    CARD8    reg;
    long counter = 0;

    /* reset status flags */
    reg = INREG8 (R128_I2C_CNTL_0 + 0) & 0xF8;
    OUTREG8 (R128_I2C_CNTL_0 + 0, reg);

    /* issue ABORT call */
    reg = INREG8 (R128_I2C_CNTL_0 + 1) & 0xE7;
    OUTREG8 (R128_I2C_CNTL_0 + 1, (reg | 0x18));

    /* wait for GO bit to go low */
    while (INREG8 (R128_I2C_CNTL_0 + 1) & (I2C_GO >> 8))
    {
       counter++;
       if(counter>1000000)return;
    }

} 



static Bool R128I2CWriteRead(I2CDevPtr d, I2CByte *WriteBuffer, int nWrite,
                            I2CByte *ReadBuffer, int nRead)
{
    int loop, status;
    CARD32 i2c_cntl_0, i2c_cntl_1;
    R128PortPrivPtr pPriv = (R128PortPrivPtr)(d->pI2CBus->DriverPrivate.ptr);
    ScrnInfoPtr pScrn = xf86Screens[d->pI2CBus->scrnIndex];
    R128InfoPtr info = R128PTR(pScrn);
    unsigned char *R128MMIO = info->MMIO;

    status=I2C_DONE;

    if(nWrite>0){
      R128WaitForFifo(pScrn, 4+nWrite);

      /* Clear the status bits of the I2C Controller */
      OUTREG(R128_I2C_CNTL_0, I2C_DONE | I2C_NACK | I2C_HALT | I2C_SOFT_RST);

      /* Write the address into the buffer first */
      OUTREG(R128_I2C_DATA, (CARD32) (d->SlaveAddr) & ~(1));

      /* Write Value into the buffer */
      for (loop = 0; loop < nWrite; loop++)
      {
        OUTREG8(R128_I2C_DATA, WriteBuffer[loop]);
      }

      i2c_cntl_1 = (pPriv->r128_i2c_timing << 24) | I2C_EN | I2C_SEL | 
    			nWrite | 0x100;
      OUTREG(R128_I2C_CNTL_1, i2c_cntl_1);
    
      i2c_cntl_0 = (pPriv->r128_N << 24) | (pPriv->r128_M << 16) | 
    			I2C_GO | (1<<8) | (((nRead>0)?0:1) << 9) | I2C_DRIVE_EN;
      OUTREG(R128_I2C_CNTL_0, i2c_cntl_0);
    
      while(INREG8(R128_I2C_CNTL_0+1) & (I2C_GO >> 8));

      status=R128_I2C_WaitForAck(pScrn,pPriv);

      if(status!=I2C_DONE){
      	R128_I2C_Halt(pScrn);
      	return FALSE;
	}
    }
    
    if(nRead > 0) {
      R128WaitForFifo(pScrn, 4+nRead);
    
      OUTREG(R128_I2C_CNTL_0, 0x27);

      /* Write the address into the buffer first */
      OUTREG(R128_I2C_DATA, (CARD32) (d->SlaveAddr) | (1));

      i2c_cntl_1 = (pPriv->r128_i2c_timing << 24) | I2C_EN | I2C_SEL | 
    			nRead | 0x100;
      OUTREG(R128_I2C_CNTL_1, i2c_cntl_1);
    
      i2c_cntl_0 = (pPriv->r128_N << 24) | (pPriv->r128_M << 16) | 
    			I2C_GO | (1<<8) | ((1) << 9) | I2C_DRIVE_EN | I2C_RECEIVE;
      OUTREG(R128_I2C_CNTL_0, i2c_cntl_0);
    
      while(INREG8(R128_I2C_CNTL_0+1) & (I2C_GO >> 8));

      status=R128_I2C_WaitForAck(pScrn,pPriv);

      /* Write Value into the buffer */
      for (loop = 0; loop < nRead; loop++)
      {
        R128WaitForFifo(pScrn, 1);
	if((status == I2C_HALT) || (status == I2C_NACK))
	{
	ReadBuffer[loop]=0xff;
	} else {
        ReadBuffer[loop]=INREG8(R128_I2C_DATA) & 0xff;
	}
      }

    }
    
    if(status!=I2C_DONE){
    	R128_I2C_Halt(pScrn);
    	return FALSE;
	}
    return TRUE;
}

static Bool R128ProbeAddress(I2CBusPtr b, I2CSlaveAddr addr)
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
} R128_tuners[32] =
    {
        /* name	,index to tuner_parms table */
	{"NO TUNNER"		, -1},
	{"FI1236"		, TUNER_TYPE_FI1236},
	{"FI1236"		, TUNER_TYPE_FI1236},
	{"FI1216"		, TUNER_TYPE_FI1216},
	{"FI1246"		, TUNER_TYPE_FI1246},
	{"FI1216MF"		, TUNER_TYPE_FI1216},
	{"FI1236"		, TUNER_TYPE_FI1236},
	{"FI1256"		, TUNER_TYPE_FI1256},
	{"FI1236"		, TUNER_TYPE_FI1236},
	{"FI1216"		, TUNER_TYPE_FI1216},
	{"FI1246"		, TUNER_TYPE_FI1246},
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

static void R128ResetI2C(ScrnInfoPtr pScrn, R128PortPrivPtr pPriv)
{
    R128InfoPtr info = R128PTR(pScrn);
    unsigned char *R128MMIO = info->MMIO;

    OUTREG8(R128_I2C_CNTL_1+2, ((I2C_SEL | I2C_EN)>>16));
    OUTREG8(R128_I2C_CNTL_0+0, (I2C_DONE | I2C_NACK | I2C_HALT | I2C_SOFT_RST | I2C_DRIVE_EN | I2C_DRIVE_SEL));
}

static void R128InitI2C(ScrnInfoPtr pScrn, R128PortPrivPtr pPriv)
{
    double nm;
    R128InfoPtr info = R128PTR(pScrn);
    R128PLLPtr  pll = &(info->pll);

    pPriv->fi1236 = NULL;
    pPriv->bt829 = NULL;
    pPriv->tda9850 = NULL;
    pPriv->msp3430 = NULL;
    
    if(pPriv->i2c==NULL) {
    if(!xf86LoadSubModule(pScrn,"i2c")) {
    	xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"Unable to initialize i2c bus\n");
	pPriv->i2c=NULL;
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
    pPriv->i2c->BusName="Rage 128 multimedia bus";
    pPriv->i2c->DriverPrivate.ptr=(pointer)pPriv;
    pPriv->i2c->I2CWriteRead=R128I2CWriteRead;
    if(!I2CBusInit(pPriv->i2c)){
    	xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"Failed to register i2c bus\n");
    	}
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "*** %p versus %p\n", xf86CreateI2CBusRec, CreateI2CBusRec);

    nm=(pll->reference_freq * 10000.0)/(4.0 * I2C_CLOCK_FREQ);
    for(pPriv->r128_N=1; pPriv->r128_N<255; pPriv->r128_N++)
          if((pPriv->r128_N * (pPriv->r128_N-1)) > nm)break;
    pPriv->r128_M=pPriv->r128_N-1;
    pPriv->r128_i2c_timing=2*pPriv->r128_N;
    
    R128ResetI2C(pScrn, pPriv);

       /* You can't attach addon card to a notebook */
    if(!pPriv->MM_TABLE_valid && 
                 (((info->Chipset != PCI_CHIP_RAGE128LE) &&
		 (info->Chipset != PCI_CHIP_RAGE128LF) &&
		 (info->Chipset != PCI_CHIP_RAGE128MF) &&
		 (info->Chipset != PCI_CHIP_RAGE128ML)) || 
		 info->forceXvProbing))R128_detect_addon(pPriv);
    	  else pPriv->addon_board = FALSE;
    
    /* no multimedia capabilities detected */
    /* the following code _will_ lockup with MobilityM3 and no i2c device attached 
       at the moment no safe way is known to detect this (as BIOS has no multimedia table) */ 
    if(!pPriv->MM_TABLE_valid && !pPriv->addon_board)
    {
       if(!info->forceXvProbing){
	       xf86DrvMsg(pScrn->scrnIndex, X_INFO, "No video input capabilities detected\n");
       	       return;
	       } else {
	       xf86DrvMsg(pScrn->scrnIndex, X_INFO, "No video input capabilities detected, but continuing anyway.\n");
	       xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Don't be surprised if the system locks up.\n");
	       }	       
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
	       R128_tuners[pPriv->board_info & 0x0f].name,
               FI1236_ADDR(pPriv->fi1236));
               xf86_FI1236_set_tuner_type(pPriv->fi1236, R128_tuners[pPriv->board_info & 0x0f].type);
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
	 pPriv->bt829->tunertype = pPriv->board_info & 0x0f;
         if(xf86_bt829_ATIInit(pPriv->bt829) < 0)pPriv->bt829 = NULL; /* disable it */
         if(pPriv->MM_TABLE_valid && (pPriv->bt829!=NULL))
         {
           xf86_bt829_SetP_IO(pPriv->bt829, 0x02); /* mute */
           xf86_bt829_SetOUT_EN(pPriv->bt829, 1);
         }
      }
    }

/* I am not sure whether this is really necessary.. but just in case - and it does
   not hurt right now */

#if 1    
    if(!pPriv->MM_TABLE_valid)
      {
     	xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"Skipping Rage Theatre detection because of absent or invalid MM_TABLE\n");
	pPriv->theatre=NULL;
      } else      
#endif
    if(pPriv->bt829 == NULL){ 
    
       R128VIP_init(pScrn,pPriv);

      if(!xf86LoadSubModule(pScrn,"theatre")) 
      {
     	xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"Unable to load Rage Theatre module\n");
	pPriv->theatre=NULL;
      } else 
      {
        xf86LoaderReqSymbols(TheatreSymbolsList, NULL);
        pPriv->theatre=xf86_DetectTheatre(pPriv->VIP);
        if((pPriv->theatre != NULL) && !R128SetupTheatre(pScrn, pPriv, pPriv->theatre))
        {
    	  free(pPriv->theatre);
	  pPriv->theatre=NULL;
	  xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to initialize Rage Theatre, chip disabled\n");
        }
       
        if(pPriv->theatre != NULL) {
          xf86_InitTheatre(pPriv->theatre);
        }
      }
    }

    if((pPriv->bt829 == NULL) && (pPriv->theatre == NULL))
             /* No decoder found. No sense initializing audio chips */
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
#if 0  /* this would confuse bt829 with MSP3430 */
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
	  xf86DrvMsg(pScrn->scrnIndex, X_INFO, "tda9850 status 0x%04x\n", xf86_tda9850_getstatus(pPriv->tda9850));
       }  
    }
    if(!xf86LoadSubModule(pScrn, "tda8425"))
    {
       xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Unable to initialize tda8425 driver\n");
    } else 
    {
       /* Bug bug bug. I don't know how to probe a write-only device properly with Rage 128
          hardware assisted  i2c. Help ?! */
       xf86LoaderReqSymbols(TDA8425SymbolsList, NULL);
       if((pPriv->tda8425 == NULL) && !pPriv->MM_TABLE_valid)
       {
          pPriv->tda8425 = xf86_Detect_tda8425(pPriv->i2c, TDA8425_ADDR_1, TRUE);
       }
       if(pPriv->tda8425 != NULL)
       {
          if(!xf86_tda8425_init(pPriv->tda8425))pPriv->tda8425 = NULL; /* disable it */
       }
    }

   if(pPriv->i2c!=NULL) R128_board_setmisc(pPriv);
}

#define VIP_NAME      "R128 VIP BUS"
#define VIP_TYPE      "ATI VIP BUS"

static Bool R128VIP_ioctl(GENERIC_BUS_Ptr b, long ioctl, long arg1, char *arg2)
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

static CARD32 R128VIP_idle(GENERIC_BUS_Ptr b)
{
   ScrnInfoPtr pScrn = xf86Screens[b->scrnIndex];
   R128InfoPtr info = R128PTR(pScrn);
   unsigned char *R128MMIO = info->MMIO;

   CARD32 timeout;
   
   timeout = INREG(VIPH_TIMEOUT_STAT);
   if(timeout & VIPH_TIMEOUT_STAT__VIPH_REG_STAT) /* lockup ?? */
   {
      OUTREG(VIPH_TIMEOUT_STAT, (timeout & 0xffffff00) | VIPH_TIMEOUT_STAT__VIPH_REG_AK);
      return (INREG(VIPH_CONTROL) & 0x2000) ? VIP_BUSY: VIP_RESET;
   }
   return (INREG(VIPH_CONTROL) & 0x2000) ? VIP_BUSY: VIP_IDLE; 
}

/* address format:
     ((device & 0x3)<<14)   | (fifo << 12) | (addr)
*/

static Bool R128VIP_read(GENERIC_BUS_Ptr b, CARD32 address, CARD32 count, CARD8 *buffer)
{
   ScrnInfoPtr pScrn = xf86Screens[b->scrnIndex];
   R128InfoPtr info = R128PTR(pScrn);
   unsigned char *R128MMIO = info->MMIO;
   CARD32 status;

   if((count!=1) && (count!=2) && (count!=4))
   {
   xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"Attempt to access VIP bus with non-stadard transaction length\n");
   return FALSE;
   }
   
   OUTREG(VIPH_REG_ADDR, address | 0x2000);
   while(VIP_BUSY == (status = R128VIP_idle(b)));
   if(VIP_IDLE != status) return FALSE;
   
/*
         disable VIPH_REGR_DIS to enable VIP cycle.
         The LSB of VIPH_TIMEOUT_STAT are set to 0
         because 1 would have acknowledged various VIP
         interrupts unexpectedly 
*/	
   OUTREG(VIPH_TIMEOUT_STAT, INREG(VIPH_TIMEOUT_STAT) & (0xffffff00 & ~VIPH_TIMEOUT_STAT__VIPH_REGR_DIS) );
/*
         the value returned here is garbage.  The read merely initiates
         a register cycle
*/
    INREG(VIPH_REG_DATA);
    
    while(VIP_BUSY == (status = R128VIP_idle(b)));
    if(VIP_IDLE != status) return FALSE;
/*
        set VIPH_REGR_DIS so that the read won't take too long.
*/
    OUTREG(VIPH_TIMEOUT_STAT, (INREG(VIPH_TIMEOUT_STAT) & 0xffffff00) | VIPH_TIMEOUT_STAT__VIPH_REGR_DIS);	      
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
     while(VIP_BUSY == (status = R128VIP_idle(b)));
     if(VIP_IDLE != status) return FALSE;
 /*	
 so that reading VIPH_REG_DATA would not trigger unnecessary vip cycles.
*/
     OUTREG(VIPH_TIMEOUT_STAT, (INREG(VIPH_TIMEOUT_STAT) & 0xffffff00) | VIPH_TIMEOUT_STAT__VIPH_REGR_DIS);
     return TRUE;
}

static Bool R128VIP_write(GENERIC_BUS_Ptr b, CARD32 address, CARD32 count, CARD8 *buffer)
{
    ScrnInfoPtr pScrn = xf86Screens[b->scrnIndex];
    R128InfoPtr info = R128PTR(pScrn);
    unsigned char *R128MMIO = info->MMIO;
    
    CARD32 status;


    if((count!=4))
    {
    xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Attempt to access VIP bus with non-stadard transaction length\n");
    return FALSE;
    }
    
    OUTREG(VIPH_REG_ADDR, address & (~0x2000));
    while(VIP_BUSY == (status = R128VIP_idle(b)));
    
    if(VIP_IDLE != status) return FALSE;
    
    switch(count){
        case 4:
	     OUTREG(VIPH_REG_DATA, *(CARD32 *)buffer);
	     break;
	}
    while(VIP_BUSY == (status = R128VIP_idle(b)));
    if(VIP_IDLE != status) return FALSE;
    return TRUE;
}

static int R128_eeprom_addresses[] = { 0xA8, 0x70, 0x40, 0x78, 0x72, 0x42, 0};

static void R128_read_eeprom(R128PortPrivPtr pPriv)
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

   for(i=0;R128_eeprom_addresses[i];i++)
   {
     d.SlaveAddr = R128_eeprom_addresses[i];
     data[0]=0x00;
     if(!I2C_WriteRead(&d, data, 1, NULL, 0))continue;
     if(!I2C_WriteRead(&d, NULL, 0, data, 5))continue;
     if(!memcmp(data, "ATI", 3))
     {
        pPriv->EEPROM_present = TRUE;
	pPriv->EEPROM_addr = R128_eeprom_addresses[i];
	break;
     }
     xf86DrvMsg(pPriv->i2c->scrnIndex, X_INFO, "Device at eeprom addr 0x%02x found, returned 0x%02x-0x%02x-0x%02x-0x%02x-0x%02x\n",
         d.SlaveAddr,
     	 data[0], data[1], data[2], data[3], data[4]);
   }


}

void R128VIP_reset(ScrnInfoPtr pScrn, R128PortPrivPtr pPriv)
{
    R128InfoPtr info = R128PTR(pScrn);
    unsigned char *R128MMIO = info->MMIO;

    R128WaitForFifo(pScrn, 7);
    OUTREG(VIPH_CONTROL, 0x003F0004); /* slowest, timeout in 16 phases */
    OUTREG(VIPH_TIMEOUT_STAT, (INREG(VIPH_TIMEOUT_STAT) & 0xFFFFFF00) | VIPH_TIMEOUT_STAT__VIPH_REGR_DIS);
    OUTREG(VIPH_DV_LAT, 0x444400FF); /* set timeslice */
    OUTREG(VIPH_BM_CHUNK, 0x151);
    OUTREG(R128_TEST_DEBUG_CNTL, INREG(R128_TEST_DEBUG_CNTL) & (~TEST_DEBUG_CNTL__TEST_DEBUG_OUT_EN));
    OUTREG(R128_MPP_GP_CONFIG, 0);
    OUTREG(R128_MPP_TB_CONFIG, 0);
}

void R128VIP_init(ScrnInfoPtr pScrn, R128PortPrivPtr pPriv)
{
    pPriv->VIP=xcalloc(1,sizeof(GENERIC_BUS_Rec));
    pPriv->VIP->scrnIndex=pScrn->scrnIndex;
    pPriv->VIP->DriverPrivate.ptr=pPriv;
    pPriv->VIP->ioctl=R128VIP_ioctl;
    pPriv->VIP->read=R128VIP_read;
    pPriv->VIP->write=R128VIP_write;

    R128VIP_reset(pScrn, pPriv);
}


static void R128ReadMM_TABLE(ScrnInfoPtr pScrn, R128PortPrivPtr pPriv)
{
     R128InfoPtr info = R128PTR(pScrn);
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
         xf86DrvMsg(pScrn->scrnIndex,X_INFO,"No MM_TABLE found\n",bios_header,mm_table);
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
         xf86DrvMsg(pScrn->scrnIndex,X_INFO,"No MM_TABLE found",bios_header,mm_table);
	 pPriv->MM_TABLE_valid = FALSE;
     } }
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

static int R128_addon_addresses[] = { 0x70, 0x40, 0x78, 0x72, 0x42, 0};

void R128_detect_addon(R128PortPrivPtr pPriv)
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

   for(i=0;R128_addon_addresses[i];i++)
   {
     d.SlaveAddr = R128_addon_addresses[i];
     data[0]=0xFF;
     if(!I2C_WriteRead(&d, data, 1, NULL, 0))continue;
     if(!I2C_WriteRead(&d, NULL, 0, data, 1))continue;
     if((data[0] == 0xFF) || (data[0] == 0x00))continue;
     pPriv->addon_board = TRUE;
     pPriv->board_control = R128_addon_addresses[i];
     pPriv->board_info = data[0];
     xf86DrvMsg(pPriv->i2c->scrnIndex, X_INFO, "Standalone board at addr 0x%02x found, returned 0x%02x\n",
         d.SlaveAddr,
     	 data[0]);
     R128_board_setmisc(pPriv);
     break;
   }


}

Bool R128SetupTheatre(ScrnInfoPtr pScrn, R128PortPrivPtr pPriv, TheatrePtr t)
{
    R128InfoPtr info = R128PTR(pScrn);
    R128PLLPtr  pll = &(info->pll);

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

	xf86DrvMsg(t->VIP->scrnIndex,X_INFO,"Rage Theatre: Connectors (detected): tuner=%d, composite=%d, svideo=%d\n",t->wTunerConnector, t->wComp0Connector, t->wSVideo0Connector);

	if(info->RageTheatreTunerPort>=0)t->wTunerConnector=info->RageTheatreTunerPort;
	if(info->RageTheatreCompositePort>=0)t->wComp0Connector=info->RageTheatreCompositePort;
	if(info->RageTheatreSVideoPort>=0)t->wSVideo0Connector=info->RageTheatreSVideoPort;
	
	xf86DrvMsg(t->VIP->scrnIndex,X_INFO,"Rage Theatre: Connectors (using): tuner=%d, composite=%d, svideo=%d\n",t->wTunerConnector, t->wComp0Connector, t->wSVideo0Connector);

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
R128AllocAdaptor(ScrnInfoPtr pScrn)
{
    XF86VideoAdaptorPtr adapt;
    R128InfoPtr info = R128PTR(pScrn);
    R128PortPrivPtr pPriv;
    unsigned char *R128MMIO = info->MMIO;

    if(!(adapt = xf86XVAllocateVideoAdaptorRec(pScrn)))
	return NULL;

    if(!(pPriv = xcalloc(1, sizeof(R128PortPrivRec) + sizeof(DevUnion))))
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
    pPriv->saturation = 0;
    pPriv->contrast = 0;
    pPriv->hue = 0;
    pPriv->currentBuffer = 0;

    pPriv->video_stream_active = FALSE;
    pPriv->encoding = 1;
    pPriv->frequency = 1000;
    pPriv->volume = -1000;
    pPriv->mute = TRUE;
    pPriv->v=0;
   
    pPriv->autopaint_colorkey = TRUE;
    /* choose ecp_div setting */
    if(info->ModeReg.dot_clock_freq < 12500) pPriv->ecp_div = 0;
    	   else
    if(info->ModeReg.dot_clock_freq < 25000) pPriv->ecp_div = 1;
    	   else pPriv->ecp_div = 2;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Dotclock is %g Mhz, setting ecp_div to %d\n", info->ModeReg.dot_clock_freq/100.0, pPriv->ecp_div);

    OUTPLL(R128_VCLK_ECP_CNTL, (INPLL(pScrn, R128_VCLK_ECP_CNTL) & 0xfffffCff) | (pPriv->ecp_div << 8));

    return adapt;
}

static XF86VideoAdaptorPtr
R128SetupImageVideo(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    R128InfoPtr info = R128PTR(pScrn);
    R128PortPrivPtr pPriv;
    XF86VideoAdaptorPtr adapt;

    info->accel->Sync(pScrn);

    if(info->adaptor != NULL){
    	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Reinitializing Xvideo subsystems\n");
	R128ResetVideo(pScrn);
	return info->adaptor;
	}

    if(!(adapt = R128AllocAdaptor(pScrn)))
	return NULL;
    pPriv = (R128PortPrivPtr)(adapt->pPortPrivates[0].ptr);
    
    R128ReadMM_TABLE(pScrn, pPriv);

    R128InitI2C(pScrn,pPriv);

       adapt->flags = VIDEO_OVERLAID_IMAGES | VIDEO_CLIP_TO_VIEWPORT;
       adapt->name = "ATI Rage128 Video Overlay";
       if(pPriv->theatre != NULL)
       {
          adapt->type = XvWindowMask | XvInputMask | XvImageMask | XvVideoMask;
          adapt->nEncodings = 13;
          adapt->pEncodings = InputVideoEncodings;
       } else 
       if(pPriv->bt829 != NULL)
       {
          adapt->type = XvWindowMask | XvInputMask | XvImageMask | XvVideoMask;
          adapt->nEncodings = 16;
	  adapt->pEncodings = InputVideoEncodings;
       } else
       {
          adapt->type = XvWindowMask | XvInputMask | XvImageMask;
          adapt->nEncodings = 1;
          adapt->pEncodings =  &DummyEncoding;
       }
       adapt->nFormats = NUM_FORMATS;
       adapt->pFormats = Formats;
       adapt->nPorts = 1;
       if(pPriv->msp3430!=NULL)
	       adapt->nAttributes = NUM_ATTRIBUTES;
	       else 
       if((pPriv->theatre!=NULL)||(pPriv->bt829!=NULL))
	       adapt->nAttributes = NUM_ATTRIBUTES-1;
	       else adapt->nAttributes =  NUM_ATTRIBUTES-3;
       adapt->pAttributes = Attributes;
       adapt->nImages = NUM_IMAGES;
       adapt->pImages = Images;
       adapt->PutVideo = R128PutVideo;
       adapt->PutStill = NULL;
       adapt->GetVideo = NULL;
       adapt->GetStill = NULL;
       adapt->StopVideo = R128StopVideo;
       adapt->SetPortAttribute = R128SetPortAttribute;
       adapt->GetPortAttribute = R128GetPortAttribute;
       adapt->QueryBestSize = R128QueryBestSize;
       adapt->PutImage = R128PutImage;
       adapt->QueryImageAttributes = R128QueryImageAttributes;
    

    info->adaptor = adapt;

    REGION_INIT(pScreen, &(pPriv->clip), NullBox, 0);

    R128ResetVideo(pScrn);

    if(info->VBIOS!=NULL){
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


/* R128ClipVideo -

   Takes the dst box in standard X BoxRec form (top and left
   edges inclusive, bottom and right exclusive).  The new dst
   box is returned.  The source boundaries are given (xa, ya
   inclusive, xb, yb exclusive) and returned are the new source
   boundaries in 16.16 fixed point.
*/

#define DummyScreen screenInfo.screens[0]

static Bool
R128ClipVideo(
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
R128StopVideo(ScrnInfoPtr pScrn, pointer data, Bool cleanup)
{
  R128InfoPtr info = R128PTR(pScrn);
  unsigned char *R128MMIO = info->MMIO;
  R128PortPrivPtr pPriv = (R128PortPrivPtr)data;

  REGION_EMPTY(pScrn->pScreen, &pPriv->clip);

  if(cleanup) {
     if(pPriv->videoStatus & CLIENT_VIDEO_ON) {
	OUTREG(R128_OV0_SCALE_CNTL, 0);
	xf86ForceHWCursor (pScrn->pScreen, FALSE);
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
R128SetPortAttribute(
  ScrnInfoPtr pScrn,
  Atom attribute,
  INT32 value,
  pointer data
){
  R128InfoPtr info = R128PTR(pScrn);
  unsigned char *R128MMIO = info->MMIO;
  R128PortPrivPtr pPriv = (R128PortPrivPtr)data;
  Bool save_mute;

  info->accel->Sync(pScrn);

  if(attribute == xv_autopaint_colorkey) {
  	pPriv->autopaint_colorkey = value;
  } else
  if(attribute == xv_set_defaults) {
        R128SetPortAttribute(pScrn, xv_autopaint_colorkey, TRUE, data);
        R128SetPortAttribute(pScrn, xvBrightness, 0, data);
        R128SetPortAttribute(pScrn, xvSaturation, 0, data);
        R128SetPortAttribute(pScrn, xvContrast,   0, data);
        R128SetPortAttribute(pScrn, xvHue,   0, data);
        R128SetPortAttribute(pScrn, xvVolume,   0, data);
        R128SetPortAttribute(pScrn, xvMute,   1, data);
        R128SetPortAttribute(pScrn, xvSAP,   0, data);
        R128SetPortAttribute(pScrn, xvDoubleBuffer,   1, data);
  } else
  if(attribute == xvBrightness) {
        if(value < -1000)value = -1000;
	if(value > 1000)value = 1000;
	
	pPriv->brightness = value;
	R128WaitForFifo(pScrn, 2);
	OUTREG(R128_OV0_COLOUR_CNTL, (((pPriv->brightness*64)/1000) & 0x7f) |
				     (((pPriv->saturation*31+31000)/2000) << 8) |
				     (((pPriv->saturation*31+31000)/2000) << 16));
	if(pPriv->theatre!=NULL) xf86_RT_SetBrightness(pPriv->theatre, pPriv->brightness);	
	if(pPriv->bt829!=NULL) xf86_bt829_SetBrightness(pPriv->bt829, pPriv->brightness);	
  } else
  if((attribute == xvSaturation) || (attribute == xvColor)) {
        if(value < -1000)value = -1000;
	if(value > 1000)value = 1000;
	pPriv->saturation = value;
	R128WaitForFifo(pScrn, 2);
	OUTREG(R128_OV0_COLOUR_CNTL, (((pPriv->brightness*64)/1000) & 0x7f) |
				     (((pPriv->saturation*31+31000)/2000) << 8) |
				     (((pPriv->saturation*31+31000)/2000) << 16));
	if(pPriv->theatre != NULL)xf86_RT_SetSaturation(pPriv->theatre, value);
	if(pPriv->bt829!=NULL) xf86_bt829_SetSaturation(pPriv->bt829, pPriv->saturation);	
  } else
  if(attribute == xvContrast) {
        if(value < -1000)value = -1000;
	if(value > 1000)value = 1000;
	pPriv->contrast = value;
	if(pPriv->theatre != NULL)xf86_RT_SetContrast(pPriv->theatre, value);
	if(pPriv->bt829!=NULL) xf86_bt829_SetContrast(pPriv->bt829, pPriv->contrast);	
  } else
  if(attribute == xvHue) {
        if(value < -1000)value = -1000;
	if(value > 1000)value = 1000;
	pPriv->hue = value;
	if(pPriv->theatre != NULL)xf86_RT_SetTint(pPriv->theatre, value);
	if(pPriv->bt829!=NULL) xf86_bt829_SetTint(pPriv->bt829, pPriv->hue);	
  } else
  if(attribute == xvDoubleBuffer) {
	if((value < 0) || (value > 1))
	   return BadValue;
	pPriv->doubleBuffer = value;
  } else
  if(attribute == xvColorKey) {
	pPriv->colorKey = value;
	R128WaitForFifo(pScrn, 2);
	OUTREG(R128_OV0_GRAPHICS_KEY_CLR, pPriv->colorKey);

	REGION_EMPTY(pScrn->pScreen, &pPriv->clip);
  } else 
  if(attribute == xvEncoding) {
	pPriv->encoding = value;
	if(pPriv->video_stream_active)
	{
	   if(pPriv->theatre != NULL) R128_RT_SetEncoding(pPriv);
	   if(pPriv->bt829 != NULL) R128_BT_SetEncoding(pPriv);
	   if(pPriv->msp3430 != NULL) R128_MSP_SetEncoding(pPriv);
           if(pPriv->i2c!=NULL) R128_board_setmisc(pPriv);
	/* put more here to actually change it */
	}
  } else 
  if(attribute == xvFrequency) {
        pPriv->frequency = value;
	save_mute = pPriv->mute;
	R128MuteAudio(pPriv, TRUE);
  	if(pPriv->fi1236 != NULL) xf86_TUNER_set_frequency(pPriv->fi1236, value);
	if((pPriv->msp3430 != NULL) && (pPriv->msp3430->recheck))
		xf86_InitMSP3430(pPriv->msp3430);
	R128MuteAudio(pPriv, save_mute);
  } else 
  if(attribute == xvMute) {
        pPriv->mute = value;
        R128MuteAudio(pPriv, pPriv->mute);
        if(pPriv->i2c!=NULL) R128_board_setmisc(pPriv);
  } else 
  if(attribute == xvSAP) {
        pPriv->sap_channel = value;
	if(pPriv->msp3430!=NULL)xf86_MSP3430SetSAP(pPriv->msp3430, pPriv->sap_channel?4:3);
	if(pPriv->tda9850!=NULL)xf86_tda9850_sap_mute(pPriv->tda9850, pPriv->sap_channel?1:0);
  } else 
  if(attribute == xvVolume) {
  	if(value<-1000) value=-1000;
	if(value>1000) value=1000;
        pPriv->volume = value;
	pPriv->mute = FALSE;
        if(pPriv->msp3430 != NULL) xf86_MSP3430SetVolume(pPriv->msp3430, MSP3430_VOLUME(value));
        R128MuteAudio(pPriv, pPriv->mute);
        if(pPriv->i2c!=NULL) R128_board_setmisc(pPriv);
  } else 
     return BadMatch;

  return Success;
}

static int
R128GetPortAttribute(
  ScrnInfoPtr pScrn,
  Atom attribute,
  INT32 *value,
  pointer data
){
  R128InfoPtr info = R128PTR(pScrn);
  R128PortPrivPtr pPriv = (R128PortPrivPtr)data;

  info->accel->Sync(pScrn);

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
R128QueryBestSize(
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


/*
 *
 * R128DMA - abuse the texture blit ioctl to transfer rectangular blocks
 *
 * The block is split into 'passes' pieces of 'hpass' lines which fit entirely
 * into an indirect buffer
 *
 */

static Bool
R128DMA(
  ScrnInfoPtr pScrn,
  R128InfoPtr info,
  unsigned char *src,
  unsigned char *dst,
  int srcPitch,
  int dstPitch,
  int h,
  int w
){

#ifdef XF86DRI

#define BUFSIZE (R128_BUFFER_SIZE - R128_HOSTDATA_BLIT_OFFSET)
#define MAXPASSES (MAXHEIGHT/(BUFSIZE/(MAXWIDTH*2))+1)

    unsigned char *buf;
    int err=-1, i, idx, offset, hpass, passes, srcpassbytes, dstpassbytes;
    int sizes[MAXPASSES], list[MAXPASSES];
    drmDMAReq req;
    drmR128Blit blit;

    /* Verify conditions and bail out as early as possible */
    if (!info->directRenderingEnabled || !info->DMAForXv)
        return FALSE;

    if ((hpass = min(h,(BUFSIZE/w))) == 0)
	return FALSE;

    if ((passes = (h+hpass-1)/hpass) > MAXPASSES)
        return FALSE;

    R128CCEWaitForIdle(pScrn);

    /* Request indirect buffers */
    srcpassbytes = w*hpass;

    req.context		= info->drmCtx;
    req.send_count	= 0;
    req.send_list	= NULL;
    req.send_sizes	= NULL;
    req.flags		= DRM_DMA_LARGER_OK;
    req.request_count	= passes;
    req.request_size	= srcpassbytes + R128_HOSTDATA_BLIT_OFFSET;
    req.request_list	= &list[0];
    req.request_sizes	= &sizes[0];
    req.granted_count	= 0;

    if (drmDMA(info->drmFD, &req))
        return FALSE;

    if (req.granted_count < passes) {
        drmFreeBufs(info->drmFD, req.granted_count, req.request_list);
	return FALSE;
    }

    /* Copy parts of the block into buffers and fire them */ 
    dstpassbytes = hpass*dstPitch;
    dstPitch /= 8;
    for (i=0, offset=dst-info->FB; i<passes; i++, offset+=dstpassbytes) {
        if (i == (passes-1) && (h % hpass) != 0) {
	    hpass = h % hpass;
	    srcpassbytes = w*hpass;
	}

	idx = req.request_list[i];
	buf = (unsigned char *) info->buffers->list[idx].address + R128_HOSTDATA_BLIT_OFFSET;

	if (srcPitch == w) {
            memcpy(buf, src, srcpassbytes);
	    src += srcpassbytes;
	} else {
	    int count = hpass;
	    while(count--) {
	        memcpy(buf, src, w);
		src += srcPitch;
		buf += w;
	    }
	}

        blit.idx = idx;
        blit.offset = offset;
        blit.pitch = dstPitch;
        blit.format = (R128_DATATYPE_CI8 >> 16);
        blit.x = (offset % 32);
        blit.y = 0;
        blit.width = w;
        blit.height = hpass;

	if ((err = drmCommandWrite(info->drmFD, DRM_R128_BLIT,
                                   &blit, sizeof(drmR128Blit))) < 0)
	    break;
    }

    drmFreeBufs(info->drmFD, req.granted_count, req.request_list);

    return (err==0) ? TRUE : FALSE;

#else

    /* This is to avoid cluttering the rest of the code with '#ifdef XF86DRI' */
    return FALSE;

#endif	/* XF86DRI */

}


static void
R128CopyData422(
  ScrnInfoPtr pScrn,
  R128InfoPtr info,
  unsigned char *src,
  unsigned char *dst,
  int srcPitch,
  int dstPitch,
  int h,
  int w
){
    w <<= 1;

    /* Attempt data transfer with DMA and fall back to memcpy */

    if (!R128DMA(pScrn, info, src, dst, srcPitch, dstPitch, h, w)) {
        while(h--) {
	    memcpy(dst, src, w);
	    src += srcPitch;
	    dst += dstPitch;
	}
    }
}

static void
R128CopyData420(
   ScrnInfoPtr pScrn,
   R128InfoPtr info,
   unsigned char *src1,
   unsigned char *src2,
   unsigned char *src3,
   unsigned char *dst1,
   unsigned char *dst2,
   unsigned char *dst3,
   int srcPitch,
   int srcPitch2,
   int dstPitch,
   int h,
   int w
){
   int count;

   /* Attempt data transfer with DMA and fall back to memcpy */

   if (!R128DMA(pScrn,info, src1, dst1, srcPitch, dstPitch, h, w)) {
       count = h;
       while(count--) {
	   memcpy(dst1, src1, w);
	   src1 += srcPitch;
	   dst1 += dstPitch;
       }
   }

   w >>= 1;
   h >>= 1;
   dstPitch >>= 1;

   if (!R128DMA(pScrn,info, src2, dst2, srcPitch2, dstPitch, h, w)) {
       count = h;
       while(count--) {
	   memcpy(dst2, src2, w);
	   src2 += srcPitch2;
	   dst2 += dstPitch;
       }
   }

   if (!R128DMA(pScrn, info, src3, dst3, srcPitch2, dstPitch, h, w)) {
       count = h;
       while(count--) {
	   memcpy(dst3, src3, w);
	   src3 += srcPitch2;
	   dst3 += dstPitch;
       }
   }
}


static FBLinearPtr
R128AllocateMemory(
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
R128DisplayVideo422(
    ScrnInfoPtr pScrn,
    R128PortPrivPtr pPriv,
    int id,
    int offset1, int offset2,
    short width, short height,
    int pitch,
    int left, int right, int top,
    BoxPtr dstBox,
    short src_w, short src_h,
    short drw_w, short drw_h
){
    R128InfoPtr info = R128PTR(pScrn);
    unsigned char *R128MMIO = info->MMIO;
    int v_inc, h_inc, step_by, tmp;
    int p1_h_accum_init, p23_h_accum_init;
    int p1_v_accum_init;
    long counter;

    /* Choose ecp_div setting. As the user may have switched resolution
      we better do it again. */
    if(info->ModeReg.dot_clock_freq < 12500) pPriv->ecp_div = 0;
    	   else
    if(info->ModeReg.dot_clock_freq < 25000) pPriv->ecp_div = 1;
    	   else pPriv->ecp_div = 2;

    OUTPLL(R128_VCLK_ECP_CNTL, (INPLL(pScrn, R128_VCLK_ECP_CNTL) & 0xfffffCff) | (pPriv->ecp_div << 8));

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

    R128WaitForFifo(pScrn,2);
    OUTREG(R128_OV0_REG_LOAD_CNTL, R128_REG_LD_CTL_LOCK);
    R128WaitForIdle(pScrn);
    counter=10000;
    while(!(INREG(R128_OV0_REG_LOAD_CNTL) & R128_REG_LD_CTL_LOCK_READBACK) && (counter>=0))counter--;
    if(counter<0)xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Cannot lock overlay registers\n");

    OUTREG(R128_OV0_H_INC, h_inc | ((h_inc >> 1) << 16));
    OUTREG(R128_OV0_STEP_BY, step_by | (step_by << 8));
    OUTREG(R128_OV0_Y_X_START, dstBox->x1 | ((dstBox->y1*((pScrn->currentMode->Flags & V_DBLSCAN)?2:1)) << 16));
    OUTREG(R128_OV0_Y_X_END,   dstBox->x2 | ((dstBox->y2*((pScrn->currentMode->Flags & V_DBLSCAN)?2:1)) << 16));
    OUTREG(R128_OV0_V_INC, v_inc);
    OUTREG(R128_OV0_P1_BLANK_LINES_AT_TOP, 0x00000fff | ((src_h - 1) << 16));
    OUTREG(R128_OV0_VID_BUF_PITCH0_VALUE, pitch);
    OUTREG(R128_OV0_P1_X_START_END, (width - 1) | (left << 16));
    left >>= 1; width >>= 1;
    OUTREG(R128_OV0_P2_X_START_END, (width - 1) | (left << 16));
    OUTREG(R128_OV0_P3_X_START_END, (width - 1) | (left << 16));
    OUTREG(R128_OV0_VID_BUF0_BASE_ADRS, offset1 & 0xfffffff0);

    R128WaitForFifo(pScrn, 14);
    OUTREG(R128_OV0_VID_BUF1_BASE_ADRS, offset1 & 0xfffffff0);
    OUTREG(R128_OV0_VID_BUF2_BASE_ADRS, offset1 & 0xfffffff0);

    OUTREG(R128_OV0_VID_BUF3_BASE_ADRS, offset2 & 0xfffffff0);
    OUTREG(R128_OV0_VID_BUF4_BASE_ADRS, offset2 & 0xfffffff0);
    OUTREG(R128_OV0_VID_BUF5_BASE_ADRS, offset2 & 0xfffffff0);
    OUTREG(R128_OV0_P1_V_ACCUM_INIT, p1_v_accum_init);
    OUTREG(R128_OV0_P23_V_ACCUM_INIT, 0);
    OUTREG(R128_OV0_P1_H_ACCUM_INIT, p1_h_accum_init);
    OUTREG(R128_OV0_P23_H_ACCUM_INIT, p23_h_accum_init);
/*  older, original magic
    if(id == FOURCC_UYVY)
       OUTREG(R128_OV0_SCALE_CNTL, 0x41FF8C03);
    else
       OUTREG(R128_OV0_SCALE_CNTL, 0x41FF8B03);
*/
    if(id == FOURCC_UYVY)
       OUTREG(R128_OV0_SCALE_CNTL, R128_SCALER_PIX_EXPAND \
	       | R128_SCALER_Y2R_TEMP \
	       | R128_SCALER_SOURCE_YVYU422 \
	       | R128_SCALER_SMART_SWITCH \
	       | R128_SCALER_BURST_PER_PLANE \
	       | R128_SCALER_DOUBLE_BUFFER \
	       | R128_SCALER_ENABLE);
    else
       OUTREG(R128_OV0_SCALE_CNTL, R128_SCALER_PIX_EXPAND \
	       | R128_SCALER_Y2R_TEMP \
	       | R128_SCALER_SOURCE_VYUY422 \
	       | R128_SCALER_SMART_SWITCH \
	       | R128_SCALER_BURST_PER_PLANE \
	       | R128_SCALER_DOUBLE_BUFFER \
	       | R128_SCALER_ENABLE);
    OUTREG(R128_OV0_REG_LOAD_CNTL, 0);
}

static void
R128DisplayVideo420(
    ScrnInfoPtr pScrn,
    R128PortPrivPtr pPriv,
    short width, short height,
    int pitch,
    int offset1, int offset2, int offset3,
    int left, int right, int top,
    BoxPtr dstBox,
    short src_w, short src_h,
    short drw_w, short drw_h
){
    R128InfoPtr info = R128PTR(pScrn);
    unsigned char *R128MMIO = info->MMIO;
    int v_inc, h_inc, step_by, tmp, leftUV;
    int p1_h_accum_init, p23_h_accum_init;
    int p1_v_accum_init, p23_v_accum_init;
    long counter;

    /* Choose ecp_div setting. As the user may have switched resolution
      we better do it again. */
    if(info->ModeReg.dot_clock_freq < 12500) pPriv->ecp_div = 0;
    	   else
    if(info->ModeReg.dot_clock_freq < 25000) pPriv->ecp_div = 1;
    	   else pPriv->ecp_div = 2;

    OUTPLL(R128_VCLK_ECP_CNTL, (INPLL(pScrn, R128_VCLK_ECP_CNTL) & 0xfffffCff) | (pPriv->ecp_div << 8));

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

    offset1 += (left >> 16) & ~15;
    offset2 += (left >> 17) & ~15;
    offset3 += (left >> 17) & ~15;

    tmp = (left & 0x0003ffff) + 0x00028000 + (h_inc << 3);
    p1_h_accum_init = ((tmp <<  4) & 0x000f8000) |
		      ((tmp << 12) & 0xf0000000);

    tmp = ((left >> 1) & 0x0001ffff) + 0x00028000 + (h_inc << 2);
    p23_h_accum_init = ((tmp <<  4) & 0x000f8000) |
		       ((tmp << 12) & 0x70000000);

    tmp = (top & 0x0000ffff) + 0x00018000;
    p1_v_accum_init = ((tmp << 4) & 0x03ff8000) | 0x00000001;

    tmp = ((top >> 1) & 0x0000ffff) + 0x00018000;
    p23_v_accum_init = ((tmp << 4) & 0x01ff8000) | 0x00000001;

    leftUV = (left >> 17) & 15;
    left = (left >> 16) & 15;

    R128WaitForFifo(pScrn, 2);
    OUTREG(R128_OV0_REG_LOAD_CNTL, R128_REG_LD_CTL_LOCK);
    R128WaitForIdle(pScrn);
    counter=10000;
    while(!(INREG(R128_OV0_REG_LOAD_CNTL) & R128_REG_LD_CTL_LOCK_READBACK) && (counter>=0))counter--;
    if(counter<0)xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Cannot lock overlay registers\n");

    OUTREG(R128_OV0_H_INC, h_inc | ((h_inc >> 1) << 16));
    OUTREG(R128_OV0_STEP_BY, step_by | (step_by << 8));
    OUTREG(R128_OV0_Y_X_START, dstBox->x1 | ((dstBox->y1*((pScrn->currentMode->Flags & V_DBLSCAN)?2:1)) << 16));
    OUTREG(R128_OV0_Y_X_END,   dstBox->x2 | ((dstBox->y2*((pScrn->currentMode->Flags & V_DBLSCAN)?2:1)) << 16));
    OUTREG(R128_OV0_V_INC, v_inc);
    OUTREG(R128_OV0_P1_BLANK_LINES_AT_TOP, 0x00000fff | ((src_h - 1) << 16));
    src_h = (src_h + 1) >> 1;
    OUTREG(R128_OV0_P23_BLANK_LINES_AT_TOP, 0x000007ff | ((src_h - 1) << 16));
    OUTREG(R128_OV0_VID_BUF_PITCH0_VALUE, pitch);
    OUTREG(R128_OV0_VID_BUF_PITCH1_VALUE, pitch >> 1);
    OUTREG(R128_OV0_P1_X_START_END, (width - 1) | (left << 16));
    width >>= 1;
    R128WaitForFifo(pScrn,14);
    OUTREG(R128_OV0_P2_X_START_END, (width - 1) | (leftUV << 16));
    OUTREG(R128_OV0_P3_X_START_END, (width - 1) | (leftUV << 16));
    OUTREG(R128_OV0_VID_BUF0_BASE_ADRS, offset1 & 0xfffffff0);
    OUTREG(R128_OV0_VID_BUF1_BASE_ADRS, (offset2 & 0xfffffff0) | 0x00000001);
    OUTREG(R128_OV0_VID_BUF2_BASE_ADRS, (offset3 & 0xfffffff0) | 0x00000001);
    OUTREG(R128_OV0_P1_V_ACCUM_INIT, p1_v_accum_init);
    OUTREG(R128_OV0_P23_V_ACCUM_INIT, p23_v_accum_init);
    OUTREG(R128_OV0_P1_H_ACCUM_INIT, p1_h_accum_init);
    OUTREG(R128_OV0_P23_H_ACCUM_INIT, p23_h_accum_init);
/*  original magic 
    OUTREG(R128_OV0_SCALE_CNTL, 0x41FF8A03);
*/
    OUTREG(R128_OV0_SCALE_CNTL, R128_SCALER_PIX_EXPAND \
	       | R128_SCALER_Y2R_TEMP \
	       | R128_SCALER_SOURCE_YUV12 \
	       | R128_SCALER_SMART_SWITCH \
	       | R128_SCALER_BURST_PER_PLANE \
	       | R128_SCALER_DOUBLE_BUFFER \
	       | R128_SCALER_ENABLE );

    OUTREG(R128_OV0_REG_LOAD_CNTL, 0);
}



static int
R128PutImage(
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
   R128InfoPtr info = R128PTR(pScrn);
   R128PortPrivPtr pPriv = (R128PortPrivPtr)data;
   unsigned char *R128MMIO = info->MMIO;
   INT32 xa, xb, ya, yb;
   int pitch, new_size, offset, s1offset, s2offset, s3offset;
   int srcPitch, srcPitch2, dstPitch;
   int d1line, d2line, d3line, d1offset, d2offset, d3offset;
   int top, left, npixels, nlines, bpp;
   BoxRec dstBox;
   CARD32 tmp;

   info->accel->Sync(pScrn);

   /* if capture was active shutdown it first */
   if(pPriv->video_stream_active)
   {
	R128WaitForFifo(pScrn, 8); 
        OUTPLL(R128_FCP_CNTL, 0x404);
        OUTREG(R128_CAP0_TRIG_CNTL, 0x0);
	pPriv->video_stream_active = FALSE;
        R128MuteAudio(pPriv, TRUE);
   }   

   /*
    * s1offset, s2offset, s3offset - byte offsets to the Y, U and V planes
    *                                of the source.
    *
    * d1offset, d2offset, d3offset - byte offsets to the Y, U and V planes
    *                                of the destination.
    *
    * offset - byte offset within the framebuffer to where the destination
    *          is stored.
    *
    * d1line, d2line, d3line - byte offsets within the destination to the
    *                          first displayed scanline in each plane.
    *
    */

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

   if(!R128ClipVideo(&dstBox, &xa, &xb, &ya, &yb, clipBoxes, width, height))
	return Success;

   dstBox.x1 -= pScrn->frameX0;
   dstBox.x2 -= pScrn->frameX0;
   dstBox.y1 -= pScrn->frameY0;
   dstBox.y2 -= pScrn->frameY0;

   bpp = pScrn->bitsPerPixel >> 3;
   pitch = bpp * pScrn->displayWidth;

   R128WaitForFifo(pScrn, 4);
   OUTREG(R128_OV0_DEINTERLACE_PATTERN, 0xAAAAA);
   OUTREG(R128_OV0_AUTO_FLIP_CNTL, 0);
   OUTREG(R128_VIDEOMUX_CNTL, (INREG(R128_VIDEOMUX_CNTL)& ~3));
   OUTREG(R128_CAP0_CONFIG, 0);

   switch(id) {
   case FOURCC_YV12:
   case FOURCC_I420:
	srcPitch = (width + 3) & ~3;
	srcPitch2 = ((width >> 1) + 3) & ~3;
	dstPitch = (width + 31) & ~31;  /* of luma */
	new_size = ((dstPitch * (height + (height >> 1))) + bpp - 1) / bpp;
	s1offset = 0;
	s2offset = srcPitch * height;
	s3offset = (srcPitch2 * (height >> 1)) + s2offset;
	break;
   case FOURCC_UYVY:
   case FOURCC_YUY2:
   default:
	srcPitch = width << 1;
	srcPitch2 = 0;
	dstPitch = ((width << 1) + 15) & ~15;
	new_size = ((dstPitch * height) + bpp - 1) / bpp;
	s1offset = 0;
	s2offset = 0;
	s3offset = 0;
	break;
   }

   if(!(pPriv->linear = R128AllocateMemory(pScrn, pPriv->linear,
		pPriv->doubleBuffer ? (new_size << 1) : new_size)))
   {
	return BadAlloc;
   }

   pPriv->currentBuffer ^= 1;

    /* copy data */
   top = ya >> 16;
   left = (xa >> 16) & ~1;
   npixels = ((((xb + 0xffff) >> 16) + 1) & ~1) - left;

   offset = pPriv->linear->offset * bpp;
   if(pPriv->doubleBuffer)
	offset += pPriv->currentBuffer * new_size * bpp;

   
   info->accel->Sync(pScrn);
   switch(id) {
    case FOURCC_YV12:
    case FOURCC_I420:
	d1line = top * dstPitch;
	d2line = (height * dstPitch) + ((top >> 1) * (dstPitch >> 1));
	d3line = d2line + ((height >> 1) * (dstPitch >> 1));

	top &= ~1;

	d1offset = (top * dstPitch) + left + offset;
	d2offset = d2line + (left >> 1) + offset;
	d3offset = d3line + (left >> 1) + offset;

	s1offset += (top * srcPitch) + left;
	tmp = ((top >> 1) * srcPitch2) + (left >> 1);
	s2offset += tmp;
	s3offset += tmp;
	if(id == FOURCC_YV12) {
	   tmp = s2offset;
	   s2offset = s3offset;
	   s3offset = tmp;
	}

	nlines = ((((yb + 0xffff) >> 16) + 1) & ~1) - top;
	{
#if X_BYTE_ORDER == X_BIG_ENDIAN
	   unsigned char *R128MMIO = info->MMIO;
	   CARD32 config_cntl;

	   /* We need to disable byte swapping, or the data gets mangled */
	   config_cntl = INREG(R128_CONFIG_CNTL);
	   OUTREG(R128_CONFIG_CNTL, config_cntl &
		 ~(APER_0_BIG_ENDIAN_16BPP_SWAP|APER_0_BIG_ENDIAN_32BPP_SWAP));
#endif
	   R128CopyData420(pScrn, info, buf + s1offset, buf + s2offset, buf + s3offset,
			  info->FB+d1offset, info->FB+d2offset, info->FB+d3offset,
			  srcPitch, srcPitch2, dstPitch, nlines, npixels);
#if X_BYTE_ORDER == X_BIG_ENDIAN
	   /* restore byte swapping */
	   OUTREG(R128_CONFIG_CNTL, config_cntl);
#endif
	}
	break;
    case FOURCC_UYVY:
    case FOURCC_YUY2:
    default:
	left <<= 1;
	d1line = top * dstPitch;
	d2line = 0;
	d3line = 0;
	d1offset = d1line + left + offset;
	d2offset = 0;
	d3offset = 0;
	s1offset += (top * srcPitch) + left;
	nlines = ((yb + 0xffff) >> 16) - top;
	R128CopyData422(pScrn, info, buf + s1offset, info->FB + d1offset,
			srcPitch, dstPitch, nlines, npixels);
	break;
    }
    info->accel->Sync(pScrn);

    /* update cliplist */
    if(!RegionsEqual(&pPriv->clip, clipBoxes)) {
	REGION_COPY(pScreen, &pPriv->clip, clipBoxes);
	/* draw these */
	if(pPriv->autopaint_colorkey)(*info->accel->FillSolidRects)(pScrn, pPriv->colorKey, GXcopy,
					(CARD32)~0,
					REGION_NUM_RECTS(clipBoxes),
					REGION_RECTS(clipBoxes));
    }

    info->accel->Sync(pScrn);

    switch(id) {
     case FOURCC_YV12:
     case FOURCC_I420:
	R128DisplayVideo420(pScrn, pPriv, width, height, dstPitch,
		     offset + d1line, offset + d2line, offset + d3line,
		     xa, xb, ya, &dstBox, src_w, src_h, drw_w, drw_h);
	break;
     case FOURCC_UYVY:
     case FOURCC_YUY2:
     default:
	R128DisplayVideo422(pScrn, pPriv, id, offset + d1line, offset + d1line, width, height, dstPitch,
		     xa, xb, ya, &dstBox, src_w, src_h, drw_w, drw_h);
	break;
    }

    if (!(pPriv->videoStatus & CLIENT_VIDEO_ON))
	xf86ForceHWCursor (pScrn->pScreen, TRUE);
    pPriv->videoStatus = CLIENT_VIDEO_ON;

    info->VideoTimerCallback = R128VideoTimerCallback;

    return Success;
}


static int
R128QueryImageAttributes(
    ScrnInfoPtr pScrn,
    int id,
    unsigned short *w, unsigned short *h,
    int *pitches, int *offsets
){
    int size, tmp;

    if(*w > MAXWIDTH) *w = MAXWIDTH;
    if(*h > MAXHEIGHT) *h = MAXHEIGHT;

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

/* this routine is here because different mach64/r128 boards use
   different methods of muting. And because sometimes there is
   no good way to autodetect this (besides knowing which board
   we have) */



void R128_board_setmisc(R128PortPrivPtr pPriv)
{
    CARD8 a;
    I2CDevRec d;

    d.DevName = "temporary";
    d.pI2CBus = pPriv->i2c;
    d.NextDev = NULL;
    d.StartTimeout = pPriv->i2c->StartTimeout;
    d.BitTimeout = pPriv->i2c->BitTimeout;
    d.AcknTimeout = pPriv->i2c->AcknTimeout;
    d.ByteTimeout = pPriv->i2c->ByteTimeout;

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
        if((pPriv->encoding>=7)&&(pPriv->encoding<=9)) /*SECAM*/
	{
    	   pPriv->fi1236->parm.band_low = 0xA3;
	   pPriv->fi1236->parm.band_mid = 0x93;
	   pPriv->fi1236->parm.band_high = 0x33;
	}
    }

    R128MuteAudio(pPriv, pPriv->mute);
}


void R128_RT_SetEncoding(R128PortPrivPtr pPriv)
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

void R128_MSP_SetEncoding(R128PortPrivPtr pPriv)
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

void R128_BT_SetEncoding(R128PortPrivPtr pPriv)
{
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
       case 13:
                xf86_bt829_SetMux(pPriv->bt829, BT829_MUX2);
                pPriv->v=23;
                xf86_bt829_SetFormat(pPriv->bt829, BT829_PAL_M);
                break;
       case 14:
                xf86_bt829_SetMux(pPriv->bt829, BT829_MUX0);
                pPriv->v=23;
                xf86_bt829_SetFormat(pPriv->bt829, BT829_PAL_M);
                break;
       case 15:
                xf86_bt829_SetMux(pPriv->bt829, BT829_MUX1);
                pPriv->v=23;
                xf86_bt829_SetFormat(pPriv->bt829, BT829_PAL_M);
                break;
	default:
                xf86_bt829_SetMux(pPriv->bt829, BT829_MUX2);
		pPriv->v=23;
	        xf86_bt829_SetFormat(pPriv->bt829, BT829_NTSC);
		return;
	}	
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


/* Older, original magic.. let's leave these for reference 
#define ENABLE_R128_CAPTURE_WEAVE (0x1C000005L \
		| (BUF_MODE_SINGLE <<7) \
		| (BUF_TYPE_FRAME << 4) \
		| ( (pPriv->theatre !=NULL)?(1L<<23):0) \
		| (1<<29)) 
#define ENABLE_R128_CAPTURE_BOB (0x1C000005L \
		| (BUF_MODE_SINGLE <<7) \
		| (BUF_TYPE_ALTERNATING << 4) \
		| ( (pPriv->theatre !=NULL)?(1L<<23):0) \
		| (0<<15) \
		| (1<<29)) 

*/

#define ENABLE_R128_CAPTURE_WEAVE (R128_CAP0_CONFIG_CONTINUOS \
			| (BUF_MODE_SINGLE <<7) \
			| (BUF_TYPE_FRAME << 4) \
			| ( (pPriv->theatre !=NULL) ? \
				(R128_CAP0_CONFIG_FORMAT_CCIR656): \
				(R128_CAP0_CONFIG_FORMAT_BROOKTREE)) \
			| R128_CAP0_CONFIG_HORZ_DECIMATOR \
			| R128_CAP0_CONFIG_VIDEO_IN_VYUY422)

#define ENABLE_R128_CAPTURE_BOB (R128_CAP0_CONFIG_CONTINUOS \
			| (BUF_MODE_SINGLE <<7)  \
			| (BUF_TYPE_ALTERNATING << 4) \
			| ( (pPriv->theatre !=NULL) ? \
				(R128_CAP0_CONFIG_FORMAT_CCIR656): \
				(R128_CAP0_CONFIG_FORMAT_BROOKTREE)) \
			| R128_CAP0_CONFIG_HORZ_DECIMATOR \
			| R128_CAP0_CONFIG_VIDEO_IN_VYUY422)

static int
R128PutVideo(
  ScrnInfoPtr pScrn,
  short src_x, short src_y,
  short drw_x, short drw_y,
  short src_w, short src_h,
  short drw_w, short drw_h,
  RegionPtr clipBoxes, pointer data
){
   R128InfoPtr info = R128PTR(pScrn);
   R128PortPrivPtr pPriv = (R128PortPrivPtr)data;
   unsigned char *R128MMIO = info->MMIO;
   INT32 xa, xb, ya, yb, top;
   int pitch, new_size, offset1, offset2, s2offset, s3offset;
   int srcPitch, srcPitch2, dstPitch;
   int bpp;
   BoxRec dstBox;
   CARD32 id;
   int width, height;

   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "PutVideo\n");
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

   if(pPriv->theatre != NULL)
   {
      width = InputVideoEncodings[pPriv->encoding].width;
      height = InputVideoEncodings[pPriv->encoding].height; 
   } else 
   if(pPriv->bt829 != NULL)
   {
      width = InputVideoEncodings[pPriv->encoding].width;
      height = InputVideoEncodings[pPriv->encoding].height;
   } else 
      return FALSE;
        
   if(!R128ClipVideo(&dstBox, &xa, &xb, &ya, &yb, clipBoxes, width, height))
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

   if(!(pPriv->linear = R128AllocateMemory(pScrn, pPriv->linear, new_size*2)))
   {
	return BadAlloc;
   }


   R128WaitForFifo(pScrn, 14);

   offset1 = (pPriv->linear->offset*bpp+0xf) & (~0xf);
   offset2 = ((pPriv->linear->offset+new_size)*bpp + 0x1f) & (~0xf);
   
   OUTREG(R128_CAP0_BUF0_OFFSET, offset1);
   OUTREG(R128_CAP0_BUF0_EVEN_OFFSET, offset2);
   OUTREG(R128_CAP0_ONESHOT_BUF_OFFSET, offset1);
   OUTREG(R128_CAP0_BUF1_OFFSET, offset1);
   OUTREG(R128_CAP0_BUF1_EVEN_OFFSET, offset2);
   
   OUTREG(R128_CAP0_BUF_PITCH, width*2);
   OUTREG(R128_CAP0_H_WINDOW, (2*width)<<16);
   OUTREG(R128_CAP0_V_WINDOW, (((height)+pPriv->v-1)<<16)|(pPriv->v));
   OUTREG(R128_OV0_AUTO_FLIP_CNTL, R128_OV0_AUTO_FLIP_CNTL_SOFT_BUF_ODD);
   OUTREG(R128_CAP0_CONFIG, ENABLE_R128_CAPTURE_BOB);
   OUTREG(R128_CAP0_DEBUG, 0);
   
   OUTREG(R128_CAP0_DWNSC_XRATIO, 0x10001000);
   OUTREG(R128_CAP0_XSHARPNESS, 0);
   OUTREG(R128_TEST_DEBUG_CNTL, 0);
   
   if(! pPriv->video_stream_active){

   /* activate capture unit */
   R128WaitForFifo(pScrn, 4);
   
   /* undocumented magic, note: these work.. unlike the values in ATI kit,
      ATI has been notified.. */
   OUTREG(R128_VIDEOMUX_CNTL, (INREG(R128_VIDEOMUX_CNTL)| 2 |1));

   OUTREG(R128_CAP0_PORT_MODE_CNTL, (pPriv->theatre!=NULL)? 1: 0);
   
   OUTPLL(R128_FCP_CNTL, 0x101);

   OUTREG(R128_CAP0_TRIG_CNTL, R128_CAP0_TRIG_CNTL_TRIGGER_SET | 
                               R128_CAP0_TRIG_CNTL_CAPTURE_EN);
   
   
   if(pPriv->theatre != NULL) 
   {
      xf86_RT_SetInterlace(pPriv->theatre, 1);
      R128_RT_SetEncoding(pPriv);
      xf86_RT_SetOutputVideoSize(pPriv->theatre, width, height*2, 0, 0);   
   }
   
   if(pPriv->bt829 != NULL) 
   {
      R128_BT_SetEncoding(pPriv);
      xf86_bt829_SetCaptSize(pPriv->bt829, width, height*2);
   }
   
   if(pPriv->i2c!=NULL) R128_board_setmisc(pPriv);
   if(pPriv->msp3430 != NULL) R128_MSP_SetEncoding(pPriv);
   if(pPriv->tda9850 != NULL)
   { 
      xf86_tda9850_mute(pPriv->tda9850, pPriv->mute);
   }
   }
    /* update cliplist */
    if(!RegionsEqual(&pPriv->clip, clipBoxes)) {
	REGION_COPY(pScreen, &pPriv->clip, clipBoxes);
	/* draw these */
	if(pPriv->autopaint_colorkey)(*info->accel->FillSolidRects)(pScrn, pPriv->colorKey, GXcopy, ~0,
					REGION_NUM_RECTS(clipBoxes),
					REGION_RECTS(clipBoxes));
    }

    pPriv->doubleBuffer = 1;
    R128DisplayVideo422(pScrn, pPriv, id, offset1 + top*srcPitch, offset2 + top*srcPitch, width, height, dstPitch,
		     xa, xb, ya, &dstBox, src_w, src_h, drw_w, drw_h);

    R128WaitForIdle(pScrn);

    OUTREG(R128_OV0_REG_LOAD_CNTL, R128_REG_LD_CTL_LOCK);
    while(!(INREG(R128_OV0_REG_LOAD_CNTL) &  R128_REG_LD_CTL_LOCK_READBACK));

    OUTREG(R128_OV0_AUTO_FLIP_CNTL, R128_OV0_AUTO_FLIP_CNTL_SOFT_BUF_ODD|R128_OV0_AUTO_FLIP_CNTL_SHIFT_ODD_DOWN);

    OUTREG (R128_OV0_AUTO_FLIP_CNTL, (INREG (R128_OV0_AUTO_FLIP_CNTL) ^ R128_OV0_AUTO_FLIP_CNTL_SOFT_EOF_TOGGLE  ));
    OUTREG (R128_OV0_AUTO_FLIP_CNTL, (INREG (R128_OV0_AUTO_FLIP_CNTL) ^ R128_OV0_AUTO_FLIP_CNTL_SOFT_EOF_TOGGLE  ));

    OUTREG(R128_OV0_DEINTERLACE_PATTERN, 0xAAAAA);

    OUTREG(R128_OV0_REG_LOAD_CNTL, 0);

    pPriv->videoStatus = CLIENT_VIDEO_ON;
    pPriv->video_stream_active = TRUE;

    info->VideoTimerCallback = R128VideoTimerCallback;

    return Success;
}

static void
R128VideoTimerCallback(ScrnInfoPtr pScrn, Time now)
{
    R128InfoPtr info = R128PTR(pScrn);
    R128PortPrivPtr pPriv = info->adaptor->pPortPrivates[0].ptr;

    if(pPriv->videoStatus & TIMER_MASK) {
	if(pPriv->videoStatus & OFF_TIMER) {
	    if(pPriv->offTime < now) {
		unsigned char *R128MMIO = info->MMIO;
		OUTREG(R128_OV0_SCALE_CNTL, 0);
		if (pPriv->videoStatus & CLIENT_VIDEO_ON)
		    xf86ForceHWCursor (pScrn->pScreen, FALSE);
		pPriv->videoStatus = FREE_TIMER;
		pPriv->freeTime = now + FREE_DELAY;
	    }
	} else {  /* FREE_TIMER */
	    if(pPriv->freeTime < now) {
		if(pPriv->linear) {
		   xf86FreeOffscreenLinear(pPriv->linear);
		   pPriv->linear = NULL;
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


#endif  /* !XvExtension */
