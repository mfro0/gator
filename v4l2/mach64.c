/* 
    This file is part of genericv4l.

    genericv4l is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    genericv4l is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


/* anything dealing with the mach64 board goes here */

#define __NO_VERSION__

#include "generic.h"
#include "i2c.h"
#include "bt829.h"
#include "mach64.h"
#include "memory.h"
#include "tda9850.h"

static const int videoRamSizes[] =
    {0, 256, 512, 1024, 2*1024, 4*1024, 6*1024, 8*1024, 12*1024, 16*1024, 0};

struct tuner_types generic_tuners[] = {
  {  0,1, NULL,           NULL,                   NULL },
  {  1,1, "Philips",      "FI1236",               "NTSC M/N" },
  {  2,6, "Philips",      "FI1236J",              "NTSC Japan" },
  {  3,0, "Philips",      "FI1216MK2",            "PAL B/G" },
  {  4,0, "Philips",      "FI1246",               "PAL I" },
  {  5,0, "Philips",      "FI1216MF",             "PAL B/G, SECAM L/L'" },
  {  6,1, "Philips",      "FI1236MK2",            "NTSC M/N" },
  {  7,2, "Philips",      "FI1256",               "SECAM D/K" },
  {  8,1, "Samsung",      "TCPN7082PC27A",        "NTSC M/N" },
  {  9,0, "Philips",      "FI1216MK2(EXT)",       "PAL B/G" },
  { 10,0, "Philips",      "FI1246MK2(EXT)",       "PAL I" },
  { 11,0, "Philips",      "FI1216MF(EXT)",        "PAL B/G, SECAM L/L'" },
  { 12,1, "Philips",      "FI1236MK2(EXT)",       "NTSC M/N" },
  { 13,0, "Temic",        "FN5AL",                "PAL I/B/G/DK, SECAM DK" },
  { 14,1, NULL,           NULL,                   NULL },
  { 15,1, NULL,           NULL,                   NULL },
  { 16,1, "Alps",         "TSBH5",                "NTSC M/N" },
  { 17,1, "Alps",         "TSC??",                "NTSC M/N" },
  { 18,1, "Alps",         "TSCH5",                "NTSC M/N with FM" }
};
const unsigned int MAXTUNERTYPE = ARRAY_SIZE(generic_tuners);


/* Board info/control byte:
 * 0x80: bt829: 0:enable 1:disable
 * 0x40: sound: 0:tuner 1:video
 * 0x20: 0:stereo 1:mono (R/O?)
 * 0x10: 0:mute 1:unmute
 * 0x0F: Tuner type (R/O)
 */

int board_setbyte(GENERIC_CARD *card)
{
  u8 info;
  u32 tmp;

  tmp = MACH64_EXT_DAC_REGS & 0xFFFFFFFC;
  if (card->mute) tmp |= 0x2; /* something to do with audio */
  if (card->mux == 2) tmp |= 0x1; /* something to do with tuner */
  MACH64_EXT_DAC_REGS = tmp;

  /* All-In-Wonder */
  info = 0x2F;               /* TV-Tuner */

/* could check if we are capturing here as well and mute if not */
  if (card->mux != 2){
      info |= 0x40;  /* sound 0:tuner 1:comp/svideo */
  }
  if (!(card->mute)) {
      info |= 0x10;  /* 0:mute 1:unmute */
  }
  i2c_write(card,card->board.addr,&info,1);

  return 0;
}

int m64_inita(GENERIC_CARD *card)
{
  u32 id;

  /* figure out what i2c mode to use */
  if (i2c_init(card) != 0){
    printk (KERN_ERR "Could not find i2c bus driver\n");
    return -ENODEV;
  }

  id = MACH64_CONFIG_CHIP_ID;
  card->board.deviceid = id & 0x0000FFFF ;            /* Device ID */
  card->board.revision = (id & 0x07000000) >> 24 ;    /* Chip Version */

  printk (KERN_INFO "genericv4l(%d): ati deviceid 0x%04X revision %d\n",card->cardnum,card->board.deviceid, card->board.revision);

  /* for 264 CT ET VT GT we probe for memory differently
   * I only know the id for 264VT though :) */
  if (card->board.deviceid == 0x5654) {
    card->videoram = (MACH64_MEM_CNTL & 7) + 2;
    card->videoram = videoRamSizes[card->videoram];
  } else {
    card->videoram = MACH64_MEM_CNTL & 15;
    if (card->videoram < 8)
      card->videoram = 512*(card->videoram+1);
    else if (card->videoram < 12)
      card->videoram = 1024*(card->videoram-3);
    else
      card->videoram = 2048*(card->videoram-7);
  }

  printk(KERN_INFO "videoram is %d\n",card->videoram);

  /* 8192 space for registers in framebuffer happens on pro version */
  card->vbibuffer = 1024*card->videoram - 32768 - 8192;


  /* find board address */
  card->board.addr = 0x70; //standalone card
  card->boardinfo = 0xFF;
  i2c_write(card,0x70,&card->boardinfo,1);
  if ((card->boardinfo = i2c_read(card,0x70)) != 0xFF){
    card->board.addr = 0x70; //standalone card
    card->type=CARD_STAND_ALONE;
  } else {
    card->boardinfo = 0xFF;
    i2c_write(card,0x40,&card->boardinfo,1);
    if ((card->boardinfo = i2c_read(card,0x40)) != 0xFF){
      card->board.addr = 0x40; //standalone card
      card->type=CARD_STAND_ALONE;
    } else {
      card->boardinfo = 0xFF;
      i2c_write(card,0x78,&card->boardinfo,1);
      if ((card->boardinfo = i2c_read(card,0x78)) != 0xFF){
        card->board.addr = 0x78; //nec card?
        card->type=CARD_NEC;
      } else {
        card->boardinfo = 0x7F;
        i2c_write(card,0x76,&card->boardinfo,1);
        if ((card->boardinfo = i2c_read(card,0x76)) != 0xFF){
          card->board.addr = 0x76; /*all in wonder */
	} else {
          card->board.addr = 0xFF; /*all in wonder pro */
          //card->board.addr = 0x76; /*all in wonder pro */
	}
	switch (card->board.deviceid) {
	  case 0x5245: case 0x5246: case 0x524B: case 0x524C:
	    card->type=CARD_ALL_IN_WONDER_128;
	    break;
	  case 0x4742: case 0x4744: case 0x4749: case 0x4750: case 0x4751:
	    card->type=CARD_ALL_IN_WONDER_PRO;
	    break;
	  default:
	    card->type=CARD_ALL_IN_WONDER;
	}
      }
    }
  } 

  /* could do tv out later */
  board_setbyte(card) ; /* Enable Capture Board */

  /* find the tuner */
  switch (card->type) {
    case CARD_STAND_ALONE:
    case CARD_NEC:
      if (i2c_device(card,0xC6)) 
	fi12xx_register(card,0xC6,0);
      if (i2c_device(card,0xC0)) 
	fi12xx_register(card,0xC0,1);
      break;
    default:
      if (i2c_device(card,0xC0)) 
	fi12xx_register(card,0xC0,0);
      if (i2c_device(card,0xC6)) 
	fi12xx_register(card,0xC6,1);
      break ; 
  }

  /* find the audio chip */
  if (i2c_device(card,0x82)){
    printk(KERN_INFO "TDA8425 Stereo Audio Processor\n");
    card->audio.deviceid = TDA8425;
    card->audio.addr = 0x82;
  } else if(i2c_device(card,0xB4)){
    printk(KERN_INFO "TDA9850 BTSC Stereo+SAP Audio Processor\n");
    card->audio.deviceid = TDA9850;
    card->audio.addr = 0xB4;
  } else if(i2c_device(card,0xB6)){
    printk(KERN_INFO "TDA9851 BTSC Economic Stereo Audio Processor\n");
    card->audio.deviceid = TDA9851;
    card->audio.addr = 0xB6;
  } else if(i2c_device(card,0x80)){
    card->audio.deviceid = MSP3410;
    printk(KERN_INFO "MSP3410 Multi-Sound Audio Processor\n");
    card->audio.addr = 0x80;
  }
  else {
    printk(KERN_INFO "NO SOUND PROCESSOR found\n");
  }

  /* find the bt829 device now */
  switch (card->type) {
    case CARD_STAND_ALONE:
    case CARD_NEC:
      if (i2c_device(card,0x8A)){
        bt829_register(card,0x8A,1,0);
      }
      if (i2c_device(card,0x88)){
        bt829_register(card,0x88,1,0);
      }
      break;
    case CARD_ALL_IN_WONDER:
      if (i2c_device(card,0x88)){
        bt829_register(card,0x88,1,0);
      }
      if (i2c_device(card,0x8A)){
        bt829_register(card,0x8A,1,0);
      }
      break;
    case CARD_ALL_IN_WONDER_PRO:
    case CARD_ALL_IN_WONDER_128:
      if (i2c_device(card,0x88)){
        bt829_register(card,0x88,1,1);
      }
      if (i2c_device(card,0x8A)){
        bt829_register(card,0x8A,1,1);
      }
      break;
  }
   
  /* TDEC controls frames per sec 0 is all frames (read bt829.pdf for info) */
  BTWRITE(card,BT829_TDEC,0x00); /*Temporal decimation register */
  BTWRITE(card,BT829_OFORM,0x0A); /* output format register */

  /* check if we are muted or not? maybe always mute? */
  if (i2c_read(card,card->board.addr) & 0x10){
     card->mute = 0; //lets just always mute
  } else {
     card->mute = 1;
  }

  /* check what mux we are on */
  if (BTREAD(card,BT829_ADC) == 0x80){
    card->mux = 3; //svideo
  } else {
    card->mux = 2; //tuner
  }

  return 0;
}

/* Tune to frequency */
int fi12xx_tune(GENERIC_CARD *card)
{
  u8 data[4];
  u8 band;
  u32 K,Fpc,freq;
  int i=0;

  /* if ntsc or pal M use this */
  if (card->tvnorm == 1 || card->tvnorm == 4 || card->tvnorm == 6){ 
    Fpc = 4575; /* 45.75 * 100 */
  } else {
    Fpc = 3890; /* 38.90 * 100 */
  }
  /* reset oform, for some reason this can be changed during use, causing white pixels to appear black, so if we reset it when we change channels... :) */
  BTWRITE(card,BT829_OFORM,0x0A);
  freq = card->freq * 100 / 16;

  K = (freq + Fpc) * 16 + 50;
  band = fi12xx_band(card,freq);
  card->freq = (K/16 - Fpc) / 100;
  i = (K > card->lastfreq) ? 0 : 2;
  card->lastfreq = K;
  K = K / 100 - 4; /* Since I removed the float operations, the conversion is off by 4... so lets just remove 4 :) */
  data[i+0] = (K>>8) & 0x7F;
  data[i+1] = K & 0xFF;
  data[2-i] = 0x8E;
  data[3-i] = band ;
  i2c_write(card,card->fi12xx.addr,data,sizeof(data));
  return 0;
}

u8 fi12xx_band(GENERIC_CARD *card, unsigned long freq)
{
  int system=0;

  switch (card->tuner) {
    case 1: case 2: case 12: /* NTSC M/N and NTSC Japan */
      if (freq <= 16000) 
      	return 0xA2;
      if (freq <= 45125) 
      	return 0x94;
      if (freq <= 46325) 
      	return 0x34;
      return 0x31;
    case 3: case 4: case 9: case 10:    /* PAL B/G and PAL I */
      if (freq <= 14025) 
        return 0xA2;
      if (freq <= 16825) 
        return 0xA4;
      if (freq <= 44725) 
      	return 0x94;
      return 0x31;
    case 6:               /* NTSC M/N Mk2 */
      if (freq <= 16000) 
      	return 0xA0;
      if (freq <= 45400) 
      	return 0x90;
      return 0x30;
    case 7:               /* SECAM D/K */
      if (freq <= 16825) 
      	return 0xA0;
      if (freq <= 45525) 
      	return 0x90;
      return 0x30;
    case 5: case 11:      /* PAL B/G, SECAM L/L' */
      if (card->tvnorm != 2) {
      	system = 1; /* pal */
      } else {
      	system = 3 ; /* secam */
      }
      if (freq <= 16825) 
      	return 0xA0+system;
      if (freq <= 44725) 
      	return 0x90+system;
      return 0x30+system;
    case 8:               /* NTSC M/N (Samsung) */
      if (card->type == CARD_NEC) {
        if (freq <= 16000) 
	  return 0xA0;
        if (freq <= 45400) 
	  return 0x90;
        return 0x30;
      } else if (card->type == CARD_STAND_ALONE  && card->board.revision == 0){
        if (freq <= 15725) 
	  return 0xA2;
        if (freq <= 45125) 
	  return 0x94;
        if (freq <= 46325) 
	  return 0x34;
        return 0x31;
      } else {
        if (freq <= 12725) 
	  return 0x01;
        if (freq <= 36125) 
	  return 0x02;
        return 0x08; }
    case 13:              /* PAL I/B/G/DK, SECAM D/K */
      if (freq <= 14025) 
        return 0xA2;
      if (freq <= 16825)
        return 0xA4;
      if (freq <= 44725) 
        return 0x94;
      return 0x31;
    case 16: case 17:     /* NTSC M/N */
      if (freq <= 13000) 
        return 0x14;
      if (freq <= 36400) 
        return 0x12;
      return 0x11;
    case 18:              /* NTSC M/N with FM */
      if (freq <= 13000) 
        return 0x14;
      if (freq <= 36400) 
        return 0x12;
      return 0x11;
    default: return 0x00;
  } 
}


int fi12xx_register(GENERIC_CARD *card,u8 addr, int use)
{
  char *ident, name[64];
  u8 *ptr;

/* need to check for r128mminfo here as well */
  ptr = card->m64mminfo;

  dprintk(2,"card(%d) fi12xx_register called ptr is %d\n",card->cardnum, *ptr);
  if (card->board.addr == 0xFF){
    if (*ptr&0x0F) 
      card->tuner = (*ptr<=MAXTUNERTYPE) ? *ptr : 0;
  } else {
      card->tuner = card->boardinfo & 0x0F;
      if (card->tuner == 0x0F){
        card->tuner = (*ptr<=MAXTUNERTYPE) ? *ptr : 0;
      }
  }
//  if (card->tuner)
//  {
    ident = generic_tuners[card->tuner].ident;
    if (ident==NULL) ident="Unknown";
    snprintf(name,sizeof(name),"%s Tuner Module (%s %s)%s",
        generic_tuners[card->tuner].system,
        generic_tuners[card->tuner].vendor,
             ident, (use)?"":" UNUSED") ;
    printk(KERN_INFO "Tuner is %s\n",name);
    card->tvnorm = generic_tuners[card->tuner].tunertype;

//  }
  /* set address of tuner */
  card->fi12xx.addr = addr;
  return 0;
}

void set_mute(GENERIC_CARD *card, int value)
{
  u8 info;
  info = 0x2F;  /* TV-Tuner */
  card->mute = value;
  if (value == 0) info |= 0x10;
  i2c_write(card,card->board.addr,&info,1);
  board_setaudio(card);
  board_setbyte(card);
}


int board_setaudio(GENERIC_CARD *card) 
{
  u8 tmp, data;

  if (card->audio.deviceid == TDA8425){
    i2c_writereg8(card,card->audio.addr,0x00,0xFF); //left vol
    i2c_writereg8(card,card->audio.addr,0x01,0xFF); //right vol
    i2c_writereg8(card,card->audio.addr,0x02,0xF6); //bass
    i2c_writereg8(card,card->audio.addr,0x03,0xF6); //treble
    tmp = 0xC0;
    tmp |= card->mute ? 0x20 : 0x0;
    /* stereo 3 , Linear 2 , Pseudo 1 , Forced mono 0 */
    tmp |= card->stereo ? (3 << 3) : 0x0;
    tmp |= 3 << 1; //src sel
    tmp |= (card->mux != 2) ? 0 : 1;
    i2c_writereg8(card,card->audio.addr,0x08,tmp); 
  }

  if (card->audio.deviceid == TDA9850) {
    i2c_writereg8(card,card->audio.addr,CON1ADDR,0x08); /* noise threshold stereo */ 
    i2c_writereg8(card,card->audio.addr,CON2ADDR,0x08); /* noise threshold sap */
    i2c_writereg8(card,card->audio.addr,CON3ADDR,0x40); /* stereo mode */
    i2c_writereg8(card,card->audio.addr,CON4ADDR,0x07); /* 0 dB input gain? */
    i2c_writereg8(card,card->audio.addr,ALI1ADDR,0x10); /* wideband alignment? */
    i2c_writereg8(card,card->audio.addr,ALI2ADDR,0x10); /* spectral alignment? */
    i2c_writereg8(card,card->audio.addr,ALI3ADDR,0x03); 

    tmp = 0x0;
    tmp |= card->stereo<<6;
    tmp |= card->sap<<7;
    tmp |= card->mute ? 0x8 : 0x0; //normal mute
    tmp |= card->mute ? 0x10 : 0x0; //sap mute?

    i2c_writereg8(card,card->audio.addr,CON3ADDR,tmp); 
  }

  if (card->audio.deviceid == TDA9851) {
    data = (BTREAD(card,BT829_P_IO)&0xFC) | (card->mux==2);
    BTWRITE(card,BT829_P_IO,data);
    data = (i2c_read(card,card->audio.addr) & 0x01) | 0x04 ;
    i2c_write(card,card->audio.addr,&data,1); 
  }

  return 0; 
}

void mach64_enable_capture(GENERIC_CARD *card)
{
  int temp,vfilt,vtotal;
  unsigned int flags;

  //set location of capture buffers
  MACH64_CAPTURE_BUF0_OFFSET = card->buffer0;
  MACH64_CAPTURE_BUF1_OFFSET = card->buffer1;

  /* just turn off the interrupts */
  MACH64_CRTC_INT_CNTL &= ~(MACH64_CAPBUF0_INT_EN|MACH64_CAPBUF1_INT_EN|MACH64_CAPONESHOT_INT_EN|MACH64_BUSMASTER_INT_EN);

  //make sure capture is off
  MACH64_TRIG_CNTL &= ~MACH64_CAPTURE_EN;

  /* turn off the capture flags */
  //card->status &= ~(STATUS_CAPTURING | STATUS_VBI_CAPTURE);
  card->status &= ~STATUS_CAPTURING;

  /* find current resolution and bit depth and set capture buffers
  according to that? or just go to the end of memory? 
  also should check the capture size to? or just set to max? 

  width = ((MACH64_CRTC_H_TOTAL_DISP >> 16) + 1) * 8;
  height = (MACH64_CRTC_V_TOTAL_DISP >> 16) + 1;
  bpp = ((MACH64_CRTC_GEN_CNTL & 0xf00) >> 8) * 4;

  dprintk(2,"card(%d) Current screen resolution %dx%d %dbpp\n",card->cardnum,width,height,bpp); 

  other programs use memory just after video memory so we will
  just use the end of memory, should figure out how to lock this
  so the xserver doesnt use it for buffer space and messup the display
  */

  /* set VDELAY_LO to start at line 10 the start of vbi
  VDELAY defines the number of half lines between the trailing edge of VRESET and the start of active video, so if we want to start at line 10, it would be 20
  but for whatever reason 23 works better... :) */
  /* use tvnorm to set this so we can do pal and secam */
  flags = generic_tvnorms[card->tvnorm].vdelay;
  BTWRITE(card,BT829_VDELAY_LO,flags);

  vfilt = 0;
  if (card->width <= 300){
    vfilt = 1;
  }
  if (card->width <= 150){
    vfilt = 2;
  }
  if (card->width <= 75){
    vfilt = 3;
  }

  /* enable vbi capture and set format */
  flags = BTREAD(card,BT829_VTC);
  flags &= ~3; /* clear old vfilt settings */
  flags |= BT829_VBIFMT | BT829_VBIEN | vfilt;
  BTWRITE(card,BT829_VTC,flags);

/* vbi stuff here we use oneshot to record vbi info */
MACH64_VBI_START_END = XY(20,6);
MACH64_VBI_WIDTH = 2048; 

/* set capture size, should be using card->width card->height */
  MACH64_CAPTURE_X_WIDTH    = XY(2*card->width,0);
  MACH64_CAPTURE_START_END  = XY(21-1+(card->height),21);

BTWRITE(card,BT829_VACTIVE_LO,LO(generic_tvnorms[card->tvnorm].sheight));

/* IF HDELAY IS NOT EVEN THEN CBSENSE MUST BE SET TO 1 
   if you dont do this then cb,cr order is wrong 
   (you get yvyu instead of yuyv) */
temp = card->width*generic_tvnorms[card->tvnorm].hdelayx1/generic_tvnorms[card->tvnorm].hactivex1;
if (temp & 0x1){
  card->cbsense=1;
} else {
  card->cbsense=0;
}
BTWRITE(card,BT829_HDELAY_LO,LO(temp));

BTWRITE(card,BT829_HACTIVE_LO,LO(card->width));

BTWRITE(card,BT829_HSCALE_HI,HI(4096*generic_tvnorms[card->tvnorm].hactivex1/card->width-4096));
BTWRITE(card,BT829_HSCALE_LO,LO(4096*generic_tvnorms[card->tvnorm].hactivex1/card->width-4096));

bt829_ctrl(card);

vtotal = generic_tvnorms[card->tvnorm].sheight;
if (disableinterlace){
  vtotal *= 2;
}

/* set crop register based on tuner type and size of image capture */
  switch (card->tuner) {
    case 3: case 4: case 9: case 10:    /* PAL B/G and PAL I */
    case 7:               /* SECAM D/K */
    case 5: case 11:      /* PAL B/G, SECAM L/L' */
    case 13:              /* PAL I/B/G/DK, SECAM D/K */
      if (card->width <= generic_tvnorms[card->tvnorm].swidth / 2){
        BTWRITE(card,BT829_CROP,0x21);
      } else {
        BTWRITE(card,BT829_CROP,0x23);
      }
      break;
    /* NTSC M/N and NTSC Japan */
    /* NTSC M/N Mk2 */
    /* NTSC M/N (Samsung) */
    /* NTSC M/N */
    /* NTSC M/N with FM */
    default: 
      if (card->width <= generic_tvnorms[card->tvnorm].swidth / 2){
        BTWRITE(card,BT829_CROP,0x11);
      } else {
        BTWRITE(card,BT829_CROP,0x12);
      }
      break;
  } 

//if less than half height then do not interlace
if (disableinterlace || card->height <= generic_tvnorms[card->tvnorm].sheight / 2){
// 0x60 is interlace 0x40 is not

BTWRITE(card,BT829_VSCALE_HI,HI(0x10000 -
        ((512*vtotal/(card->height*2)-512) & 0x1fff))|0x40);
        //((512*vtotal/(card->height*2)-512) & 0x1fff))|0xe0);
BTWRITE(card,BT829_VSCALE_LO,LO(0x10000 -
        ((512*vtotal/(card->height*2)-512) & 0x1fff)));

  /* capture every other field? (30 fps) */
  //MACH64_CAPTURE_CONFIG = MACH64_CAP_INPUT_MODE | MACH64_CAP_BUF_MODE | MACH64_ONESHOT_MODE | MACH64_CAP_FIELD_FLIP;

  /* this lets us capture 60 fields per second */
  //MACH64_CAPTURE_CONFIG = MACH64_CAP_INPUT_MODE | MACH64_CAP_BUF_MODE | MACH64_ONESHOT_MODE | MACH64_CAP_BUF_ALTERNATING;

  /* only capture even fields? */
  MACH64_CAPTURE_CONFIG = MACH64_CAP_INPUT_MODE | MACH64_CAP_BUF_MODE | MACH64_ONESHOT_MODE | MACH64_CAP_START_FIELD;

  //MACH64_CAPTURE_CONFIG = MACH64_CAP_INPUT_MODE | MACH64_CAP_BUF_MODE | MACH64_ONESHOT_MODE;
} else {
// 0x60 is interlace 0x40 is not
BTWRITE(card,BT829_VSCALE_HI,HI(0x10000 -
        ((512*vtotal/card->height-512) & 0x1fff))|0x60);
BTWRITE(card,BT829_VSCALE_LO,LO(0x10000 -
        ((512*vtotal/card->height-512) & 0x1fff)));
  //set it up for interlaced mode The only difference is we tell it we are capturing a full frame instead of a field
  MACH64_CAPTURE_CONFIG = MACH64_CAP_INPUT_MODE | MACH64_CAP_BUF_MODE | MACH64_ONESHOT_MODE | MACH64_CAP_BUF_FRAME | MACH64_CAP_FIELD_FLIP;
}

  MACH64_TRIG_CNTL |= MACH64_CAPTURE_EN;

  flags = MACH64_CRTC_INT_CNTL;

 //acknowledge buffer and busmaster interrupts (basicly clear them)
  flags |= MACH64_CAPBUF0_INT_ACK | MACH64_CAPBUF1_INT_ACK | MACH64_CAPONESHOT_INT_ACK | MACH64_BUSMASTER_INT_ACK;

 //Enable buffer and busmaster interrupts
  flags |= MACH64_CAPBUF0_INT_EN|MACH64_CAPBUF1_INT_EN|MACH64_CAPONESHOT_INT_EN|MACH64_BUSMASTER_INT_EN;

  MACH64_CRTC_INT_CNTL = flags;

  card->status |= STATUS_CAPTURING;
}

void mach64_disable_capture(GENERIC_CARD *card)
{
  u32 bus_cntl;

  /* make sure DMA transfers have stopped */
  bus_cntl = MACH64_BUS_CNTL;
  MACH64_BUS_CNTL = bus_cntl | MACH64_BUS_MSTR_RESET;

  MACH64_CAPTURE_CONFIG = 0x00000000L;

  /* disable interrupts */
  //MACH64_CRTC_INT_CNTL &= ~(MACH64_CAPBUF0_INT_EN|MACH64_CAPBUF1_INT_EN|MACH64_CAPONESHOT_INT_EN|MACH64_BUSMASTER_INT_EN);
}
