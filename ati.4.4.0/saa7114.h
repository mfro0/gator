#ifndef __SAA7114_H__
#define __SAA7114_H__


typedef struct {
	 I2CDevRec d;
	 
	 CARD8 id;
	 } SAA7114Rec, * SAA7114Ptr;

#define SAA7114_ADDR_1	 0x42
#define SAA7114_ADDR_2   0x40

SAA7114Ptr DetectSAA7114(I2CBusPtr b, I2CSlaveAddr addr);

/* DO NOT FORGET to setup constants before calling InitSAA7114 */
void InitSAA7114(SAA7114Ptr t);

void ShutdownSAA7114(SAA7114Ptr t);
void DumpSAA7114Regs(SAA7114Ptr t);


#define SAA7114SymbolsList  \
		"InitSAA7114" \
		"DetectSAA7114" \
		"DumpSAA7114Regs", \
		"ShutdownSAA7114"

#ifdef XFree86LOADER

#define xf86_DetectSAA7114         ((SAA7114Ptr (*)(I2CBusPtr, I2CSlaveAddr))LoaderSymbol("DetectSAA7114"))

#define xf86_InitSAA7114           ((void (*)(SAA7114Ptr t))LoaderSymbol("InitSAA7114"))

#define xf86_ShutdownSAA7114       ((void (*)(SAA7114Ptr))LoaderSymbol("ShutdownSAA7114"))
#define xf86_DumpSAA7114Regs       ((void (*)(SAA7114Ptr))LoaderSymbol("DumpSAA7114Regs"))
#else

#define xf86_DetectSAA7114             DetectSAA7114

#define xf86_InitSAA7114               InitSAA7114

#define xf86_ShutdownSAA7114           ShutdownSAA7114
#define xf86_DumpRageSAA7114Regs       DumpRageSAA7114Regs 

#endif		

#endif
