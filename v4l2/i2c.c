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

/* all the different i2c functions go here */

#define __NO_VERSION__

#include <linux/delay.h>

#include "generic.h"
#include "i2c.h"
#include "mach64.h"
#include "rage128.h"

struct i2c_funcs generic_i2c_driver[] = {
/* A (DAC+GEN_TEST) B (GP_IO Register) LG (GP_IO Register) */
  { &reg_scldir, &reg_setscl, &reg_getscl,
    &reg_sdadir, &reg_setsda, &reg_getsda },
  /* TB (ImpacTV) */
  { &itv_scldir, &itv_setscl, &itv_getscl,
    &itv_sdadir, &itv_setsda, &itv_getsda },
  { &pro_scldir, &pro_setscl, &pro_getscl,      /* C (Rage PRO) */
    &pro_sdadir, &pro_setsda, &pro_getsda },
  { NULL, NULL, NULL, NULL, NULL, NULL} /* Rage128 H/W Driver */
};
const unsigned int MAXI2CDRIVER = ARRAY_SIZE(generic_i2c_driver);

int i2c_init(GENERIC_CARD *card)
{
  int ok;
  u32 save1,save2,save3,save4;
  unsigned long nm;

  if (card->driver_data & RAGE128CHIP){
    /* set it to the rage128 i2c driver (all nulls) */
    card->i2c = &generic_i2c_driver[3];

    nm = card->refclock * 10000 / (4*R128_CLOCK_FREQ);
     for (card->R128_N=1 ; card->R128_N<255 ; card->R128_N++)
       if (card->R128_N*(card->R128_N-1) > nm)
         break;
     card->R128_M = card->R128_N - 1; 
     card->R128_TIME = 2*card->R128_N;
     R128_I2C_CNTL_1   = (card->R128_TIME<<24) | I2C_SEL | I2C_EN;
     R128_I2C_CNTL_0_0 = I2C_DONE | I2C_NACK | I2C_HALT | I2C_SOFT_RST |
                         I2C_DRIVE_EN | I2C_DRIVE_SEL; 

    ok = i2c_device(card,0xC0) + i2c_device(card,0xC2) +
      i2c_device(card,0xC4) + i2c_device(card,0xC6);

    if (ok != 0 && ok != 4){
printk(KERN_INFO "card(%d) Rage128 i2c driver\n",card->cardnum);
      return 0;
    }  
    /* check for bt829 chip */
    ok = i2c_device(card,0x88) + i2c_device(card,0x8a);

    if ((ok != 0) && (ok != 4)) {
	return 0;
    }
    /* failed to find it */
    R128_I2C_CNTL_1 = 0x0;
  } else if (card->driver_data & MACH64CHIP){
    // try (DAC+GEN_TEST) 
    card->i2c = &generic_i2c_driver[0];
    card->sclreg = MACH64_DAC_CNTL_PTR;
    card->sdareg = MACH64_GEN_TEST_CNTL_PTR;
    card->sclset = 0x01000000; 
    card->sdaset = 0x00000001; 
    card->sdaget = 0x00000008;
    card->scldir = 0x08000000; 
    card->sdadir = 0x00000020;
    save1 = MACH64_DAC_CNTL; 
    save2 = MACH64_GEN_TEST_CNTL; 
    save3 = MACH64_GP_IO;
    *card->sdareg |= 0x00000010; 
    save4 = MACH64_CRTC_H_TOTAL_DISP; //hmm why do this?
    MACH64_GP_IO &= 0x7FFFFFFF; 
    MACH64_CRTC_H_TOTAL_DISP = save4; //hmm why do this?

    ok = i2c_device(card,0xC0) + i2c_device(card,0xC2) +
      i2c_device(card,0xC4) + i2c_device(card,0xC6);

    if (ok != 0 && ok != 4){
dprintk(2,"card(%d) DAC+GEN_TEST i2c driver\n",card->cardnum);
      return 0;
    }
    //else this is not the right i2c driver set everything back to what it was
    MACH64_DAC_CNTL = save1;
    MACH64_GEN_TEST_CNTL = save2;
    MACH64_GP_IO = save3;

    //try next i2c driver  (GP_IO Register)
    card->sclreg = MACH64_GP_IO_PTR;
    card->sdareg = MACH64_GP_IO_PTR; 
    save1 = MACH64_GP_IO;
    card->sclset = 0x00000800; 
    card->sdaset = 0x00000010; 
    card->sdaget = 0x00000010;
    card->scldir = 0x08000000; 
    card->sdadir = 0x00100000;
    ok = i2c_device(card,0xC0) + i2c_device(card,0xC2) +
      i2c_device(card,0xC4) + i2c_device(card,0xC6);

    if (ok != 0 && ok != 4){
dprintk(2,"card(%d) GP_IO i2c driver\n",card->cardnum);
      return 0;
    }
    /* now try bt829 units in case there is no tuner */
    if (ok == 0){
      ok = i2c_device(card,0x88)+i2c_device(card,0x8a);
      if (ok != 0){
        return 0;
      }
    }

    //else nope reset and test next
    MACH64_GP_IO = save1;

    //try next i2c driver LG (GP_IO Register)
    card->sclreg = MACH64_GP_IO_PTR; 
    card->sdareg = MACH64_GP_IO_PTR; 
    save1 = MACH64_GP_IO;
    card->sclset = 0x00000400; 
    card->sdaset = 0x00001000; 
    card->sdaget = 0x00001000;
    card->scldir = 0x04000000; 
    card->sdadir = 0x10000000; 
    ok = i2c_device(card,0xC0) + i2c_device(card,0xC2) +
    i2c_device(card,0xC4) + i2c_device(card,0xC6);

    if (ok != 0 && ok != 4){
dprintk(2,"card(%d) LG GP_IO i2c driver\n",card->cardnum);
      return 0;
    }
    //else nope reset and try next
    MACH64_GP_IO = save1;

    //TB (ImpacTV)
    card->i2c = &generic_i2c_driver[1];
    tvout_write32(card,MACH64_TV_I2C_CNTL,0x00005500|card->tv_i2c_cntl);
    ok = i2c_device(card,0xC0) + i2c_device(card,0xC2) +
      i2c_device(card,0xC4) + i2c_device(card,0xC6);

    if (ok != 0 && ok != 4){
dprintk(2,"card(%d) Impact tv i2c driver\n",card->cardnum);
      return 0;
    }
    //else nope try last one

    //(Rage PRO)
    card->i2c = &generic_i2c_driver[2];
    save1 = MACH64_I2C_CNTL_0; 
    save2 = MACH64_I2C_CNTL_1;
    MACH64_I2C_CNTL_1 = 0x00400000; 
    if (MACH64_I2C_CNTL_1!=0x00400000) 
      return (-ENODEV);

    card->i2c_cntl_0 = 0x0000C000; 
    MACH64_I2C_CNTL_0 = card->i2c_cntl_0|0x00040000; 

    ok = i2c_device(card,0xC0) + i2c_device(card,0xC2) +
      i2c_device(card,0xC4) + i2c_device(card,0xC6);

    if (ok != 0 && ok != 4){
dprintk(2,"card(%d) RagePro i2c driver\n",card->cardnum);
      return 0;
    }  
    MACH64_I2C_CNTL_0 = save1;
    MACH64_I2C_CNTL_1 = save2;
  }
  return (-ENODEV);
}

void pro_scldir(GENERIC_CARD *card,int set) { }
void pro_sdadir(GENERIC_CARD *card,int set) { }

void pro_setscl(GENERIC_CARD *card,int set) 
{
  if (set){
    card->i2c_cntl_0 |= 0x00004000;
  } else {
    card->i2c_cntl_0 &= ~0x00004000;
  }
  MACH64_I2C_CNTL_0 = card->i2c_cntl_0; 
  I2C_SLEEP; 
}

void pro_setsda(GENERIC_CARD *card,int set) 
{
  if (set){
    card->i2c_cntl_0 |= 0x00008000; 
  } else {
    card->i2c_cntl_0 &= ~0x00008000;
  }
  MACH64_I2C_CNTL_0 = card->i2c_cntl_0; 
  I2C_SLEEP; 
}

int pro_getscl(GENERIC_CARD *card) 
{ 
  return (MACH64_I2C_CNTL_0 & 0x00004000); 
}

int pro_getsda(GENERIC_CARD *card) 
{ 
  return (MACH64_I2C_CNTL_0 & 0x00008000); 
}

/* Hardware routines for I2C modes A, B and LG (DAC+GEN_TEST or GP_IO) */
void reg_scldir(GENERIC_CARD *card,int set)
{
  if (set)
    *card->sclreg |= card->scldir;
  else
    *card->sclreg &= ~card->scldir;
}

void reg_sdadir(GENERIC_CARD *card,int set)
{
  if (set)
    *card->sdareg |= card->sdadir;
  else
    *card->sdareg &= ~card->sdadir;
}

void reg_setscl(GENERIC_CARD *card,int set)
{
  if (set)
    *card->sclreg |= card->sclset;
  else
    *card->sclreg &= ~card->sclset;

  I2C_SLEEP;
}

void reg_setsda(GENERIC_CARD *card,int set)
{
  if (set)
    *card->sdareg |= card->sdaset;
  else
    *card->sdareg &= ~card->sdaset;

  I2C_SLEEP;
}

int reg_getscl(GENERIC_CARD *card)
{
  return (*card->sclreg & card->sclset);
}

int reg_getsda(GENERIC_CARD *card)
{
  return (*card->sdareg & card->sdaget);
}

/* Hardware routines for I2C mode TB (ImpacTV) */

void itv_scldir(GENERIC_CARD * card,int set)
{
  if (set)
    card->tv_i2c_cntl |= 0x01;
  else
    card->tv_i2c_cntl &= ~0x01;

  tvout_write_i2c_cntl8(card,card->tv_i2c_cntl);
  I2C_SLEEP;
}

void itv_sdadir(GENERIC_CARD * card,int set)
{
  if (set)
    card->tv_i2c_cntl |= 0x10;
  else
    card->tv_i2c_cntl &= ~0x10;

  tvout_write_i2c_cntl8(card,card->tv_i2c_cntl);
  I2C_SLEEP;
}

void itv_setsda(GENERIC_CARD * card,int set)
{
  if (set)
    card->tv_i2c_cntl |= 0x20;
  else
    card->tv_i2c_cntl &= ~0x20;

  tvout_write_i2c_cntl8(card,card->tv_i2c_cntl);
  I2C_SLEEP;
}

void itv_setscl(GENERIC_CARD * card,int set)
{
  if (set)
    card->tv_i2c_cntl |= 0x02;
  else
    card->tv_i2c_cntl &= ~0x02;

  tvout_write_i2c_cntl8(card,card->tv_i2c_cntl);
  I2C_SLEEP;
}

int itv_getscl(GENERIC_CARD *card)
{
  return (tvout_read_i2c_cntl8(card) & 0x04);
}

int itv_getsda(GENERIC_CARD *card)
{
  return (tvout_read_i2c_cntl8(card) & 0x40);
}

/* Returns 1 if ready, 0 if MPP bus timed out (not ready) */
int mpp_wait(GENERIC_CARD *card) 
{
  u32 tries=MPPTRIES ;
  while (tries && (MPP_CONFIG3 & 0x40))
    tries--; 
  if (tries)
    return 1 ;
  return 0 ;
}

/* Write low byte of ImpacTV TV_I2C_CNTL register */
void tvout_write_i2c_cntl8(GENERIC_CARD *card,u8 data)
{
  mpp_wait(card);
  MPP_CONFIG = MPPNORMAL;
  MPP_DATA0 = data;
}

/* Read low byte of ImpacTV TV_I2C_CNTL register */
u8 tvout_read_i2c_cntl8(GENERIC_CARD *card)
{
  mpp_wait(card);
  MPP_CONFIG = MPPREAD;
  mpp_wait(card);
  return MPPREADBYTE;
}

void i2c_start(GENERIC_CARD *card)
{
  card->i2c->scldir(card,1);
  card->i2c->sdadir(card,1);
  card->i2c->setsda(card,1);
  card->i2c->setscl(card,1);
  card->i2c->setsda(card,0);
  card->i2c->setscl(card,0);
}

void i2c_stop(GENERIC_CARD * card)
{
  card->i2c->sdadir(card,1);
  card->i2c->setscl(card,0);
  card->i2c->setsda(card,0);
  card->i2c->setscl(card,1);
  card->i2c->setsda(card,1);
  card->i2c->scldir(card,0);
  card->i2c->sdadir(card,0);
}

int i2c_sendbyte(GENERIC_CARD *card, u8 data)
{
  int nack, i;

  card->i2c->sdadir(card,1);

  for (i=7; i>=0; i--) {
    card->i2c->setscl(card,0);
    card->i2c->setsda(card,data&(1<<i));
    card->i2c->setscl(card,1);
  }
  card->i2c->setscl(card,0);
  card->i2c->sdadir(card,0);
  card->i2c->setsda(card,1);
  card->i2c->setscl(card,1);
  nack = (card->i2c->getsda(card)) ? 1 : 0;
  card->i2c->setscl(card,0);
  return nack ;
}

int i2c_device(GENERIC_CARD * card,u8 addr)
{
  int nack=0 ;

  if (card->driver_data & RAGE128CHIP){
    r128_reset(card);
    R128_I2C_DATA = addr|1; 
    nack = r128_go(card,0,1,0) ;
  } else {
    i2c_start(card);
    nack = i2c_sendbyte(card,addr);
    i2c_stop(card);
  }
  return !nack;
}

/* Set ImpacTV register index and return MPP_ADDR
 * to 0x0018 for ImpacTV register data access. */
void tvout_addr(GENERIC_CARD *card,u16 addr)
{
  mpp_wait(card);
  MPP_CONFIG = MPPNORMALINC;
  MPP_ADDR = 0x00000008L;
  MPP_DATA0 = addr;
  mpp_wait(card);
  MPP_DATA0 = addr>>8;
  mpp_wait(card);
  MPP_CONFIG = MPPNORMAL;
  MPP_ADDR = 0x00000018L;
}

/* Write to ImpacTV register */
void tvout_write32(GENERIC_CARD *card, u16 addr, u32 data)
{
  tvout_addr(card,addr);

  mpp_wait(card);
  MPP_CONFIG = MPPNORMALINC ;
  MPP_DATA0 = data ;       mpp_wait(card) ;
  MPP_DATA0 = data >> 8 ;  mpp_wait(card) ;
  MPP_DATA0 = data >> 16 ; mpp_wait(card) ;
  MPP_DATA0 = data >> 24 ; mpp_wait(card) ;
  if (addr != MACH64_TV_I2C_CNTL) tvout_addr(card,MACH64_TV_I2C_CNTL) ;
}

int r128_wait_ack(GENERIC_CARD *card) {
  int nack=0, n1=0, n2=0 ;
  while (n1++ < 10 && (R128_I2C_CNTL_0_1 & (I2C_GO>>8))) udelay(15);
  while (n2++ < 10 && !nack) { 
    nack = R128_I2C_CNTL_0_0 & (I2C_DONE|I2C_NACK|I2C_HALT);
    if (!nack)
      udelay(15); 
  }
  return (nack != I2C_DONE);
} 

void r128_reset(GENERIC_CARD *card) {
  R128_I2C_CNTL_0 = I2C_SOFT_RST | I2C_HALT | I2C_NACK | I2C_DONE;
}

int r128_go(GENERIC_CARD *card,int naddr, int ndata, u32 flags) {
  R128_I2C_CNTL_1 = (card->R128_TIME<<24) | I2C_EN | I2C_SEL 
  	| (naddr<<8) | ndata;
  R128_I2C_CNTL_0 = (card->R128_N<<24) | (card->R128_M<<16) 
  	| I2C_GO | I2C_START | I2C_STOP | I2C_DRIVE_EN | flags;
  return r128_wait_ack(card); 
}

u8 i2c_read(GENERIC_CARD *card,u8 addr) 
{
  int nack=0;
  u8 data;

  if (card->driver_data & RAGE128CHIP){
    r128_reset(card);
    R128_I2C_DATA = addr|1;
    nack = r128_go(card,1,1,I2C_RECEIVE);
    data = (nack) ? 0xFF : R128_I2C_DATA;
  } else {
    i2c_start(card);
    i2c_sendbyte(card,addr|1);
    data = i2c_readbyte(card,1);
    i2c_stop(card);
  }

  return data;
}

u8 i2c_readbyte(GENERIC_CARD *card,int last)
{
  int i;
  u8 data=0;

  card->i2c->sdadir(card,0) ;
  for (i=7;i>=0;i--) {
    card->i2c->setscl(card,1);
    if (card->i2c->getsda(card))
      data |= (1<<i);
    I2C_SLEEP;
    card->i2c->setscl(card,0);
  }
  card->i2c->sdadir(card,1);
  card->i2c->setsda(card,last);
  card->i2c->setscl(card,1);
  card->i2c->setscl(card,0);
  card->i2c->sdadir(card,0);

  return data;
}

u8 i2c_readreg8(GENERIC_CARD *card,u8 addr, u8 reg)
{
  u8 data ;

  if (card->driver_data & RAGE128CHIP){
    i2c_write(card,addr,&reg,1); 
    data = i2c_read(card,addr);
  } else {
    i2c_start(card); 
    i2c_sendbyte(card,addr); 
    i2c_sendbyte(card,reg);
    i2c_start(card); 
    i2c_sendbyte(card,addr|1);
    data = i2c_readbyte(card,1);
    i2c_stop(card);
  }

  return data ;
}

int i2c_writereg8(GENERIC_CARD *card,u8 addr, u8 reg, u8 value)
{
  u8 data[2];

  data[0] = reg;
  data[1] = value;
  i2c_write(card,addr,data,2);

  return 0;
}

int i2c_write(GENERIC_CARD *card,u8 addr, u8 *data, int count)
{
  int nack=-1,c=count;

  if (card->driver_data & RAGE128CHIP){
    r128_reset(card); 
    R128_I2C_DATA = addr;
    while (c > 0) {
      --c;
      R128_I2C_DATA = *data; 
      ++data;
    }
    data -= count; 
    nack = r128_go(card,1,count,0);
  } else {
    i2c_start(card);
    i2c_sendbyte(card,addr);
    while (count > 0) {
      nack = i2c_sendbyte(card,*data);
      ++data;
      --count;
    }
    i2c_stop(card);
  }

  return nack ? -1 : 0;
}
