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

#include "avcodec.h"

int ffmpeg_present(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tcl_ResetResult(interp);
Tcl_AppendResult(interp,"yes", NULL);
return 0;
}

struct {
	char *name;
	Tcl_CmdProc *command;
	} ffmpeg_commands[]={
	{"ffmpeg_present", ffmpeg_present},
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
