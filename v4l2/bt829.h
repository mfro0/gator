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

#ifndef GENERIC_BT829_HEADER
#define GENERIC_BT829_HEADER 1

/* i2c read,write commands for bt829 chip */
#define BTREAD(card,adr)         i2c_readreg8(card,card->bt829.addr,(adr)) 
#define BTWRITE(card,adr,val)    i2c_writereg8(card,card->bt829.addr,(adr),(val))

/* Bt829 family chip ID's */
#define BT815   0x02
#define BT817   0x06
#define BT819   0x07
#define BT827   0x0C
#define BT829   0x0E

/* Bt829 registers */
#define BT829_DSTATUS          0x000  /* device status */
#define BT829_DSTATUS_PRES     (1<<7)
#define BT829_DSTATUS_HLOC     (1<<6)
#define BT829_DSTATUS_FIELD    (1<<5)
#define BT829_DSTATUS_NUML     (1<<4)
#define BT829_DSTATUS_CSEL     (1<<3)
#define BT829_DSTATUS_CCVALID  (1<<2)
#define BT829_DSTATUS_LOF      (1<<1)
#define BT829_DSTATUS_COF      (1<<0)

#define BT829_IFORM           0x01    /* Input Format */
#define BT829_TDEC            0x02    /* Temporal Decimation */
#define BT829_CROP            0x03    /* MSB Cropping */
#define BT829_VDELAY_LO       0x04    /* Vertical Delay */
#define BT829_VACTIVE_LO      0x05    /* Vertical Active */
#define BT829_HDELAY_LO       0x06    /* Horizontal Delay */
#define BT829_HACTIVE_LO      0x07    /* Horizontal Active */
#define BT829_HSCALE_HI       0x08    /* Horizontal Scaling */
#define BT829_HSCALE_LO       0x09    /* Horizontal Scaling */
#define BT829_BRIGHT          0x0A    /* Brightness Control */
#define BT829_CONTROL         0x0B    /* Miscellaneous Control */
#define BT829_CONTRAST_LO     0x0C    /* Luma Gain (Contrast) */
#define BT829_SAT_U_LO        0x0D    /* Chroma (U) Gain (Saturation) */
#define BT829_SAT_V_LO        0x0E    /* Chroma (V) Gain (Saturation) */
#define BT829_HUE             0x0F    /* Hue Control */
#define BT829_SCLOOP          0x10    /* SC Loop Control */
#define BT829_HFILT	(1<<4) /* must be set when decoding secam */
#define BT829_CKILL	(1<<5)
#define BT829_CAGC	(1<<6)
#define BT829_PEAK	(1<<7)

#define BT829_WC_UP           0x11    /* White Crush Up Count */
#define BT829_OFORM           0x12    /* Output Format */
#define BT829_VSCALE_HI       0x13    /* Vertical Scaling */
#define BT829_VSCALE_LO       0x14    /* Vertical Scaling */
#define BT829_TEST            0x15    /* Test Control */
#define BT829_VPOLE           0x16    /* Video Timing Polarity */
#define BT829_IDCODE          0x17    /* ID Code */
#define BT829_ADELAY          0x18    /* AGC Delay */
#define BT829_BDELAY          0x19    /* Burst Gate Delay */
#define BT829_ADC             0x1A    /* ADC Interface */
#define BT829_VTC             0x1B    /* Video Timing Control */
#define BT829_VBIFMT	(1<<3) /* Byte order for VBI data */
#define BT829_VBIEN	(1<<4) /* Enable vbi data to be captured */

#define BT829_CC_STATUS       0x1C    /* Extended Data Services/Closed Capt Status */
#define BT829_CC_DATA         0x1D    /* Extended Data Services/Closed Capt Data */
#define BT829_WC_DN           0x1E    /* White Crush Down Count */
#define BT829_SRESET          0x1F    /* Software Reset */
#define BT829_P_IO            0x3F    /* Programmable I/O */

/* this should be the i2c address on the card? */
#define BT829_I2C              0x110

void set_brightness(GENERIC_CARD *card, int value);
void set_hue(GENERIC_CARD *card, int value);
void set_contrast(GENERIC_CARD *card, int value);
void set_saturation(GENERIC_CARD *card, int value);
int bt829_setmux(GENERIC_CARD *card);
void bt829_iform(GENERIC_CARD *card);
int bt829_ctrl(GENERIC_CARD *card);
int bt829_register(GENERIC_CARD *card,u8 addr, int use, int vpole);
void bt829_dumpregs(GENERIC_CARD *card);

#endif
