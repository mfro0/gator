#include "xf86.h"
#include "xf86i2c.h"
#include "fi1236.h"
#include "i2c_def.h"

#define NUM_TUNERS    3

const FI1236_parameters tuner_parms[NUM_TUNERS] =
{
    /* 0 - FI1236 */
    { 733 ,884 ,12820 ,2516 ,7220 ,0xA2 ,0x94, 0x34, 0x8e },
    /* !!!based on documentation - it should be:
    {733 ,16*55.25 ,16*801.25 ,16*160 ,16*454 ,0xA0 ,0x90, 0x30, 0x8e},*/
    
    /* 1 - FI1216 */
    { 623 ,16*48.75 ,16*855.25 ,16*170 ,16*450 ,0xA0 ,0x90, 0x30, 0x8e },
    /* 2 - TEMIC FN5AL */
    { 623 ,16*45.75 ,16*855.25 ,16*169 ,16*454 ,0xA0 ,0x90, 0x30, 0x8e }
};


FI1236Ptr Detect_FI1236(I2CBusPtr b, I2CSlaveAddr addr)
{
   FI1236Ptr f;
   I2CByte a;

   f = xcalloc(1,sizeof(FI1236Rec));
   if(f == NULL) return NULL;
   f->d.DevName = "FI12xx Tuner";
   f->d.SlaveAddr = addr;
   f->d.pI2CBus = b;
   f->d.NextDev = NULL;
   f->d.StartTimeout = b->StartTimeout;
   f->d.BitTimeout = b->BitTimeout;
   f->d.AcknTimeout = b->AcknTimeout;
   f->d.ByteTimeout = b->ByteTimeout;
  
   if(!I2C_WriteRead(&(f->d), NULL, 0, &a, 1))
   {
   	free(f);
	return NULL;
    }
    FI1236_set_tuner_type(f, TUNER_TYPE_FI1236);
    if(!I2CDevInit(&(f->d)))
    {
       free(f);
       return NULL;
    }
    return f;
}

void FI1236_set_tuner_type(FI1236Ptr f, int type)
{
if(type<0)type = 0;
if(type>=NUM_TUNERS)type = NUM_TUNERS-1;
memcpy(&(f->parm), &(tuner_parms[type]), sizeof(FI1236_parameters));
}

void FI1236_tune(FI1236Ptr f, CARD32 frequency)
{
    CARD16 divider;

    if(frequency < f->parm.min_freq) frequency = f->parm.min_freq;
    if(frequency > f->parm.max_freq) frequency = f->parm.max_freq;

    divider = (f->parm.fcar+(CARD16)frequency) & 0x7fff;
    f->tuner_data.div1 = (CARD8)((divider>>8)&0x7f);
    f->tuner_data.div2 = (CARD8)(divider & 0xff);
    f->tuner_data.control = f->parm.control; 

    if(frequency < f->parm.threshold1)
    {
        f->tuner_data.band = f->parm.band_low;
    } 
    else if (frequency < f->parm.threshold2)
    {
        f->tuner_data.band = f->parm.band_mid;
    }
    else
    {
        f->tuner_data.band = f->parm.band_high;
    }

#if 0
    xf86DrvMsg(f->d.pI2CBus->scrnIndex, X_INFO, "Setting tuner frequency to %d\n", frequency);
#endif    
    I2C_WriteRead(&(f->d), (I2CByte *)&(f->tuner_data), 4, NULL, 0);
}
