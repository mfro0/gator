
#include "xf86Module.h"

static MODULESETUPPROTO(saa7114Setup);


static XF86ModuleVersionInfo saa7114VersRec =
{
        "saa7114",
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
 
XF86ModuleData saa7114ModuleData = { &saa7114VersRec, saa7114Setup, NULL }; 

static pointer
saa7114Setup(pointer module, pointer opts, int *errmaj, int *errmin) {
   return (pointer)1;
}
