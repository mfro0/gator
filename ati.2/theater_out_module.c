
#include "xf86Module.h"

static MODULESETUPPROTO(theaterOutSetup);

static 
XF86ModuleVersionInfo 
theaterVersRec =
{
  "theater_out",	/* modname */
  MODULEVENDORSTRING,	/* vendor */
  MODINFOSTRING1,	/* _modinfo1_ */
  MODINFOSTRING2,	/* _modinfo2_ */
  XF86_VERSION_CURRENT,	/* xf86version */
  1,			/* majorversion */
  0,			/* minorversion */
  0,			/* patchlevel */
  ABI_CLASS_VIDEODRV,	/* abiclass */
  ABI_VIDEODRV_VERSION,	/* abiversion */
  MOD_CLASS_NONE,	/* moduleclass */
  { 0 , 0 , 0 , 0 }	/* checksum */
};
 
XF86ModuleData 
theater_outModuleData = 
  { 
    &theaterVersRec, 
    theaterOutSetup, 
    NULL 
  }; 

static
pointer
theaterOutSetup(
		pointer module, 
		pointer opts, 
		int *errmaj, 
		int *errmin
		)
{
  return (pointer)1;
}
