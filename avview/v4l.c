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

#include "string_cache.h"
#include "global.h"
#include "formats.h"

STRING_CACHE *v4l_sc=NULL;

#define MODE_SINGLE_FRAME	1
#define MODE_DEINTERLACE_BOB	2
#define MODE_DEINTERLACE_WEAVE	3

typedef struct S_V4L_DATA{
	int fd;
	struct video_capability vcap;
	char *read_buffer;
	long transfer_size;
	long transfer_read;
	void (*transfer_callback)(struct S_V4L_DATA *);
	Tcl_Interp *interp;
	char *transfer_complete_script;
	char *transfer_failed_script;
	Tk_PhotoHandle ph;
	Tk_PhotoImageBlock pib;
	int mode;
	int frame_count; /* to keep track of odd/even fields */
	} V4L_DATA;


int v4l_open_device(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
long i;
V4L_DATA *data;

Tcl_ResetResult(interp);

if(argc<3){
	Tcl_AppendResult(interp,"ERROR: v4l_open_device requires two arguments", NULL);
	return TCL_ERROR;
	}
i=add_string(v4l_sc, argv[1]);
if(v4l_sc->data[i]!=NULL){
	data=(V4L_DATA*) v4l_sc->data[i];
	Tcl_DeleteFileHandler(data->fd);
	if(data->transfer_size>0)data->transfer_callback(data);
	close(data->fd);
	} else {
	v4l_sc->data[i]=do_alloc(1, sizeof(V4L_DATA));
	}
data=(V4L_DATA*) v4l_sc->data[i];
data->fd=open(argv[2], O_RDONLY);
if(data->fd<0){
	free(data);
	v4l_sc->data[i]=NULL;
	Tcl_AppendResult(interp,"failed: ", NULL);
	Tcl_AppendResult(interp,strerror(errno), NULL);
	return TCL_OK;
	}
if(ioctl(data->fd, VIDIOCGCAP, &(data->vcap))<0){
	close(data->fd);
	free(data);
	v4l_sc->data[i]=NULL;
	Tcl_AppendResult(interp,"failed: not a v4l device", NULL);
	}
data->read_buffer=NULL;
data->transfer_size=0;
data->transfer_read=0;
data->transfer_callback=NULL;
data->interp=interp;
data->transfer_complete_script=NULL;
data->transfer_failed_script=NULL;
data->mode=0;
data->frame_count=0;
return 0;
}

int v4l_close_device(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
long i;
V4L_DATA *data;

Tcl_ResetResult(interp);

if(argc<2){
	Tcl_AppendResult(interp,"ERROR: v4l_close_device requires one argument", NULL);
	return TCL_ERROR;
	}

/* produce no errors if device has never been opened */

i=lookup_string(v4l_sc, argv[1]);
if(i<0){
	return 0;
	}
data=(V4L_DATA *)v4l_sc->data[i];	
if(data==NULL){
	return 0;
	}
close(data->fd);
free(data);
v4l_sc->data[i]=NULL;
return 0;
}

int v4l_device_type(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
long i;
V4L_DATA *data;
Tcl_Obj *ans;

Tcl_ResetResult(interp);

if(argc<2){
	Tcl_AppendResult(interp,"ERROR: v4l_device_type requires one argument", NULL);
	return TCL_ERROR;
	}
i=lookup_string(v4l_sc, argv[1]);
if(i<0){
	Tcl_AppendResult(interp,"ERROR: v4l_device_type: no such v4l handle", NULL);
	return TCL_ERROR;
	}
data=(V4L_DATA *)v4l_sc->data[i];	
if(data==NULL){
	Tcl_AppendResult(interp,"ERROR: v4l_device_type: no such v4l handle", NULL);
	return TCL_ERROR;
	}

ans=Tcl_NewListObj(0, NULL);

if(data->vcap.type & VID_TYPE_CAPTURE)Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("capture", -1));
if(data->vcap.type & VID_TYPE_TUNER)Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("tuner", -1));
if(data->vcap.type & VID_TYPE_TELETEXT)Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("teletext", -1));
if(data->vcap.type & VID_TYPE_OVERLAY)Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("overlay", -1));
if(data->vcap.type & VID_TYPE_CHROMAKEY)Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("chromakey", -1));
if(data->vcap.type & VID_TYPE_CLIPPING)Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("clipping", -1));
if(data->vcap.type & VID_TYPE_FRAMERAM)Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("frameram", -1));
if(data->vcap.type & VID_TYPE_SCALES)Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("scales", -1));
if(data->vcap.type & VID_TYPE_MONOCHROME)Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("monochrome", -1));
if(data->vcap.type & VID_TYPE_SUBCAPTURE)Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("subcapture", -1));
if(data->vcap.type & VID_TYPE_MPEG_DECODER)Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("mpeg_decoder", -1));
if(data->vcap.type & VID_TYPE_MPEG_ENCODER)Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("mpeg_encoder", -1));
if(data->vcap.type & VID_TYPE_MJPEG_DECODER)Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("mjpeg_decoder", -1));
if(data->vcap.type & VID_TYPE_MJPEG_ENCODER)Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("mjpeg_encoder", -1));

Tcl_SetObjResult(interp, ans);
return 0;
}

int v4l_device_name(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
long i;
V4L_DATA *data;
Tcl_Obj *ans;

Tcl_ResetResult(interp);

if(argc<2){
	Tcl_AppendResult(interp,"ERROR: v4l_device_name requires one argument", NULL);
	return TCL_ERROR;
	}
i=lookup_string(v4l_sc, argv[1]);
if(i<0){
	Tcl_AppendResult(interp,"ERROR: v4l_device_name: no such v4l handle", NULL);
	return TCL_ERROR;
	}
data=(V4L_DATA *)v4l_sc->data[i];	
if(data==NULL){
	Tcl_AppendResult(interp,"ERROR: v4l_device_name: no such v4l handle", NULL);
	return TCL_ERROR;
	}

Tcl_AppendResult(interp,data->vcap.name, NULL);
return 0;
}

void set_transparency(Tk_PhotoImageBlock *pib, char alpha)
{
char *p;
long i,j;

for(j=0;j<pib->height;j++){
	p=(pib->pixelPtr+j*pib->pitch)+3;
	for(i=0;i<pib->width;i++){
		*p=alpha;
		p+=4;
		}
	}
}

void v4l_transfer_handler(V4L_DATA *data, int mask)
{
long status;
if(mask & TCL_READABLE){
	status=read(data->fd, data->read_buffer+data->transfer_read, data->transfer_size-data->transfer_read);
	if(status<0)return;
	data->transfer_read+=status;
	if((status==0) || (data->transfer_read==data->transfer_size)){
		Tcl_DeleteFileHandler(data->fd);
		data->transfer_callback(data);
		}
	}
}

void v4l_snapshot_callback(V4L_DATA *data)
{
char *p=NULL;
if(data->transfer_size==data->transfer_read){
	data->pib.pixelPtr=do_alloc(data->pib.pitch*data->pib.height, 1);
	switch(data->mode){
		case MODE_SINGLE_FRAME:
			data->frame_count++;
			break;
		case MODE_DEINTERLACE_BOB:
			p=do_alloc(data->pib.width*data->pib.height*2, 1);
			if(data->frame_count & 1){
				deinterlace_422_bob(data->pib.width, data->pib.height/2, data->pib.width*2,
					data->read_buffer+(data->transfer_size/3), data->read_buffer+2*(data->transfer_size/3),
					p);
				data->frame_count+=3;
				} else {
				deinterlace_422_bob(data->pib.width, data->pib.height/2, data->pib.width*2,
					data->read_buffer, data->read_buffer+(data->transfer_size/2),
					p);
				data->frame_count+=2;
				}
			free(data->read_buffer);
			data->read_buffer=p;
			break;
		case MODE_DEINTERLACE_WEAVE:
			p=do_alloc(data->pib.width*data->pib.height*2, 1);
			if(data->frame_count & 1){
				deinterlace_422_weave(data->pib.width, data->pib.height/2, data->pib.width*2,
					data->read_buffer+(data->transfer_size/3), data->read_buffer+2*(data->transfer_size/3),
					p);
				data->frame_count+=3;
				} else {
				deinterlace_422_weave(data->pib.width, data->pib.height/2, data->pib.width*2,
					data->read_buffer, data->read_buffer+(data->transfer_size/2),
					p);
				data->frame_count+=2;
				}
			free(data->read_buffer);
			data->read_buffer=p;
			break;
		}
	
	vcvt_422_rgb32(data->pib.width, data->pib.height, data->pib.width, data->read_buffer, data->pib.pixelPtr);
	set_transparency(&(data->pib), 0xff);
	Tk_PhotoSetSize(data->ph, data->pib.width, data->pib.height);
	Tk_PhotoPutBlock(data->ph, &(data->pib), 0, 0, data->pib.width, data->pib.height);
	if(data->transfer_complete_script!=NULL)Tcl_Eval(data->interp, data->transfer_complete_script);
	free(data->pib.pixelPtr);
	data->pib.pixelPtr=NULL;
	} else {
	if(data->transfer_failed_script!=NULL)Tcl_Eval(data->interp, data->transfer_failed_script);
	}
if(data->read_buffer!=NULL){
	free(data->read_buffer);
	data->read_buffer=NULL;
	}
if(data->transfer_complete_script!=NULL)free(data->transfer_complete_script);
if(data->transfer_failed_script!=NULL)free(data->transfer_failed_script);
data->transfer_complete_script=NULL;
data->transfer_failed_script=NULL;
data->transfer_size=0;
data->transfer_read=0;
}

int v4l_capture_snapshot(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
long i;
V4L_DATA *data;
Tcl_Obj *ans;
struct video_picture vpic;
struct video_window vwin;

Tcl_ResetResult(interp);

if(argc<5){
	Tcl_AppendResult(interp,"ERROR: v4l_capture_snapshot requires four or five arguments", NULL);
	return TCL_ERROR;
	}
i=lookup_string(v4l_sc, argv[1]);
if(i<0){
	Tcl_AppendResult(interp,"ERROR: v4l_capture_snapshot: no such v4l handle", NULL);
	return TCL_ERROR;
	}
data=(V4L_DATA *)v4l_sc->data[i];	
if(data==NULL){
	Tcl_AppendResult(interp,"ERROR: v4l_capture_snapshot: no such v4l handle", NULL);
	return TCL_ERROR;
	}
data->ph=Tk_FindPhoto(interp, argv[2]);
if(data->ph==NULL){
	Tcl_AppendResult(interp,"ERROR: v4l_capture_snapshot: no such photo image", NULL);
	return TCL_ERROR;
	}
if(ioctl(data->fd, VIDIOCGWIN, &vwin)<0){
	Tcl_AppendResult(interp,"ERROR: v4l_capture_snapshot: error getting window parameters", NULL);
	return TCL_ERROR;
	}
if(ioctl(data->fd, VIDIOCGPICT, &vpic)<0){
	Tcl_AppendResult(interp,"ERROR: v4l_capture_snapshot: error getting picture parameters", NULL);
	return TCL_ERROR;
	}
vpic.palette=VIDEO_PALETTE_YUV422;
if(ioctl(data->fd, VIDIOCSPICT, &vpic)<0){
	if(argc>=6)Tcl_Eval(data->interp, argv[5]);	
	return 0;
	}
data->mode=MODE_SINGLE_FRAME;
if(!strcmp("deinterlace-bob", argv[3])){
	data->mode=MODE_DEINTERLACE_BOB;
	}
if(!strcmp("deinterlace-weave", argv[3])){
	data->mode=MODE_DEINTERLACE_WEAVE;
	}
data->transfer_complete_script=strdup(argv[4]);
if(argc>=6)data->transfer_failed_script=strdup(argv[5]);
data->transfer_callback=v4l_snapshot_callback;
data->pib.width=vwin.width;
switch(data->mode){
	case MODE_SINGLE_FRAME:
		data->pib.height=vwin.height;
		data->transfer_size=2*vwin.width*vwin.height; 
		break;
	case MODE_DEINTERLACE_BOB:
	case MODE_DEINTERLACE_WEAVE:
		data->pib.height=vwin.height*2;
		if(data->frame_count & 1)
			data->transfer_size=3*2*vwin.width*vwin.height; 
			else
			data->transfer_size=2*2*vwin.width*vwin.height; 
		break;
	}
data->pib.offset[0]=0;
data->pib.offset[1]=1;
data->pib.offset[2]=2;
data->pib.offset[3]=3;
data->pib.pixelSize=4;
data->pib.pitch=data->pib.width*data->pib.pixelSize;
data->pib.pixelPtr=NULL;
data->transfer_read=0;
data->read_buffer=do_alloc(data->transfer_size, 1);
Tcl_CreateFileHandler(data->fd, TCL_READABLE, v4l_transfer_handler, data);
return 0;
}

int v4l_get_current_window(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
long i;
V4L_DATA *data;
Tcl_Obj *ans;
struct video_window vwin;

Tcl_ResetResult(interp);

if(argc<2){
	Tcl_AppendResult(interp,"ERROR: v4l_get_current_window requires one argument", NULL);
	return TCL_ERROR;
	}
i=lookup_string(v4l_sc, argv[1]);
if(i<0){
	Tcl_AppendResult(interp,"ERROR: v4l_get_current_window: no such v4l handle", NULL);
	return TCL_ERROR;
	}
data=(V4L_DATA *)v4l_sc->data[i];	
if(data==NULL){
	Tcl_AppendResult(interp,"ERROR: v4l_get_current_window: no such v4l handle", NULL);
	return TCL_ERROR;
	}
if(ioctl(data->fd, VIDIOCGWIN, &vwin)<0){
	Tcl_AppendResult(interp,"ERROR: v4l_get_current_window: error getting window parameters", NULL);
	return TCL_ERROR;
	}

ans=Tcl_NewListObj(0, NULL);

Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(vwin.x));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(vwin.y));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(vwin.width));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(vwin.height));

Tcl_SetObjResult(interp, ans);
return 0;
}

int v4l_set_current_window(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
long i;
V4L_DATA *data;
Tcl_Obj *ans;
struct video_window vwin;

Tcl_ResetResult(interp);

if(argc<6){
	Tcl_AppendResult(interp,"ERROR: v4l_set_current_window requires five arguments", NULL);
	return TCL_ERROR;
	}
i=lookup_string(v4l_sc, argv[1]);
if(i<0){
	Tcl_AppendResult(interp,"ERROR: v4l_set_current_window: no such v4l handle", NULL);
	return TCL_ERROR;
	}
data=(V4L_DATA *)v4l_sc->data[i];	
if(data==NULL){
	Tcl_AppendResult(interp,"ERROR: v4l_set_current_window: no such v4l handle", NULL);
	return TCL_ERROR;
	}
if(ioctl(data->fd, VIDIOCGWIN, &vwin)<0){
	Tcl_AppendResult(interp,"ERROR: v4l_set_current_window: error getting window parameters", NULL);
	return TCL_ERROR;
	}
vwin.x=atoi(argv[2]);
vwin.y=atoi(argv[3]);
vwin.width=atoi(argv[4]);
vwin.height=atoi(argv[5]);
if(ioctl(data->fd, VIDIOCSWIN, &vwin)<0){
	Tcl_AppendResult(interp,"ERROR: v4l_set_current_window: error setting window parameters", NULL);
	return TCL_ERROR;
	}

return 0;
}


struct {
	char *name;
	Tcl_CmdProc *command;
	} v4l_commands[]={
	{"v4l_open_device", v4l_open_device},
	{"v4l_close_device", v4l_close_device},
	{"v4l_device_type", v4l_device_type},
	{"v4l_device_name", v4l_device_name},
	{"v4l_capture_snapshot", v4l_capture_snapshot},
	{"v4l_get_current_window", v4l_get_current_window},
	{"v4l_set_current_window", v4l_set_current_window},
	{NULL, NULL}
	};

void init_v4l(Tcl_Interp *interp)
{
long i;

v4l_sc=new_string_cache();

for(i=0;v4l_commands[i].name!=NULL;i++)
	Tcl_CreateCommand(interp, v4l_commands[i].name, v4l_commands[i].command, (ClientData)0, NULL);
}
