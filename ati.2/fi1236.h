#ifndef __FI1236_H__
#define __FI1236_H__

#include "xf86i2c.h"

/* why someone has defined NUM someplace else is beyoung me.. */
#undef NUM

typedef struct {
	CARD32 fcar;           /* 16 * fcar_Mhz */
	CARD32 min_freq;       /* 16 * min_freq_Mhz */
	CARD32 max_freq;       /* 16 * max_freq_Mhz */
	
	CARD32 threshold1;     /* 16 * Value_Mhz */
	CARD32 threshold2;     /* 16 * Value_Mhz */
	
	CARD8  band_low;
	CARD8  band_mid;
	CARD8  band_high;
	CARD8  control;
	} FI1236_parameters;


typedef struct {
	/* what we want */
	/* all frequencies are in Mhz */
	double f_rf;	/* frequency to tune to */
	double f_if1;   /* first intermediate frequency */
	double f_if2;   /* second intermediate frequency */
	double f_ref;   /* reference frequency */
	double f_ifbw;  /* bandwidth */
	double f_step;  /* step */
	
	/* what we computer */
	double f_lo1;
	double f_lo2;
	int LO1I;
	int LO2I;
	int SEL;
	int STEP;
	int NUM;
	} MT2032_parameters;

typedef struct {
	I2CDevRec  d;
	int type;
	
	FI1236_parameters parm;
	
	struct {
		CARD8   div1;
		CARD8   div2;
		CARD8   control;
		CARD8   band;
		} tuner_data;
	} FI1236Rec, *FI1236Ptr;

#define TUNER_TYPE_FI1236              0
#define TUNER_TYPE_FI1216              1
#define TUNER_TYPE_TEMIC_FN5AL         2
#define TUNER_TYPE_MT2032	       3

#define FI1236_ADDR(a)        ((a)->d.SlaveAddr)

#define FI1236_ADDR_1	     0xC6
#define FI1236_ADDR_2        0xC0


FI1236Ptr Detect_FI1236(I2CBusPtr b, I2CSlaveAddr addr);
void FI1236_set_tuner_type(FI1236Ptr f, int type);
void FI1236_tune(FI1236Ptr f, CARD32 frequency);

#define FI1236SymbolsList  \
		"Detect_FI1236", \
		"FI1236_set_tuner_type", \
		"FI1236_tune"

#ifdef XFree86LOADER

#define xf86_Detect_FI1236         ((FI1236Ptr (*)(I2CBusPtr, I2CSlaveAddr))LoaderSymbol("Detect_FI1236"))
#define xf86_FI1236_set_tuner_type ((void (*)(FI1236Ptr, int))LoaderSymbol("FI1236_set_tuner_type"))
#define xf86_FI1236_tune           ((void (*)(FI1236Ptr, CARD32))LoaderSymbol("FI1236_tune"))


#else

#define xf86_Detect_FI1236         Detect_FI1236
#define xf86_FI1236_set_tuner_type FI1236_set_tuner_type
#define xf86_FI1236_tune           FI1236_tune

#endif

#endif
