#ifndef __BT829_H__
#define __BT829_H__

#include "xf86i2c.h"

typedef struct {
	I2CDevRec d;
	
	int 		width;
	int		height;
    	CARD16		adelay;
    	CARD16		bdelay;
    	CARD16		hdelay;
    	CARD16		vdelay;
    	CARD16		luma;
    	CARD16		sat_u;
    	CARD16		sat_v;
    	int		xtsel;
    	int		htotal;
    	int		vtotal;
    	int		ldec;
    	int		cbsense;
    	int		format;
    	int		mux;
    	int		iHue;
    	int		iSaturation;
    	int		iBrightness;
    	int		iContrast;
    	int 		CCmode;
    	CARD8		id;
    	CARD8		vpole;
	int 		tunertype;
	} BT829Rec, *BT829Ptr;
	
#define BT829_ADDR_1   0x8a
#define BT829_ADDR_2   0x88

#define BT829_PAL      3
#define BT829_NTSC     1
#define BT829_SECAM    6

BT829Ptr Detect_bt829(I2CBusPtr b, I2CSlaveAddr addr);
Bool bt829_init(BT829Ptr bt);
void bt829_setformat(BT829Ptr bt, int format);
void bt829_setmux(BT829Ptr bt);
void bt829_SetBrightness(BT829Ptr bt, int b);
void bt829_SetContrast(BT829Ptr bt, int c);
void bt829_SetSaturation(BT829Ptr bt, int s);
void bt829_SetTint(BT829Ptr bt, int h);
void bt829_setcaptsize(BT829Ptr bt, int width, int height);
int bt829_setCC(BT829Ptr bt);

#define BT829SymbolsList   \
		"Detect_bt829", \
		"bt829_init", \
		"bt829_setformat", \
		"bt829_setmux", \
		"bt829_SetBrightness", \
		"bt829_SetContrast", \
		"bt829_SetSaturation", \
		"bt829_SetTint", \
		"bt829_setcaptsize", \
		"bt829_setCC"

#ifdef XFree86LOADER

#define xf86_Detect_bt829         ((BT829Ptr (*)(I2CBusPtr, I2CSlaveAddr))LoaderSymbol("Detect_bt829"))
#define xf86_bt829_init           ((Bool (*)(BT829Ptr))LoaderSymbol("bt829_init"))
#define xf86_bt829_setformat      ((void (*)(BT829Ptr, int))LoaderSymbol("bt829_setformat"))
#define xf86_bt829_setmux         ((void (*)(BT829Ptr))LoaderSymbol("bt829_setmux"))
#define xf86_bt829_setcaptsize    ((void (*)(BT829Ptr, int, int))LoaderSymbol("bt829_setcaptsize"))
#define xf86_bt829_SetBrightness  ((void (*)(BT829Ptr, int))LoaderSymbol("bt829_SetBrightness"))
#define xf86_bt829_SetContrast    ((void (*)(BT829Ptr, int))LoaderSymbol("bt829_SetContrast"))
#define xf86_bt829_SetSaturation  ((void (*)(BT829Ptr, int))LoaderSymbol("bt829_SetSaturation"))
#define xf86_bt829_SetTint        ((void (*)(BT829Ptr, int))LoaderSymbol("bt829_SetTint"))

#else

#define xf86_Detect_bt829               Detect_bt829
#define xf86_bt829_init                 bt829_init
#define xf86_bt829_setformat            bt829_setformat
#define xf86_bt829_setmux               bt829_setmux
#define xf86_bt829_setcaptsize          bt829_setcaptsize
#define xf86_bt829_SetBrightness        bt829_SetBrightness
#define xf86_bt829_SetContrast          bt829_SetContrast
#define xf86_bt829_SetSaturation        bt829_SetSaturation
#define xf86_bt829_SetTint              bt829_SetTint


#endif

#endif
