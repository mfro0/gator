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

//  printk ("R128_CONFIG_APER_0_BASE 0x%08x\n", R128_CONFIG_APER_0_BASE);

  card->videoram = R128_CONFIG_MEMSIZE >> 10;
  printk(KERN_INFO "videoram is %d\n",card->videoram);

  /* not sure what this does */
  R128_VIDEOMUX_CNTL=0x50781;

  /* set card type to all in wonder for now, I should change this to
  make it for different capture abilities */
  card->type=CARD_RAGE128_AIW;
  
  return 0;
}

/*
void rage128_enable_capture(GENERIC_CARD *card)
{
  int width,height,bpp,bufsize,temp,vfilt,vtotal;
  unsigned int flags;

  MACH64_CRTC_INT_CNTL &= ~(MACH64_CAPBUF0_INT_EN|MACH64_CAPBUF1_INT_EN|MACH64_CAPONESHOT_INT_EN|MACH64_BUSMASTER_INT_EN);
}
*/

void rage128_disable_capture(GENERIC_CARD *card)
{
  u32 bus_cntl;

//  R128_CAP0_CONFIG = 0x00000000L;
  //make sure DMA transfers have stopped 
//  bus_cntl = R128_BUS_CNTL;
//  R128_BUS_CNTL = bus_cntl | R128_BUS_MSTR_RESET;
  card->status = 0;
} 
