/*     avview preliminary version

       (C) Vladimir Dergachev 2001
       
       GNU Public License
       
*/


#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <tcl.h>
#include <tk.h>
#include <linux/videodev.h>

#include "global.h"

#if USE_FFMPEG

#include <pthread.h>
#include "avcodec.h"
#include "v4l.h"

int ffmpeg_present(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tcl_ResetResult(interp);
Tcl_AppendResult(interp,"yes", NULL);
return 0;
}

void *ffmpeg_v4l_encoding_thread(V4L_DATA *data)
{
int i;
int retval;
for(i=0;i<100;i++){
	fprintf(stderr,"i=%d\n",i);
	}
pthread_exit(NULL);
}

int ffmpeg_encode_v4l_stream(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
V4L_DATA *data;
pthread_t thread;

Tcl_ResetResult(interp);

if(argc<4){
	Tcl_AppendResult(interp,"ERROR: ffmpeg_encode_v4l_stream requires three arguments", NULL);
	return TCL_ERROR;
	}
data=get_v4l_device_from_handle(argv[1]);
if(data==NULL){
	Tcl_AppendResult(interp,"ERROR: ffmpeg_encode_v4l_stream: no such v4l device", NULL);
	return TCL_ERROR;
	}
if(pthread_create(&thread, NULL, ffmpeg_v4l_encoding_thread, data)<0){
	Tcl_AppendResult(interp,"ERROR: ffmpeg_encode_v4l_stream: error creating encoding thread, ", NULL);
	Tcl_AppendResult(interp, strerror(errno), NULL);
	return TCL_ERROR;
	}
return 0;
}


struct {
	char *name;
	Tcl_CmdProc *command;
	} ffmpeg_commands[]={
	{"ffmpeg_present", ffmpeg_present},
	{"ffmpeg_encode_v4l_stream", ffmpeg_encode_v4l_stream},
	{NULL, NULL}
	};

#else /* NO FFMPEG present */

int ffmpeg_present(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tcl_ResetResult(interp);
Tcl_AppendResult(interp,"no", NULL);
return 0;
}

struct {
	char *name;
	Tcl_CmdProc *command;
	} ffmpeg_commands[]={
	{"ffmpeg_present", ffmpeg_present},
	{NULL, NULL}
	};

#endif /* USE_FFMPEG */

void init_ffmpeg(Tcl_Interp *interp)
{
long i;

#if USE_FFMPEG
avcodec_init();
avcodec_register_all();
#endif

for(i=0;ffmpeg_commands[i].name!=NULL;i++)
	Tcl_CreateCommand(interp, ffmpeg_commands[i].name, ffmpeg_commands[i].command, (ClientData)0, NULL);
}
