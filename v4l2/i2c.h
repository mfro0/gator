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


#ifndef GENERIC_I2C_HEADER
#define GENERIC_I2C_HEADER 1

/* impact tv stuff */
#define MPPTRIES        100000
#define MPP_CONFIG            (*(card->MEM_1+0x30))
#define MPP_CONFIG3           (*(((volatile u8*)(card->MEM_1+0x30))+3))
#define MPPREAD         0x84038CB0L     /* prefetch read */
#define MPP_DATA0             (*(((volatile u8*)(card->MEM_1+0x33))+0))
#define MPPREADBYTE           (*(((volatile u8*)(card->MEM_1+0x33))+0))
#define MPPNORMAL       0x80038CB0L     /* write */
#define MPPNORMALINC    0x80338CB0L     /* write, autoincrement MPP_ADDR */
#define MPP_ADDR              (*(card->MEM_1+0x32))

#define I2C_SLEEP udelay(15)

/* Function pointers used by low level functions */
struct i2c_funcs {
  void (*scldir)(GENERIC_CARD *,int);
  void (*setscl)(GENERIC_CARD *,int);
  int (*getscl)(GENERIC_CARD *);
  void (*sdadir)(GENERIC_CARD *,int);
  void (*setsda)(GENERIC_CARD *,int);
  int (*getsda)(GENERIC_CARD *);
};

void reg_scldir(GENERIC_CARD *card,int set);
void reg_sdadir(GENERIC_CARD *card,int set);
void reg_setscl(GENERIC_CARD *card,int set);
void reg_setsda(GENERIC_CARD *card,int set);
int reg_getscl(GENERIC_CARD *card);
int reg_getsda(GENERIC_CARD *card);
void itv_scldir(GENERIC_CARD * card,int set);
void itv_sdadir(GENERIC_CARD * card,int set);
void itv_setsda(GENERIC_CARD * card,int set);
void itv_setscl(GENERIC_CARD * card,int set);
int itv_getscl(GENERIC_CARD *card);
int itv_getsda(GENERIC_CARD *card);
int mpp_wait(GENERIC_CARD *card);
void tvout_write_i2c_cntl8(GENERIC_CARD *card,u8 data);
u8 tvout_read_i2c_cntl8(GENERIC_CARD *card);
void i2c_start(GENERIC_CARD *card);
void i2c_stop(GENERIC_CARD * card);
int i2c_sendbyte(GENERIC_CARD *card, u8 data);
int i2c_device(GENERIC_CARD * card,u8 addr);
void tvout_addr(GENERIC_CARD *card,u16 addr);
void tvout_write32(GENERIC_CARD *card, u16 addr, u32 data);
u8 i2c_read(GENERIC_CARD *card,u8 addr);
u8 i2c_readbyte(GENERIC_CARD *card,int last);
u8 i2c_readreg8(GENERIC_CARD *card,u8 addr, u8 reg);
int i2c_writereg8(GENERIC_CARD *card,u8 addr, u8 reg, u8 value);
int i2c_write(GENERIC_CARD *card,u8 addr, u8 *data, int count);
void pro_scldir(GENERIC_CARD *card, int set);
void pro_sdadir(GENERIC_CARD *card, int set);
void pro_setscl(GENERIC_CARD *card, int set);
void pro_setsda(GENERIC_CARD *card, int set);
int pro_getscl(GENERIC_CARD *card);
int pro_getsda(GENERIC_CARD *card);
int i2c_init(GENERIC_CARD *card);
void r128_reset(GENERIC_CARD *card);
int r128_go(GENERIC_CARD *card,int naddr, int ndata, u32 flags);
int r128_wait_ack(GENERIC_CARD *card);

#endif
