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

#include "generic.h"
#include "board.h"
#include "bt829.h"
#include "mach64.h"
#include "rage128.h"
#include "i2c.h"

/* Board info/control byte:
 * 0x80: bt829: 0:enable 1:disable
 * 0x40: sound: 0:tuner 1:video
 * 0x20: 0:stereo 1:mono (R/O?)
 * 0x10: 0:mute 1:unmute
 * 0x0F: Tuner type (R/O)
 */

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
  K = K / 100; /* - 4; Since I removed the float operations, the conversion is off by 4... so lets just remove 4 :) */
  data[i+0] = (K>>8) & 0x7F;
  data[i+1] = K & 0xFF;
  data[2-i] = 0x8E;
  //data[2-i] = 0xCE; // MSB -> 1 CP T2 T1 T0 RSA RSB OS <- LSB (see fi1236.pdf)
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

  if (card->driver_data & RAGE128CHIP) {
    ptr = card->r128mminfo;
  } else {
    ptr = card->m64mminfo;
  }

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

int board_setbyte(GENERIC_CARD *card)
{
  u8 info;

  if (card->driver_data & RAGE128CHIP){
    if (card->mux == 2) {
      BTWRITE(card,0x3f,1); //set to tv for audio
    } else {
      BTWRITE(card,0x3f,0); //set to input for audio
    }
  } else if (card->driver_data & MACH64CHIP){
    u32 tmp;
    tmp = MACH64_EXT_DAC_REGS & 0xFFFFFFFC;
    if (card->mute) tmp |= 0x2; /* something to do with audio */
    if (card->mux == 2) tmp |= 0x1; /* something to do with tuner */
    MACH64_EXT_DAC_REGS = tmp;
  }

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

void set_mute(GENERIC_CARD *card, int value)
{
  u8 info;
  info = 0x2F;  /* TV-Tuner */
  card->mute = value;
  if (value == 0) {
    info |= 0x10;
  }
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
