/*     avview preliminary version

       (C) Vladimir Dergachev 2001-2002
       
       GNU Public License
       
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <tcl.h>
#include <tk.h>

#include "frequencies.h"
#include "xv.h"
#include "xmisc.h"
#include "v4l.h"
#include "ffmpeg.h"
#include "linux.h"
#include "alsa.h"
#include <pthread.h>

pthread_mutex_t  memory_mutex;

void * do_alloc(long a, long b)
{
void *r;
pthread_mutex_lock(&memory_mutex);
if(a<=0)a=1;
if(b<=0)b=1;
r=calloc(a, b);
while(r==NULL){
	fprintf(stderr,"Could not allocate %ld chunks of %ld bytes (%ld bytes total)\n", a, b, a*b);
	sleep(1);
	r=calloc(a,b);
	}
pthread_mutex_unlock(&memory_mutex);
return r;
}

void do_free(void *a)
{
pthread_mutex_lock(&memory_mutex);
free(a);
pthread_mutex_unlock(&memory_mutex);
}

char *get_value(int argc, char *argv[], char *key)
{
int i;
for(i=0;i<argc;i++){
	if(!strcmp(key, argv[i])){
		if((i+1)<argc)return argv[i+1];
		}
	}
return NULL;
}

#ifndef AVVIEW_VERSION
#error "please regenerate ./configure file and run it again"
#endif

int avview_version(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tcl_ResetResult(interp);
Tcl_AppendResult(interp, AVVIEW_VERSION, NULL);
return TCL_OK;
}

int Tcl_AppInit(Tcl_Interp * interp)
{
int status=TCL_OK;

status=Tcl_Init(interp);
if(status!=TCL_OK)return TCL_ERROR;
status=Tk_Init(interp);
if(status!=TCL_OK)return TCL_ERROR; 
Tcl_CreateCommand(interp, "avview_version", avview_version, (ClientData)0, NULL);
init_freq(interp);
init_xv(interp);
init_xmisc(interp);
init_v4l(interp);
init_ffmpeg(interp);
init_alsa(interp);
init_linux(interp);
return TCL_OK;
}

int main(int argc, char *argv[])
{
int major,minor,patchlevel,type;
/* check version first */
Tcl_GetVersion(&major, &minor, &patchlevel, &type);
if((major!=TCL_MAJOR_VERSION)||(minor!=TCL_MINOR_VERSION)){
	fprintf(stderr,"ERROR: detected mismatched between library and header version.\n");
	fprintf(stderr,"ERROR: this is most likely caused by installation of two Tcl/Tk versions\n");
	fprintf(stderr,"ERROR: and configure mistake. Report to gatos-devel@lists.sf.net\n");
	exit(-1);
	}

pthread_mutex_init(&memory_mutex, NULL);
Tk_Main(argc, argv, Tcl_AppInit);
return 0;
}
