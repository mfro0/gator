/*     avview preliminary version

       (C) Vladimir Dergachev 2001
       
       GNU Public License
       
*/

#include <stdio.h>
#include <stdlib.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <tcl.h>
#include <tk.h>

#include "frequencies.h"
#include "xv.h"
#include "xmisc.h"
#include "v4l.h"
#include "ffmpeg.h"

void * do_alloc(long a, long b)
{
void *r;
if(a<=0)a=1;
if(b<=0)b=1;
r=calloc(a, b);
while(r==NULL){
	fprintf(stderr,"Could not allocate %ld chunks of %ld bytes (%ld bytes total)\n", a, b);
	sleep(1);
	r=calloc(a,b);
	}
return r;
}

int Tcl_AppInit(Tcl_Interp * interp)
{
int status=TCL_OK;

status=Tcl_Init(interp);
if(status!=TCL_OK)return TCL_ERROR;
status=Tk_Init(interp);
if(status!=TCL_OK)return TCL_ERROR; 
init_freq(interp);
init_xv(interp);
init_xmisc(interp);
init_v4l(interp);
init_ffmpeg(interp);
return TCL_OK;
}

int main(int argc, char *argv[])
{
Tk_Main(argc, argv, Tcl_AppInit);
return 0;
}
