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

/* any functions that deal with the BT chip should go here */

#define __NO_VERSION__

#include "generic.h"
#include "i2c.h"
#include "bt829.h"

void set_brightness(GENERIC_CARD *card, int value)
{
  /* We want -128 to 127 we get 0-65535 */
  card->bright = value;
  value = (value >> 8) - 128;
  BTWRITE(card,BT829_BRIGHT,value);
}

void set_hue(GENERIC_CARD *card, int value)
{
  card->hue = value;
  value = (value >> 8) - 128;
  BTWRITE(card,BT829_HUE,value);
}

void set_contrast(GENERIC_CARD *card, int value)
{
  int hibit;
  /* 0 - 511 */
  card->contrast = value;
  value = (value >> 7);
  hibit = (value >> 6) & 4;
  card->luma = value;
  bt829_ctrl(card);
  BTWRITE(card,BT829_CONTRAST_LO, LO(value));
}

void set_saturation(GENERIC_CARD *card, int value)
{
  card->saturation = value;
  /* 0 - 511  for the color */
  card->sat_u = value >> 7;
  card->sat_v = ((value >> 7)*180L)/254;
  bt829_ctrl(card);
  BTWRITE(card,BT829_SAT_U_LO,LO(card->sat_u));
  BTWRITE(card,BT829_SAT_V_LO,LO(card->sat_v)) ;
}

int bt829_setmux(GENERIC_CARD *card)
{
  /* 1=Composite,  2=tuner, 3=S-Video */
  if(card->mux==3)
    //BTWRITE(card, BT829_ADC,0xA1);
    BTWRITE(card, BT829_ADC,0x80);
  else
//    BTWRITE(card, BT829_ADC,0xA3);
    BTWRITE(card, BT829_ADC,0x82);

  bt829_ctrl(card);
  bt829_iform(card);
  return 0;
}

void bt829_iform(GENERIC_CARD *card)
{
/*  int flag;

  flag = BTREAD(card,BT829_IFORM);
  if (tunertype == -1) {
    flag &= 0xf8; // set FORMAT to AUTOFORMAT (000) 
    BTWRITE(card,BT829_IFORM,card->mux<<5|GENERIC_IFORM_XT0|GENERIC_IFORM_XT1);
  } else { */
    BTWRITE(card,BT829_IFORM,card->mux<<5|generic_tvnorms[card->tvnorm].iform);
//  }
}

int bt829_ctrl(GENERIC_CARD *card)
{
  int ldec = 0;
  if (card->width > 360){
    ldec = 1;
  }
  BTWRITE(card, BT829_CONTROL,((card->mux==3)?0xC0:0x00)+
            (ldec<<5)+(card->cbsense<<4)+(HI(card->luma)<<2)+
            (HI(card->sat_u)<<1)+HI(card->sat_v));
  return 0;
}

int bt829_register(GENERIC_CARD *card,u8 addr, int use, int vpole)
{
  char *ident, name[64];

  card->bt829.addr = addr;
  BTWRITE(card,BT829_SRESET,0x00);
  BTWRITE(card,BT829_VPOLE,vpole<<7);

  /* Chip device id, chip revision and chip ident string */
  card->bt829.deviceid = BTREAD(card,BT829_IDCODE);
  card->bt829.revision = card->bt829.deviceid & 0x0F;
  card->bt829.deviceid = (card->bt829.deviceid & 0xF0) >> 4;
  switch (card->bt829.deviceid) {
    case BT815:
      ident = "Bt815A";
      break;
    case BT817:
      ident = "Bt817A";
      break;
    case BT819:
      ident = "Bt819A";
      break;
    case BT827:
      ident = "Bt827A/B";
      break ;
    case BT829:
      ident = "Bt829A/B";
      break;
    default:
      card->bt829.addr = 0xFF;
      return(ENODEV);
  }
  snprintf(name,sizeof(name),
    "%s Video Decoder, Revision %d%s",ident,card->bt829.revision,
    (use)?"":" DISABLED") ;
  printk(KERN_INFO "bt829 chip is %s\n",name);
  return 0;
}
