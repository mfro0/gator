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

#ifndef GENERIC_RAGE128_HEADER
#define GENERIC_RAGE128_HEADER 1

#define R128_CLOCK_FREQ   80000 /* 7.5kHz .. 100kHz */

#define R128_I2C_CNTL_0                 (*((u32*)(card->MMR+0x0090)))
#define R128_I2C_CNTL_0_0               (*(( u8*)(card->MMR+0x0090)))
#define R128_I2C_CNTL_0_1               (*(( u8*)(card->MMR+0x0091)))
#define R128_I2C_CNTL_1                 (*((u32*)(card->MMR+0x0094)))
#define R128_I2C_DATA                   (*(( u8*)(card->MMR+0x0098)))
#define R128_CONFIG_MEMSIZE             (*((u32*)(card->MMR+0x00F8)))
#define R128_CONFIG_APER_0_BASE         (*((u32*)(card->MMR+0x0100)))

#define R128_VIDEOMUX_CNTL              (*((u32*)(card->MMR+0x0190)))
#define R128_DEVICE_ID                  (*((u16*)(card->MMR+0x0F02)))
#define R128_REVISION_ID                (*(( u8*)(card->MMR+0x0F08)))

/* this matches up to CARD_ALL_IN_WONDER in mach64.h */
#define CARD_RAGE128_AIW 2

/* R128_I2C_CNTL_0 bits */
#define I2C_DONE        0x00000001
#define I2C_NACK        0x00000002
#define I2C_HALT        0x00000004
#define I2C_SOFT_RST    0x00000020
#define I2C_DRIVE_EN    0x00000040
#define I2C_DRIVE_SEL   0x00000080
#define I2C_START       0x00000100
#define I2C_STOP        0x00000200
#define I2C_RECEIVE     0x00000400
#define I2C_ABORT       0x00000800
#define I2C_GO          0x00001000
#define I2C_CLEAR       (I2C_DONE|I2C_NACK|I2C_HALT|I2C_SOFT_RST)

/* R128_I2C_CNTL_1 bits */
#define I2C_SEL         0x00010000
#define I2C_EN          0x00020000

#define R128_CAP0_TRIG_CNTL			(*((u32*)(card->MMR+0x0950)))
#define R128_CAP0_TRIG_CNTL_TRIGGER_GET         0x00000003        
#define R128_CAP0_TRIG_CNTL_TRIGGER_SET         0x00000001        
#define R128_CAP0_TRIG_CNTL_CAPTURE_EN          0x00000010        
#define R128_CAP0_TRIG_CNTL_VSYNC_GET           0x0000FF00        
#define R128_CAP0_TRIG_CNTL_VSYNC_SET           0x00010000        


#define R128_BUS_MSTR_RESET			0x00000002ul

int r128_inita(GENERIC_CARD *card);
void rage128_disable_capture(GENERIC_CARD *card);

#endif
