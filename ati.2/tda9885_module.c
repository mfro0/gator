
#include "xf86Module.h"

static MODULESETUPPROTO(tda9885Setup);


static XF86ModuleVersionInfo tda9885VersRec =
{
        "tda9885",
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
 
XF86ModuleData tda9885ModuleData = { &tda9885VersRec, tda9885Setup, NULL }; 

static pointer
tda9885Setup(pointer module, pointer opts, int *errmaj, int *errmin) {
   return (pointer)1;
}
