
#include "xf86Module.h"

static MODULESETUPPROTO(tda9850Setup);


static XF86ModuleVersionInfo tda9850VersRec =
{
        "tda9850",
        MODULEVENDORSTRING,
        MODINFOSTRING1,
        MODINFOSTRING2,
        XF86_VERSION_CURRENT,
        1, 0, 0,
        ABI_CLASS_VIDEODRV,             /* This needs the video driver ABI */
        ABI_VIDEODRV_VERSION,
        MOD_CLASS_NONE,
        {0,0,0,0}
};
 
XF86ModuleData tda9850ModuleData = { &tda9850VersRec, tda9850Setup, NULL }; 

static pointer
tda9850Setup(pointer module, pointer opts, int *errmaj, int *errmin) {
   return (pointer)1;
}
