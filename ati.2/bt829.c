#include "xf86.h"
#include "xf86i2c.h"
#include "bt829.h"
#include "i2c_def.h"

#define BTREAD(R)	btread(bt,(R))
#define BTWRITE(R,V)	btwrite(bt,(R),(V))
#define H(X)		( ((X)>>8) & 0xFF )
#define L(X)		( (X) & 0xFF )


#define LIMIT(X,A,B)  (((X)<(A)) ? (A) : ((X)>(B)) ? (B) : (X) )


/* Bt829 family chip ID's */
#define BT815	0x02
#define BT817	0x06
#define BT819	0x07
#define BT827	0x0C
#define BT829	0x0E

/* Bt829 registers */
#define STATUS		0x00	/* Device Status */
#define IFORM		0x01	/* Input Format */
#define TDEC		0x02	/* Temporal Decimation */
#define CROP		0x03	/* MSB Cropping */
#define VDELAY_LO	0x04	/* Vertical Delay */
#define VACTIVE_LO	0x05	/* Vertical Active */
#define HDELAY_LO	0x06	/* Horizontal Delay */
#define HACTIVE_LO	0x07	/* Horizontal Active */
#define HSCALE_HI	0x08	/* Horizontal Scaling */
#define HSCALE_LO	0x09	/* Horizontal Scaling */
#define BRIGHT		0x0A	/* Brightness Control */
#define CONTROL		0x0B	/* Miscellaneous Control */
#define CONTRAST_LO	0x0C	/* Luma Gain (Contrast) */
#define SAT_U_LO	0x0D	/* Chroma (U) Gain (Saturation) */
#define SAT_V_LO	0x0E	/* Chroma (V) Gain (Saturation) */
#define HUE		0x0F	/* Hue Control */
#define SCLOOP		0x10	/* SC Loop Control */
#define WC_UP		0x11	/* White Crush Up Count */
#define OFORM		0x12	/* Output Format */
#define VSCALE_HI	0x13	/* Vertical Scaling */
#define VSCALE_LO	0x14	/* Vertical Scaling */
#define TEST		0x15	/* Test Control */
#define VPOLE		0x16	/* Video Timing Polarity */
#define IDCODE		0x17	/* ID Code */
#define ADELAY		0x18	/* AGC Delay */
#define BDELAY		0x19	/* Burst Gate Delay */
#define ADC		0x1A	/* ADC Interface */
#define VTC		0x1B	/* Video Timing Control */
#define CC_STATUS	0x1C	/* Extended Data Services/Closed Capt Status */
#define CC_DATA		0x1D	/* Extended Data Services/Closed Capt Data */
#define WC_DN		0x1E	/* White Crush Down Count */
#define SRESET		0x1F	/* Software Reset */
#define P_IO		0x3F	/* Programmable I/O */

void bt829_crop(BT829Ptr bt);
void bt829_ctrl(BT829Ptr bt);
void bt829_iform(BT829Ptr bt);


static CARD8 btread(BT829Ptr bt, CARD8 reg)
{
  CARD8 v;

  I2C_WriteRead(&(bt->d), &reg, 1, &v, 1);

  return v;
}

static void btwrite(BT829Ptr bt, CARD8 reg, CARD8 val)
{
  CARD8 data[2];

  data[0] = reg;
  data[1] = val;
  I2C_WriteRead(&(bt->d), data, 2, NULL, 0);
}

BT829Ptr Detect_bt829(I2CBusPtr b, I2CSlaveAddr addr)
{
  BT829Ptr bt;
  I2CByte a;
  
  bt = xcalloc(1, sizeof(BT829Rec));
  if(bt == NULL) return NULL;
  bt->d.DevName = strdup("BT829 video decoder");
  bt->d.SlaveAddr = addr;
  bt->d.pI2CBus = b;
  bt->d.NextDev = NULL;
  bt->d.StartTimeout = b->StartTimeout;
  bt->d.BitTimeout = b->BitTimeout;
  bt->d.AcknTimeout = b->AcknTimeout;
  bt->d.ByteTimeout = b->ByteTimeout;

  
  if(!I2C_WriteRead(&(bt->d), NULL, 0, &a, 1))
  {
     free(bt);
     return NULL;
  }
  
  bt->id = BTREAD(IDCODE);
  
  free(bt->d.DevName);
  bt->d.DevName = xcalloc(200, sizeof(char));
  switch((bt->id)>>4){
  	case BT815:
		sprintf(bt->d.DevName, "bt815a video decoder, revision %d",bt->id & 0xf);
		break;
	case BT817:
		sprintf(bt->d.DevName, "bt817a video decoder, revision %d",bt->id & 0xf);
  		break;
	case BT819:
		sprintf(bt->d.DevName, "bt819a video decoder, revision %d",bt->id & 0xf);
  		break;
	case BT827:
		sprintf(bt->d.DevName, "bt827a/b video decoder, revision %d",bt->id & 0xf);
  		break;
	case BT829:
		sprintf(bt->d.DevName, "bt829a/b video decoder, revision %d",bt->id & 0xf);
  		break;
	default:
		sprintf(bt->d.DevName, "bt8xx/unknown video decoder version %d, revision %d",bt->id >> 4,bt->id & 0xf);
  		break;
	}

  bt->xtsel=0;
  bt->htotal=0; 
  bt->vtotal=0; 
  bt->ldec=1; 
  bt->cbsense=0;
  bt->adelay=0; 
  bt->bdelay=0; 
  bt->hdelay=0; 
  bt->vdelay=22;
  bt->luma=0; 
  bt->sat_u=0; 
  bt->sat_v=0;

  bt->vpole=1; /* all-in-wonder 128 uses vpole=1, other cards
                  (like All-in-Wonder classic) use vpole=0 */

  bt->tunertype=1;
  bt->format=BT829_NTSC;
  /* set default parameters */
  if(!I2CDevInit(&(bt->d)))
  {
     free(bt);
     return NULL;
  }
  return bt;
}

Bool bt829_init(BT829Ptr bt) 
{
  	
  BTWRITE(SRESET,0x0);
  BTWRITE(VPOLE,(bt->vpole)<<7) ;

  bt829_setformat(bt, bt->format);
  return TRUE;  
}

void bt829_setformat(BT829Ptr bt, int format) 
{

  /* NTSC, PAL or SECAM ? */
  bt->format=format;
  switch (bt->format) {
    case 2: case 4:						/* NTSC */
      if ((bt->id>>4) <= BT819) return; /* can't do it */
    case 1:							/* NTSC */
      bt->adelay = 104 ; 
      bt->bdelay = 93 ; 
      bt->xtsel = 1 ; 
      bt->vdelay = 22 ;
      bt->htotal = 754 ; 
      bt->vtotal = 480 ; 
      break ;
    case 5: case 7:						/* PAL */
      if ((bt->id>>4) <= BT819) return ; /* can't do it */
    case 3:							/* PAL */
      bt->vdelay = (bt->tunertype==5) ? 34 : 22 ;
      bt->adelay = 127 ; 
      bt->bdelay = 114 ; 
      bt->xtsel = 2 ;
/*      bt->htotal = 888 ;  */
	bt->htotal=922;  /* according to Marko..*/
      bt->vtotal = 576 ; 
      break ;
    case 6:							/* SECAM */
      if ((bt->id>>4) <= BT819) return; /* can't do it */
      bt->adelay = 127 ; 
      bt->bdelay = 160 ; 
      bt->xtsel = 2 ; 
      bt->vdelay = 34 ;
/*      bt->htotal = 888 ; */
      bt->htotal=922;  /* according to Marko..*/
      bt->vtotal = 576 ; 
      break ; 
    }

  /* Program the Bt829 */
  bt829_iform(bt) ;
  BTWRITE(TDEC,0x00) ;
  BTWRITE(VDELAY_LO,L(bt->vdelay)) ;
  BTWRITE(OFORM,0x0A) ;
  BTWRITE(ADELAY,bt->adelay) ;
  BTWRITE(BDELAY,bt->bdelay) ;
  BTWRITE(ADC,0x82) ;
  if ((bt->id>>4) >= BT827) {
    BTWRITE(SCLOOP,(format==6)?0x10:0x00) ;
    BTWRITE(WC_UP,0xCF) ;
    bt829_setCC(bt) ;
    BTWRITE(WC_DN,0x7F) ;
    BTWRITE(P_IO,0x00) ; }
  bt829_setmux(bt) ;
  bt829_SetBrightness(bt,bt->iBrightness) ;
  bt829_SetContrast(bt,bt->iContrast) ;
  bt829_SetSaturation(bt,bt->iSaturation) ;
  bt829_SetTint(bt,bt->iHue) ;
}

void bt829_setmux(BT829Ptr bt) 
{ 
if(bt->mux==2){
	btwrite(bt,P_IO,1);
	} else {
	btwrite(bt,P_IO,0);
	}
if(bt->mux==3){
	btwrite(bt,ADC,0x80);
	} else {
	btwrite(bt,ADC,0x82);
	}
bt829_ctrl(bt) ; 
bt829_iform(bt) ; 
}

void bt829_SetBrightness(BT829Ptr bt, int b) 
{
  bt->iBrightness=b; 
  b = (127*(b))/1000 ;
  b = LIMIT(b,-128,127) ; 
  BTWRITE(BRIGHT,b) ;; 
}

void bt829_SetContrast(BT829Ptr bt, int c) 
{
  bt->iContrast=c;
  c = (216*(c+1000))/1000 ; 
  bt->luma = LIMIT(c,0,511) ;
  bt829_ctrl(bt) ; 
  BTWRITE(CONTRAST_LO,L(bt->luma)) ; 
}

void bt829_SetSaturation(BT829Ptr bt, int s)
{
  bt->iSaturation=s;
  s=(s+1000)/10;
  s = LIMIT(s, 0, 200);
  bt->sat_u = 254*s/100 ; bt->sat_v = 180*s/100 ;
  bt829_ctrl(bt) ; 
  BTWRITE(SAT_U_LO,L(bt->sat_u)) ; 
  BTWRITE(SAT_V_LO,L(bt->sat_v)) ;
}

void bt829_SetTint(BT829Ptr bt, int h) 
{
  bt->iHue=h;
  h = 128*h/1000 ; h = LIMIT(h,-128,127) ;
  BTWRITE(HUE,h) ;   
}

void bt829_setcaptsize(BT829Ptr bt, int width, int height) 
{

  CARD16 hscale, vscale ; CARD8 yci=0x60, vfilt=0 ;
#if 0
  xf86DrvMsg(bt->d.pI2CBus->scrnIndex, X_INFO, "bt829_setcaptsize %dx%d\n", width, height);
#endif  
  bt->width = width;
  bt->height = height;
  /* NTSC, PAL or SECAM ? */
  switch (bt->format) {
    case 1: case 2: case 4:				/* NTSC */
      bt->hdelay = bt->width*135/754 ;
      if (bt->width <= 240) vfilt = 1 ;
      if (bt->width <= 120) vfilt = 2 ;
      if (bt->width <=  60) vfilt = 3 ; break ;
    case 3: case 5: case 6: case 7:			/* PAL/SECAM */
/*      bt->hdelay = bt->width*176/768 ;
	Marko says should be 186/922 */
      bt->hdelay = bt->width*186/922 ;
      if (bt->width <= 384) vfilt = 1 ;
      if (bt->width <= 192) vfilt = 2 ;
      if (bt->width<=  96) vfilt = 3 ; break ;
    default: return ; /* can't do it */ }

  bt->ldec = (bt->width> 384) ;
  bt->cbsense = bt->hdelay & 1 ;
  hscale = 4096*bt->htotal/bt->width-4096 ;
  vscale = (0x10000 - (512*bt->vtotal/bt->height-512)) & 0x1FFF ;

  bt829_crop(bt) ; bt829_ctrl(bt) ;
  BTWRITE(VACTIVE_LO,L(bt->vtotal)) ;
  BTWRITE(HDELAY_LO,L(bt->hdelay)) ;
  BTWRITE(HACTIVE_LO,L(bt->width)) ;
  BTWRITE(HSCALE_HI,H(hscale)) ;
  BTWRITE(HSCALE_LO,L(hscale)) ;
  BTWRITE(VSCALE_HI,H(vscale)|yci) ;
  BTWRITE(VSCALE_LO,L(vscale)) ;
  if ((bt->id>>4) >= BT827) BTWRITE(VTC,0x18|vfilt) ;

}

int bt829_setCC(BT829Ptr bt) 
{
  if ((bt->id >>4) < BT827) {
    bt->CCmode = 0;
    return -1; }
  if (bt->CCmode == 0) { /* Powering off circuitry */
    BTWRITE(CC_STATUS,0x00);
     return 0; }
  /* 0x40 is activate to set the CCVALID line. Not required yet */
  BTWRITE(CC_STATUS,(bt->CCmode<<4)|0x40);
/* we write to STATUS to reset the CCVALID flag */
  BTWRITE(STATUS,0x00);
  return 0; 
}

#if 0

void bt829_getCCdata(BT829Ptr bt,struct CCdata *data)
{
  CARD8 status;
  data->num_valid=0;
  /* wait for buffer to be half full (means 8/16 bytes)
   * either 4 (one of CC/EDS) or 2 (both CC/EDS) frames */
  if(!(BTREAD(STATUS)&0x04)) return; /* could comment this line */
  for(;data->num_valid<CC_FIFO_SIZE;data->num_valid++) {
    status=BTREAD(CC_STATUS);
    if(!(status&0x04)) break;
    data->data[data->num_valid]= BTREAD(CC_DATA)&0x7f;
                         /* stripped high bit (parity) */
    data->status[data->num_valid]= (CCS_EDS*((status&0x02)>>1))  |
                                 (CCS_HIGH*(status&0x01)) |
                                 (CCS_OVER*((status&0x08)>>3)) |
                                 (CCS_PAR*((status&0x80)>>7)) ; }
  BTWRITE(STATUS,0x00); /* Reset CCVALID status bit */
  return;
}

#endif

/* ------------------------------------------------------------------------ */
/* Debug and report routines */

#define DUMPREG(REG)   \
  xf86DrvMsg(bt->d.pI2CBus->scrnIndex,X_INFO," %-12s (0x%02X) = 0x%02X\n", \
              #REG,REG,BTREAD(REG))

static void bt829_dumpregs(BT829Ptr bt) 
{
  DUMPREG(STATUS) ;
  DUMPREG(IFORM) ;
  DUMPREG(TDEC) ;
  DUMPREG(CROP) ;
  DUMPREG(VDELAY_LO) ;
  DUMPREG(VACTIVE_LO) ;
  DUMPREG(HDELAY_LO) ;
  DUMPREG(HACTIVE_LO) ;
  DUMPREG(HSCALE_HI) ;
  DUMPREG(HSCALE_LO) ;
  DUMPREG(BRIGHT) ;
  DUMPREG(CONTROL) ;
  DUMPREG(CONTRAST_LO) ;
  DUMPREG(SAT_U_LO) ;
  DUMPREG(SAT_V_LO) ;
  DUMPREG(HUE) ;
  if ((bt->id>>4) >= BT827) {
    DUMPREG(SCLOOP) ;
    DUMPREG(WC_UP) ; }
  DUMPREG(OFORM) ;
  DUMPREG(VSCALE_HI) ;
  DUMPREG(VSCALE_LO) ;
  DUMPREG(TEST) ;
  DUMPREG(VPOLE) ;
  DUMPREG(IDCODE) ;
  DUMPREG(ADELAY) ;
  DUMPREG(BDELAY) ;
  DUMPREG(ADC) ;
  if ((bt->id) >= BT827) {
    DUMPREG(VTC) ;
    DUMPREG(CC_STATUS) ;
    DUMPREG(CC_DATA) ;
    DUMPREG(WC_DN) ;
    DUMPREG(P_IO) ; } 
}

/* ------------------------------------------------------------------------ */
/* Private routines */

void bt829_crop(BT829Ptr bt) 
{
  BTWRITE(CROP,(H(bt->vdelay)<<6)+(H(bt->vtotal)<<4)+(H(bt->hdelay)<<2)+H(bt->width)) ;
}

void bt829_ctrl(BT829Ptr bt) 
{
  BTWRITE(CONTROL,((bt->mux==3)?0xC0:0x00)+
          (bt->ldec<<5)+(bt->cbsense<<4)+(H(bt->luma)<<2)+(H(bt->sat_u)<<1)+H(bt->sat_v)) ;
}

void bt829_iform(BT829Ptr bt) 
{
  BTWRITE(IFORM,(bt->mux<<5)|(bt->xtsel<<3)|bt->format) ;  
}
