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


#ifndef GENERIC_BOARD_HEADER
#define GENERIC_BOARD_HEADER 1

#define TDA8425 8425
#define TDA9850 9850
#define TDA9851 9851
#define MSP3410 3410

#define CARD_STAND_ALONE        1
#define CARD_ALL_IN_WONDER      2
#define CARD_ALL_IN_WONDER_PRO  3
#define CARD_ALL_IN_WONDER_128  4
#define CARD_NEC                5

#define CAPTURE_MODE_SINGLE_ODD 0
#define CAPTURE_MODE_SINGLE_EVEN 1
#define CAPTURE_MODE_DOUBLE 2
#define CAPTURE_MODE_INTERLACED 3
#define CAPTURE_MODE_INTERLACED_INV 4

/* registers in the TDA9850 BTSC/dbx chip */
#define CON1ADDR                0x04
#define CON2ADDR                0x05
#define CON3ADDR                0x06 
#define CON4ADDR                0x07
#define ALI1ADDR                0x08 
#define ALI2ADDR                0x09
#define ALI3ADDR                0x0a

void set_mute(GENERIC_CARD *card, int value);
int board_setaudio(GENERIC_CARD *card);
int fi12xx_tune(GENERIC_CARD *card);
u8 fi12xx_band(GENERIC_CARD *card, unsigned long freq);
int fi12xx_register(GENERIC_CARD *card,u8 addr, int use);
int board_setbyte(GENERIC_CARD *card);

#endif
