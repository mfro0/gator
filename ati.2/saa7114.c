#include "xf86.h"
#include "xf86i2c.h"
#include "saa7114.h"
#include "saa7114_regs.h"
#include "i2c_def.h"

static CARD8 saaread(SAA7114Ptr saa, CARD8 reg)
{
  CARD8 v;

  I2C_WriteRead(&(saa->d), &reg, 1, &v, 1);

  return v;
}

static void saawrite(SAA7114Ptr saa, CARD8 reg, CARD8 val)
{
  CARD8 data[2];

  data[0] = reg;
  data[1] = val;
  I2C_WriteRead(&(saa->d), data, 2, NULL, 0);
}

static void saablockwrite(SAA7114Ptr saa, CARD8 startreg, CARD8 *data, int num_values)
{
#define I2C_TRANSFER_SIZE 8
CARD8 block[I2C_TRANSFER_SIZE];
int i;
int length;

for(i=0;i<num_values;i+=I2C_TRANSFER_SIZE-1){
	block[0]=startreg+i;
	length=num_values-i;
	if(length>I2C_TRANSFER_SIZE-1)length=I2C_TRANSFER_SIZE-1;
	memcpy(&(block[1]), &(data[i]), length);
	I2C_WriteRead(&(saa->d), block, length+1, NULL, 0);
	}
}

#define RT_regr(reg,data)	saa7114_read(t,(reg),(data))
#define RT_regw(reg,data)	saa7114_write(t,(reg),(data))
#define VIP_TYPE      "ATI VIP BUS"


SAA7114Ptr DetectSAA7114(I2CBusPtr b, I2CSlaveAddr addr)
{
  SAA7114Ptr saa;
  I2CByte a;

  saa = xcalloc(1, sizeof(SAA7114Rec));
  if(saa == NULL) return NULL;
  saa->d.DevName = strdup("SAA7114 video decoder");
  saa->d.SlaveAddr = addr;
  saa->d.pI2CBus = b;
  saa->d.NextDev = NULL;
  saa->d.StartTimeout = b->StartTimeout;
  saa->d.BitTimeout = b->BitTimeout;
  saa->d.AcknTimeout = b->AcknTimeout;
  saa->d.ByteTimeout = b->ByteTimeout;


  if(!I2C_WriteRead(&(saa->d), NULL, 0, &a, 1))
  {
     xfree(saa);
     return NULL;
  }

  saa->id = saaread(saa,SAA7114_CHIP_VERSION);

  xfree(saa->d.DevName);
  saa->d.DevName = xcalloc(200, sizeof(char));
  sprintf(saa->d.DevName, "saa7114 video decoder, version %d",(saa->id >>4) & 0xf);

  /* set default parameters */
  if(!I2CDevInit(&(saa->d)))
  {
     xfree(saa);
     return NULL;
  }

  return saa;
}

#define NUM_VALUES 25
CARD8 NTSC_VALUES[NUM_VALUES]={
	0x08, 0xc0, 0x10, 0x90, 0x90, 0xeb, 0xe0, 0x98, 0x40, 0x80, 0x44, 0x40,
	0x00, 0x89, 0x2a, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x11, 0xfe, 0x30, 0x30, 0x80
	};

void InitSAA7114(SAA7114Ptr saa)
{
    CARD32 data;

    saablockwrite(saa, 0x01, NTSC_VALUES, NUM_VALUES);
}


void ShutdownSAA7114(SAA7114Ptr t)
{

}

void DumpSAA7114Regs(SAA7114Ptr t)
{
    int i;
    CARD32 data;
    

}

