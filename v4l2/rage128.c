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


/* anything dealing with the rage128 board goes here */

#define __NO_VERSION__

#include "generic.h"
#include "rage128.h"
#include "i2c.h"
#include "bt829.h"
#include "board.h"

int r128_inita(GENERIC_CARD *card)
{
  u16 id;
  u8 rev;

  id=R128_DEVICE_ID;
  rev=R128_REVISION_ID;
  rev = (rev & 0xF0) >> 4;

  /* Chip register aperture enabled (chip in use by X server) ? */
  if (id == 0xFFFF){
     printk (KERN_INFO "Error initializing r128 card\n");
     return -ENODEV;
  }
  printk (KERN_INFO "device id 0x%08x revision %d\n",id, rev);

  /* figure out what i2c mode to use */
  if (i2c_init(card) != 0){
    printk (KERN_ERR "Could not find i2c bus driver\n");
    return -ENODEV;
  }

//  printk ("R128_CONFIG_APER_0_BASE 0x%08x\n", R128_CONFIG_APER_0_BASE);

  card->videoram = R128_CONFIG_MEMSIZE >> 10;
  printk(KERN_INFO "videoram is %d\n",card->videoram);

  /* not sure what this does */
  R128_VIDEOMUX_CNTL=0x50783;

  R128_CAPTURE_PORT_MODE_CNTL = 0;

   /* find board address (make this into a function) */
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
 

/* should pull this out from mach64 and put it in board.c then call that */
  /* could do tv out later */
  board_setbyte(card) ; /* Enable Capture Board */

  /* find the tuner */
  if (i2c_device(card,0xC0))
    fi12xx_register(card,0xC0,0);
  if (i2c_device(card,0xC6))
    fi12xx_register(card,0xC6,1);

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
  if (i2c_device(card,0x88)){
    bt829_register(card,0x88,1,1);
  }
  if (i2c_device(card,0x8A)){
    bt829_register(card,0x8A,1,1);
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

  /* make sure capture is off to start with */
  R128_CAP0_TRIG_CNTL = 0x00000000; 

  return 0;
}


void rage128_enable_capture(GENERIC_CARD *card)
{
  int temp,vtotal;

  /* set location of capture buffers */

/* if interlaced then pitch is cardwidth * 4 to skip every other line 
 cap buf0 even offset is the offset of even frames 
 so the offset should be 0 if not interlaced? */
R128_CAP0_BUF0_OFFSET = card->buffer0;
R128_CAP0_BUF1_OFFSET = card->buffer1;

if (disableinterlace || 
	card->height <= generic_tvnorms[card->tvnorm].sheight / 2){
  R128_CAP0_BUF_PITCH = 2*card->width; 
  R128_CAP0_BUF0_EVEN_OFFSET = card->buffer1;
  R128_CAP0_BUF1_EVEN_OFFSET = card->buffer0;
} else {
  R128_CAP0_BUF_PITCH = 2*2*card->width; 
  R128_CAP0_BUF0_EVEN_OFFSET = card->buffer0 + 2*card->width; 
  R128_CAP0_BUF1_EVEN_OFFSET = card->buffer1 + 2*card->width;
}

  //R128_CAP0_ONESHOT_BUF_OFFSET = card->buffer0;
  R128_CAP0_ONESHOT_BUF_OFFSET = 0;
  R128_CAP0_H_WINDOW = XY(2*card->width,0);
 // should change the 21 based on capture size and pal/ntsc/secam
  R128_CAP0_V_WINDOW = XY(21-1+(card->height),21);

R128_CAP0_VBI_V_WINDOW = 0;
R128_CAP0_VBI_H_WINDOW = 0;
#warning make vbibuffer 2*32768 size in generic.c
R128_CAP0_VBI_EVEN_OFFSET = card->vbibuffer;
R128_CAP0_VBI_ODD_OFFSET = card->vbibuffer + 32768;

R128_CAP0_DEBUG = 0;

/* enable streaming (turn on interrupts) */
  R128_CLOCK_CNTL_INDEX0 = (FCP_CNTL & 0x1F) | R128_PLL_WR_EN;
  R128_CLOCK_CNTL_DATA = 0x001; // was 101
  R128_CLOCK_CNTL_INDEX0 = (FCP_CNTL & 0x1F) & ~R128_PLL_WR_EN;

BTWRITE(card,BT829_VACTIVE_LO,LO(generic_tvnorms[card->tvnorm].sheight));
/* IF HDELAY IS NOT EVEN THEN CBSENSE MUST BE SET TO 1 
 *    if you dont do this then cb,cr order is wrong 
 *       (you get yvyu instead of yuyv) */
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
BTWRITE(card,BT829_VSCALE_HI,HI(0x10000 -
        ((512*vtotal/(card->height*2)-512) & 0x1fff))|0x40);
        //((512*vtotal/(card->height*2)-512) & 0x1fff))|0xe0);
BTWRITE(card,BT829_VSCALE_LO,LO(0x10000 -
        ((512*vtotal/(card->height*2)-512) & 0x1fff)));

  R128_CAP0_CONFIG = (R128_CAP0_CONFIG_CONTINUOS
| R128_CAP0_CONFIG_START_FIELD_EVEN
| R128_CAP0_CONFIG_START_BUF_GET
| R128_CAP0_CONFIG_BUF_TYPE_ALT // alternates between buf0 and buf1
//| R128_CAP0_CONFIG_BUF_TYPE_FRAME
| R128_CAP0_CONFIG_BUF_MODE_DOUBLE
| R128_CAP0_CONFIG_VBI_EN //vertical blank interrupt
| R128_CAP0_CONFIG_VIDEO_IN_VYUY422);
} else {
// 0x60 is interlace 0x40 is not
BTWRITE(card,BT829_VSCALE_HI,HI(0x10000 -
        ((512*vtotal/card->height-512) & 0x1fff))|0x60);
BTWRITE(card,BT829_VSCALE_LO,LO(0x10000 -
        ((512*vtotal/card->height-512) & 0x1fff)));

  R128_CAP0_CONFIG = (R128_CAP0_CONFIG_CONTINUOS
| R128_CAP0_CONFIG_START_FIELD_EVEN
| R128_CAP0_CONFIG_START_BUF_GET
| R128_CAP0_CONFIG_BUF_TYPE_ALT
| R128_CAP0_CONFIG_BUF_TYPE_FRAME
| R128_CAP0_CONFIG_BUF_MODE_DOUBLE
| R128_CAP0_CONFIG_VBI_EN //vertical blank interrupt
| R128_CAP0_CONFIG_VIDEO_IN_VYUY422);
}

  R128_CAP1_CONFIG = 0;

  R128_BUS_CNTL &= ~R128_BUS_MASTER_DIS;

  R128_BM_CHUNK_0_VAL |= (R128_BM_PTR_FORCE_TO_PCI |
                  R128_BM_PM4_RD_FORCE_TO_PCI |
                  R128_BM_GLOBAL_FORCE_TO_PCI); // |0xFF

  R128_BM_CHUNK_1_VAL = 0xF0F0F0F;

  R128_CAP_INT_STATUS = 3;
  R128_GEN_INT_STATUS = (1<<8)|(1<<16);
  R128_CAP_INT_CNTL = 3;
  R128_GEN_INT_CNTL |= (1<<16);

  R128_CAP0_TRIG_CNTL = R128_CAP0_TRIG_CNTL_CAPTURE_EN|R128_CAP0_TRIG_CNTL_TRIGGER_SET;

  card->status |= STATUS_CAPTURING;
}

void rage128_disable_capture(GENERIC_CARD *card)
{
  R128_CAP0_CONFIG = 0x00000000L;
  R128_CAP_INT_CNTL &= ~3;
  R128_GEN_INT_CNTL &= ~((1<<16)|(1<<24));
/* disable streaming (turn off interrupts) */
  R128_CLOCK_CNTL_INDEX0 = (FCP_CNTL & 0x1F) | R128_PLL_WR_EN;
  R128_CLOCK_CNTL_DATA = 0x404;
  R128_CLOCK_CNTL_INDEX0 = (FCP_CNTL & 0x1F) & ~R128_PLL_WR_EN;

printk (KERN_INFO "dis R128_CAP0_TRIG_CNTL is 0x%08x\n", R128_CAP0_TRIG_CNTL);
  R128_CAP0_TRIG_CNTL = 0x00000000; 
printk (KERN_INFO "dis R128_CAP0_TRIG_CNTL is now 0x%08x\n", R128_CAP0_TRIG_CNTL);
  //make sure DMA transfers have stopped 
  R128_BUS_CNTL |= R128_BUS_MASTER_RESET;
} 
