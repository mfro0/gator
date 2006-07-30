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

static struct i2c_funcs* i2c_getfuncs(void); /* sleazy C proto avoidance */

int i2c_init(GENERIC_CARD *card)
{
  struct i2c_funcs *i2cf;
  int idx, ok;
  u32 saves[4];
  unsigned long nm;
  
  i2cf = i2c_getfuncs(); /* sleazy C proto avoidance */
  
  if (card->driver_data & RAGE128CHIP){
    /* No banging, handled specially.  Set to all NULLs JIC. */
    idx = 0;
    while (i2cf[idx].name) idx++;
    card->i2c = &i2cf[idx];
    
    nm = card->refclock * 10000 / (4*R128_CLOCK_FREQ);
    for (card->R128_N=1 ; card->R128_N < 255 ; card->R128_N++)
      if (card->R128_N * (card->R128_N-1) > nm)
	break;
    card->R128_M = card->R128_N - 1; 
    card->R128_TIME = 2*card->R128_N;
    R128_I2C_CNTL_1   = (card->R128_TIME<<24) | I2C_SEL | I2C_EN;
    R128_I2C_CNTL_0_0 = I2C_DONE | I2C_NACK | I2C_HALT | I2C_SOFT_RST |
                        I2C_DRIVE_EN | I2C_DRIVE_SEL; 


    /* Probes can "succeed" if the pin is stuck high.  At least 
       one of these should fail.  If stuck low, or no hardware,
       all will fail as well.  Do minimum IO to make the call.
    */
    ok = i2c_device(card,0xC0); /* tuners */
    if ((i2c_device(card,0xC2) == ok) && 
	(i2c_device(card,0xC4) == ok) && 
	(i2c_device(card,0xC6) == ok) && 
	(i2c_device(card,0x88) == ok) && /* grabbers (bt8xx) */
	(i2c_device(card,0x8a) == ok)) {
      /* pin stuck or no harware present */
      R128_I2C_CNTL_1 = 0x0;
      return (-ENODEV);
    }

    printk(KERN_INFO "card(%d) Rage128 i2c driver\n",card->cardnum);
    return 0;
  } else if (card->driver_data & MACH64CHIP){

    idx = 0;
    while (i2cf[idx].name) {
      card->i2c = &i2cf[idx];

      dprintk(3, "card(%d) Trying %s I2C driver\n",
	      card->cardnum,i2cf[idx].name);
      if (!card->i2c->init(card,saves)) {
	/* Probes can "succeed" if the pin is stuck high.  At least 
	   one of these should fail.  If stuck low, or no hardware,
	   all will fail as well.  Do minimum IO to make the call.
	*/
	ok =   i2c_device(card,0xC0); /* tuners */
	if (!((i2c_device(card,0xC2) == ok) && 
	      (i2c_device(card,0xC4) == ok) && 
	      (i2c_device(card,0xC6) == ok) && 
	      (i2c_device(card,0x88) == ok) && /* grabbers (bt8xx) */
	      (i2c_device(card,0x8a) == ok))) {
	  /* found */
	  dprintk(2, "card(%d) %s I2C driver succeeded\n",
		  card->cardnum,i2cf[idx].name);
	  return 0;
	}
      }
      card->i2c->deinit(card, saves);
      idx++;
    }
    printk(KERN_INFO "No hardware found via i2c.\n");
  }
  return (-ENODEV);
}



/* Type A: I2C bus accessed through DAC_CNTL and GEN_TEST_CNTL */

static int dac_init(GENERIC_CARD *card, u32 *saves) {
  saves[0] = MACH64_DAC_CNTL; 
  saves[1] = MACH64_GEN_TEST_CNTL; 
  saves[2] = MACH64_GP_IO;
  MACH64_GEN_TEST_CNTL |= 0x00000010; 
  saves[3] = MACH64_CRTC_H_TOTAL_DISP; //hmm why do this?
  MACH64_GP_IO &= 0x7FFFFFFF; 
  MACH64_CRTC_H_TOTAL_DISP = saves[3]; //hmm why do this?
  return 0;
}

static void dac_deinit(GENERIC_CARD *card, u32 *saves) {
  MACH64_DAC_CNTL = saves[0];
  MACH64_GEN_TEST_CNTL = saves[1];
  MACH64_GP_IO = saves[2];
}

static void dac_scldir(GENERIC_CARD *card, int set)
{
  set ? (MACH64_DAC_CNTL |= 0x08000000) : (MACH64_DAC_CNTL &= 0xf7ffffff);
}

static void dac_sdadir(GENERIC_CARD *card, int set)
{
  set ? (MACH64_GEN_TEST_CNTL|=0x00000020):(MACH64_GEN_TEST_CNTL&=0xffffffdf);
}

static void dac_setscl(GENERIC_CARD *card, int set)
{
  set ? (MACH64_DAC_CNTL |= 0x01000000) : (MACH64_DAC_CNTL &= 0xfeffffff);
  I2C_SLEEP;
}

static void dac_setsda(GENERIC_CARD *card, int set)
{
  set ? (MACH64_GEN_TEST_CNTL|=0x00000001):(MACH64_GEN_TEST_CNTL&=0xfffffffe);
  I2C_SLEEP;
}

static int dac_getscl(GENERIC_CARD *card)
{
  return (MACH64_DAC_CNTL & 0x01000000);
}

static int dac_getsda(GENERIC_CARD *card)
{
  return (MACH64_GEN_TEST_CNTL & 0x00000008);
}


/* Type B: Routines for I2C bus accessed through GP_IO_B and GP_IO_4 */

static int gb4_init(GENERIC_CARD *card, u32 *saves) {
  saves[0] = MACH64_GP_IO;
  return 0;
}

static void gb4_deinit(GENERIC_CARD *card, u32 *saves) {
  MACH64_GP_IO = saves[0]; 
}

static void gb4_scldir(GENERIC_CARD *card, int set)
{
  set ? (MACH64_GP_IO |= 0x08000000) : (MACH64_GP_IO &= 0xf7ffffff);
}

static void gb4_sdadir(GENERIC_CARD *card, int set)
{
  set ? (MACH64_GP_IO |= 0x00100000) : (MACH64_GP_IO &= 0xffefffff);
}

static void gb4_setscl(GENERIC_CARD *card, int set)
{
  set ? (MACH64_GP_IO |= 0x00000800) : (MACH64_GP_IO &= 0xfffff7ff);
  I2C_SLEEP;
}

static void gb4_setsda(GENERIC_CARD *card, int set)
{
  set ? (MACH64_GP_IO |=0x00000010) : (MACH64_GP_IO &=0xffffffef);
  I2C_SLEEP;
}

static int gb4_getscl(GENERIC_CARD *card)
{
  return (MACH64_GP_IO & 0x00000800);
}

static int gb4_getsda(GENERIC_CARD *card)
{
  return (MACH64_GP_IO & 0x00000010);
}


/* Type C: I2C bus accessed through GP_IO_A and GP_IO_C */

static int gac_init(GENERIC_CARD *card, u32 *saves) {
  saves[0] = MACH64_GP_IO;
  return 0;
}

static void gac_deinit(GENERIC_CARD *card, u32 *saves) {
  MACH64_GP_IO = saves[0]; 
}

static void gac_scldir(GENERIC_CARD *card, int set)
{
  set ? (MACH64_GP_IO |= 0x04000000) : (MACH64_GP_IO &= 0xfbffffff);
}

static void gac_sdadir(GENERIC_CARD *card, int set)
{
  set ? (MACH64_GP_IO |= 0x10000000) : (MACH64_GP_IO &= 0xefffffff);
}

static void gac_setscl(GENERIC_CARD *card, int set)
{
  set ? (MACH64_GP_IO |= 0x00000400) : (MACH64_GP_IO &= 0xfffffbff);
  I2C_SLEEP;
}

static void gac_setsda(GENERIC_CARD *card, int set)
{
  set ? (MACH64_GP_IO |=0x00001000) : (MACH64_GP_IO &=0xffffefff);
  I2C_SLEEP;
}

static int gac_getscl(GENERIC_CARD *card)
{
  return (MACH64_GP_IO & 0x00000400);
}

static int gac_getsda(GENERIC_CARD *card)
{
  return (MACH64_GP_IO & 0x00001000);
}


/* Type BPM (Broken Pro/Mobility) I2C access through GPIO_D and GPIO_C */
static int gdc_init(GENERIC_CARD *card, u32 *saves) {
  saves[0] = MACH64_GP_IO;
  saves[1] = MACH64_I2C_CNTL_1;
  MACH64_I2C_CNTL_1 |= 0x00400000; /* bit 23 does something too, but what? */
  return 0;
}

static void gdc_deinit(GENERIC_CARD *card, u32 *saves) {
  MACH64_GP_IO = saves[0]; 
  MACH64_I2C_CNTL_1 = saves[1]; 
}

/* Needs 8-bit access */
#define MACH64_GP_IO_B1 *(( u8*)MACH64_GP_IO_PTR + 1)
#define MACH64_GP_IO_B3 *(( u8*)MACH64_GP_IO_PTR + 3)
static void gdc_scldir(GENERIC_CARD *card, int set)
{
  set ? (MACH64_GP_IO_B3 |= 0x20) : (MACH64_GP_IO_B3 &= 0xdf);
}

static void gdc_sdadir(GENERIC_CARD *card, int set)
{
  set ? (MACH64_GP_IO_B3 |= 0x10) : (MACH64_GP_IO_B3 &= 0xef);
}

static void gdc_setscl(GENERIC_CARD *card, int set)
{
  set ? (MACH64_GP_IO_B1 |= 0x20) : (MACH64_GP_IO_B1 &= 0xdf);
  I2C_SLEEP;
}

static void gdc_setsda(GENERIC_CARD *card, int set)
{
  set ? (MACH64_GP_IO_B1 |= 0x10) : (MACH64_GP_IO_B1 &=0xef);
  I2C_SLEEP;
}

static int gdc_getscl(GENERIC_CARD *card)
{
  return (MACH64_GP_IO_B1 & 0x20);
}

static int gdc_getsda(GENERIC_CARD *card)
{
  return (MACH64_GP_IO_B1 & 0x10);
}


/* Type C: Rage Pro I2C control registers */

static int pro_init(GENERIC_CARD *card, u32 *saves) {
  saves[0] = MACH64_I2C_CNTL_0; 
  saves[1] = MACH64_I2C_CNTL_1;
  MACH64_I2C_CNTL_1 = 0x00400000; 
  if (MACH64_I2C_CNTL_1!=0x00400000) return (-ENODEV);

  card->i2c_cntl_0 = 0x0000C000; 
  MACH64_I2C_CNTL_0 = card->i2c_cntl_0|0x00040000; 
  return 0;
}

static void pro_deinit(GENERIC_CARD *card, u32 *saves) {
  MACH64_I2C_CNTL_0 = saves[0];
  MACH64_I2C_CNTL_1 = saves[1];
}

static void pro_scldir(GENERIC_CARD *card,int set) { }
static void pro_sdadir(GENERIC_CARD *card,int set) { }

static void pro_setscl(GENERIC_CARD *card,int set) 
{
  if (set){
    card->i2c_cntl_0 |= 0x00004000;
  } else {
    card->i2c_cntl_0 &= ~0x00004000;
  }
  MACH64_I2C_CNTL_0 = card->i2c_cntl_0; 
  I2C_SLEEP; 
}

static void pro_setsda(GENERIC_CARD *card,int set) 
{
  if (set){
    card->i2c_cntl_0 |= 0x00008000; 
  } else {
    card->i2c_cntl_0 &= ~0x00008000;
  }
  MACH64_I2C_CNTL_0 = card->i2c_cntl_0; 
  I2C_SLEEP; 
}

static int pro_getscl(GENERIC_CARD *card) 
{ 
  return (MACH64_I2C_CNTL_0 & 0x00004000); 
}

static int pro_getsda(GENERIC_CARD *card) 
{ 
  return (MACH64_I2C_CNTL_0 & 0x00008000); 
}


/* Type TB: Hardware routines for ImpacTV I2C access */

/* Returns 1 if ready, 0 if MPP bus timed out (not ready) */
static int mpp_wait(GENERIC_CARD *card) 
{
  u32 tries=MPPTRIES ;
  while (tries && (MPP_CONFIG3 & 0x40))
    tries--; 
  if (tries)
    return 1 ;
  return 0 ;
}

/* Set ImpacTV register index and return MPP_ADDR
 * to 0x0018 for ImpacTV register data access. */
static void tvout_addr(GENERIC_CARD *card,u16 addr)
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
static void tvout_write32(GENERIC_CARD *card, u16 addr, u32 data)
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

/* Write low byte of ImpacTV TV_I2C_CNTL register */
static void tvout_write_i2c_cntl8(GENERIC_CARD *card,u8 data)
{
  mpp_wait(card);
  MPP_CONFIG = MPPNORMAL;
  MPP_DATA0 = data;
}

/* Read low byte of ImpacTV TV_I2C_CNTL register */
static u8 tvout_read_i2c_cntl8(GENERIC_CARD *card)
{
  mpp_wait(card);
  MPP_CONFIG = MPPREAD;
  mpp_wait(card);
  return MPPREADBYTE;
}

static int itv_init (GENERIC_CARD *card, u32 *saves) {
 tvout_write32(card, MACH64_TV_I2C_CNTL, 0x00005500 | card->tv_i2c_cntl);
 return 0;
}

static void itv_deinit (GENERIC_CARD *card, u32 *saves) { }

static void itv_scldir(GENERIC_CARD * card,int set)
{
  if (set)
    card->tv_i2c_cntl |= 0x01;
  else
    card->tv_i2c_cntl &= ~0x01;

  tvout_write_i2c_cntl8(card,card->tv_i2c_cntl);
  I2C_SLEEP;
}

static void itv_sdadir(GENERIC_CARD * card,int set)
{
  if (set)
    card->tv_i2c_cntl |= 0x10;
  else
    card->tv_i2c_cntl &= ~0x10;

  tvout_write_i2c_cntl8(card,card->tv_i2c_cntl);
  I2C_SLEEP;
}

static void itv_setsda(GENERIC_CARD * card,int set)
{
  if (set)
    card->tv_i2c_cntl |= 0x20;
  else
    card->tv_i2c_cntl &= ~0x20;

  tvout_write_i2c_cntl8(card,card->tv_i2c_cntl);
  I2C_SLEEP;
}

static void itv_setscl(GENERIC_CARD * card,int set)
{
  if (set)
    card->tv_i2c_cntl |= 0x02;
  else
    card->tv_i2c_cntl &= ~0x02;

  tvout_write_i2c_cntl8(card,card->tv_i2c_cntl);
  I2C_SLEEP;
}

static int itv_getscl(GENERIC_CARD *card)
{
  return (tvout_read_i2c_cntl8(card) & 0x04);
}

static int itv_getsda(GENERIC_CARD *card)
{
  return (tvout_read_i2c_cntl8(card) & 0x40);
}

/* Rage128 I2C routines */

static int r128_wait_ack(GENERIC_CARD *card) {
  int nack=0, n1=0, n2=0 ;
  while (n1++ < 10 && (R128_I2C_CNTL_0_1 & (I2C_GO>>8))) udelay(15);
  while (n2++ < 10 && !nack) { 
    nack = R128_I2C_CNTL_0_0 & (I2C_DONE|I2C_NACK|I2C_HALT);
    if (!nack)
      udelay(15); 
  }
  return (nack != I2C_DONE);
} 

static void r128_reset(GENERIC_CARD *card) {
  R128_I2C_CNTL_0 = I2C_SOFT_RST | I2C_HALT | I2C_NACK | I2C_DONE;
}

static int r128_go(GENERIC_CARD *card,int naddr, int ndata, u32 flags) {
  R128_I2C_CNTL_1 = (card->R128_TIME<<24) | I2C_EN | I2C_SEL 
  	| (naddr<<8) | ndata;
  R128_I2C_CNTL_0 = (card->R128_N<<24) | (card->R128_M<<16) 
  	| I2C_GO | I2C_START | I2C_STOP | I2C_DRIVE_EN | flags;
  return r128_wait_ack(card); 
}


/* Mach64/Rage128 I2C Intraface */

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
  dprintk(7, "%s @ %x result %d\n", card->i2c->name, addr, !nack);
  return !nack;
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

/* Drivers are tried in order listed here. */
static struct i2c_funcs generic_i2c_driver[] = {
  { "type A (DAC_CNTL/GEN_TEST_CNTL)", &dac_init, &dac_deinit,
    &dac_scldir, &dac_setscl, &dac_getscl,
    &dac_sdadir, &dac_setsda, &dac_getsda },
  { "type B (GP_IO pins B and 4)", &gb4_init, &gb4_deinit,
    &gb4_scldir, &gb4_setscl, &gb4_getscl,
    &gb4_sdadir, &gb4_setsda, &gb4_getsda },
  { "type LG (GP_IO pins A and C)", &gac_init, &gac_deinit,
    &gac_scldir, &gac_setscl, &gac_getscl,
    &gac_sdadir, &gac_setsda, &gac_getsda },
  { "type C (Pro I2C registers)", &pro_init, &pro_deinit,
    &pro_scldir, &pro_setscl, &pro_getscl,
    &pro_sdadir, &pro_setsda, &pro_getsda },
  { "type BPM (GP_IO pins D and C)", &gdc_init, &gdc_deinit,
    &gdc_scldir, &gdc_setscl, &gdc_getscl,
    &gdc_sdadir, &gdc_setsda, &gdc_getsda },
  { "type TB (ImpacTV)", &itv_init, &itv_deinit,
    &itv_scldir, &itv_setscl, &itv_getscl,
    &itv_sdadir, &itv_setsda, &itv_getsda },
  { NULL, NULL, NULL, 
    NULL, NULL, NULL, 
    NULL, NULL, NULL} /* List terminator + Rage128 */
};

static struct i2c_funcs *i2c_getfuncs(void) { return generic_i2c_driver; };
