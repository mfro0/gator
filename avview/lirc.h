/*     lirc support for avview

       (C) Antti Ajanki 2004

       GNU Public License

*/

#ifndef __LIRC_H__
#define __LIRC_H__

#include <tcl.h>

ClientData init_lirc(Tcl_Interp * interp);
void deinit_lirc(ClientData data);

#endif
