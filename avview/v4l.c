/*     avview preliminary version

       (C) Vladimir Dergachev 2001
       
       GNU Public License
       
*/

#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <tcl.h>
#include <tk.h>

#include "string_cache.h"
#include "global.h"
#include "formats.h"
#include "v4l.h"

STRING_CACHE *v4l_sc=NULL;

int v4l_open_device(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
long i;
V4L_DATA *data;
struct video_picture vpic;

Tcl_ResetResult(interp);

if(argc<3){
	Tcl_AppendResult(interp,"ERROR: v4l_open_device requires two arguments", NULL);
	return TCL_ERROR;
	}
i=add_string(v4l_sc, argv[1]);
if(v4l_sc->data[i]!=NULL){
	data=(V4L_DATA*) v4l_sc->data[i];
	Tcl_DeleteFileHandler(data->fd);
	if(data->priv!=NULL)data->transfer_callback(data);
	close(data->fd);
	} else {
	v4l_sc->data[i]=do_alloc(1, sizeof(V4L_DATA));
	}
data=(V4L_DATA*) v4l_sc->data[i];
data->fd=open(argv[2], O_RDONLY);
if((data->fd<0)){
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
/* for now just require that the device supports YUV422 */
/* ++++++++++++++++ FIXME ++++++++++++++++++++++++++++++*/
if(ioctl(data->fd, VIDIOCGPICT, &vpic)<0){
	Tcl_AppendResult(interp,"ERROR: v4l_capture_snapshot: error getting picture parameters", NULL);
	return TCL_ERROR;
	}
vpic.palette=VIDEO_PALETTE_YUV422;
if(ioctl(data->fd, VIDIOCSPICT, &vpic)<0){
	close(data->fd);
	free(data);
	v4l_sc->data[i]=NULL;
	Tcl_AppendResult(interp,"failed: v4l device cannot support YUV422", NULL);
	return 0;
	}
data->mode=0;
data->frame_count=0;
data->priv=NULL;
return 0;
}

V4L_DATA *get_v4l_device_from_handle(char *handle)
{
long i;
i=lookup_string(v4l_sc, handle);
if(i<0)return NULL;
return (V4L_DATA *)v4l_sc->data[i];	
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

typedef struct {
	long type;
	char *read_buffer;
	long transfer_size;
	long transfer_read;
	Tcl_Interp *interp;
	char *transfer_complete_script;
	char *transfer_failed_script;
	Tk_PhotoHandle ph;
	Tk_PhotoImageBlock pib;
	} V4L_SNAPSHOT_DATA;

void v4l_transfer_handler(V4L_DATA *data, int mask)
{
long status;
V4L_SNAPSHOT_DATA *sdata;
sdata=(V4L_SNAPSHOT_DATA *)data->priv;
if((sdata==NULL)||(sdata->type!=V4L_SNAPSHOT_KEY)){
	fprintf(stderr,"INTERNAL ERROR: incorrect data->priv in v4l_transfer_callback\n");
	Tcl_DeleteFileHandler(data->fd);
	return;
	}
if(mask & TCL_READABLE){
	status=read(data->fd, sdata->read_buffer+sdata->transfer_read, sdata->transfer_size-sdata->transfer_read);
	if(status<0)return;
	sdata->transfer_read+=status;
	if((status==0) || (sdata->transfer_read==sdata->transfer_size)){
		Tcl_DeleteFileHandler(data->fd);
		data->transfer_callback(data);
		}
	}
}

void v4l_snapshot_callback(V4L_DATA *data)
{
char *p=NULL;
V4L_SNAPSHOT_DATA *sdata;
sdata=(V4L_SNAPSHOT_DATA *)data->priv;
if((sdata==NULL)||(sdata->type!=V4L_SNAPSHOT_KEY)){
	fprintf(stderr,"INTERNAL ERROR: unset data->priv in v4l_snapshot_callback\n");
	return;
	}
if(sdata->transfer_size==sdata->transfer_read){
	sdata->pib.pixelPtr=do_alloc(sdata->pib.pitch*sdata->pib.height, 1);
	switch(data->mode){
		case MODE_SINGLE_FRAME:
			data->frame_count++;
			break;
		case MODE_DOUBLE_INTERPOLATE:
			p=do_alloc(sdata->pib.width*sdata->pib.height*2, 1);
			if(data->frame_count & 1){
				deinterlace_422_double_interpolate(sdata->pib.width, sdata->pib.height/2, sdata->pib.width*2,
					sdata->read_buffer+(sdata->transfer_size/3), sdata->read_buffer+2*(sdata->transfer_size/3),
					p);
				data->frame_count+=3;
				} else {
				deinterlace_422_double_interpolate(sdata->pib.width, sdata->pib.height/2, sdata->pib.width*2,
					sdata->read_buffer, sdata->read_buffer+(sdata->transfer_size/2),
					p);
				data->frame_count+=2;
				}
			free(sdata->read_buffer);
			sdata->read_buffer=p;
			break;
		case MODE_DEINTERLACE_BOB:
			p=do_alloc(sdata->pib.width*sdata->pib.height*2, 1);
			if(data->frame_count & 1){
				deinterlace_422_bob(sdata->pib.width, sdata->pib.height/2, sdata->pib.width*2,
					sdata->read_buffer+(sdata->transfer_size/3), sdata->read_buffer+2*(sdata->transfer_size/3),
					p);
				data->frame_count+=3;
				} else {
				deinterlace_422_bob(sdata->pib.width, sdata->pib.height/2, sdata->pib.width*2,
					sdata->read_buffer, sdata->read_buffer+(sdata->transfer_size/2),
					p);
				data->frame_count+=2;
				}
			free(sdata->read_buffer);
			sdata->read_buffer=p;
			break;
		case MODE_DEINTERLACE_WEAVE:
			p=do_alloc(sdata->pib.width*sdata->pib.height*2, 1);
			if(data->frame_count & 1){
				deinterlace_422_weave(sdata->pib.width, sdata->pib.height/2, sdata->pib.width*2,
					sdata->read_buffer+(sdata->transfer_size/3), sdata->read_buffer+2*(sdata->transfer_size/3),
					p);
				data->frame_count+=3;
				} else {
				deinterlace_422_weave(sdata->pib.width, sdata->pib.height/2, sdata->pib.width*2,
					sdata->read_buffer, sdata->read_buffer+(sdata->transfer_size/2),
					p);
				data->frame_count+=2;
				}
			free(sdata->read_buffer);
			sdata->read_buffer=p;
			break;
		case MODE_DEINTERLACE_HALF_WIDTH:
			p=do_alloc(sdata->pib.width*sdata->pib.height*2, 1);
			deinterlace_422_half_width(sdata->pib.width*2, sdata->pib.height, sdata->pib.width*4,
					sdata->read_buffer,p);
			data->frame_count++;
			free(sdata->read_buffer);
			sdata->read_buffer=p;
			break;
		}
	
	vcvt_422_rgb32(sdata->pib.width, sdata->pib.height, sdata->pib.width, sdata->read_buffer, sdata->pib.pixelPtr);
	set_transparency(&(sdata->pib), 0xff);
	Tk_PhotoSetSize(sdata->ph, sdata->pib.width, sdata->pib.height);
	Tk_PhotoPutBlock(sdata->ph, &(sdata->pib), 0, 0, sdata->pib.width, sdata->pib.height);
	if(sdata->transfer_complete_script!=NULL)Tcl_Eval(sdata->interp, sdata->transfer_complete_script);
	free(sdata->pib.pixelPtr);
	sdata->pib.pixelPtr=NULL;
	} else {
	if(sdata->transfer_failed_script!=NULL)Tcl_Eval(sdata->interp, sdata->transfer_failed_script);
	}
if(sdata->read_buffer!=NULL){
	free(sdata->read_buffer);
	sdata->read_buffer=NULL;
	}
if(sdata->transfer_complete_script!=NULL)free(sdata->transfer_complete_script);
if(sdata->transfer_failed_script!=NULL)free(sdata->transfer_failed_script);
free(sdata);
data->priv=NULL;
data->transfer_callback=NULL;
}

int v4l_capture_snapshot(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
long i;
V4L_DATA *data;
struct video_picture vpic;
struct video_window vwin;
V4L_SNAPSHOT_DATA *sdata;

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
if(data->priv!=NULL){
	if(argc>=6)Tcl_Eval(interp, argv[5]);	
	return 0;
	} else {
	/* setup device for transfer */
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
		if(argc>=6)Tcl_Eval(interp, argv[5]);	
		return 0;
		}
	data->mode=MODE_SINGLE_FRAME;
	if(!strcmp("deinterlace-bob", argv[3])){
		data->mode=MODE_DEINTERLACE_BOB;
		} else
	if(!strcmp("deinterlace-weave", argv[3])){
		data->mode=MODE_DEINTERLACE_WEAVE;
		} else
	if(!strcmp("double-interpolate", argv[3])){
		data->mode=MODE_DOUBLE_INTERPOLATE;
		} else
	if(!strcmp("half-width", argv[3])){
		data->mode=MODE_DEINTERLACE_HALF_WIDTH;
		}
	}
sdata=do_alloc(1, sizeof(V4L_SNAPSHOT_DATA));
sdata->ph=Tk_FindPhoto(interp, argv[2]);
if(sdata->ph==NULL){
	free(sdata);
	Tcl_AppendResult(interp,"ERROR: v4l_capture_snapshot: no such photo image", NULL);
	return TCL_ERROR;
	}
data->transfer_callback=v4l_snapshot_callback;
data->priv=sdata;
sdata->transfer_complete_script=strdup(argv[4]);
sdata->type=V4L_SNAPSHOT_KEY;
if(argc>=6)sdata->transfer_failed_script=strdup(argv[5]);
sdata->interp=interp;
switch(data->mode){
	case MODE_SINGLE_FRAME:
		sdata->pib.width=vwin.width;
		sdata->pib.height=vwin.height;
		sdata->transfer_size=2*vwin.width*vwin.height; 
		break;
	case MODE_DOUBLE_INTERPOLATE:
	case MODE_DEINTERLACE_BOB:
	case MODE_DEINTERLACE_WEAVE:
		sdata->pib.width=vwin.width;
		sdata->pib.height=vwin.height*2;
		if(data->frame_count & 1)
			sdata->transfer_size=3*2*vwin.width*vwin.height; 
			else
			sdata->transfer_size=2*2*vwin.width*vwin.height; 
		break;
	case MODE_DEINTERLACE_HALF_WIDTH:
		sdata->pib.width=vwin.width/2;
		sdata->pib.height=vwin.height;
		sdata->transfer_size=2*vwin.width*vwin.height; 
		break;
	}
sdata->pib.offset[0]=0;
sdata->pib.offset[1]=1;
sdata->pib.offset[2]=2;
sdata->pib.offset[3]=3;
sdata->pib.pixelSize=4;
sdata->pib.pitch=sdata->pib.width*sdata->pib.pixelSize;
sdata->pib.pixelPtr=NULL;
sdata->transfer_read=0;
sdata->read_buffer=do_alloc(sdata->transfer_size, 1);
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

int get_deinterlacing_methods(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tcl_Obj *ans;
Tcl_ResetResult(interp);

ans=Tcl_NewListObj(0, NULL);
Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("single-frame", -1));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("deinterlace-bob", -1));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("deinterlace-weave", -1));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("half-width", -1));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("double-interpolate", -1));
Tcl_SetObjResult(interp, ans);
return TCL_OK;
}

void v4l_reader_thread(PACKET_STREAM *s)
{
PACKET *p;
V4L_DATA *data=s->priv;
fd_set read_fds;
int a;
long incoming_frames_count=0;
struct timeval tv;
/* lock mutex before testing s->stop_stream */
p=new_generic_packet(s, data->video_size);
pthread_mutex_lock(&(s->ctr_mutex));
while(!(s->stop_stream & STOP_PRODUCER_THREAD)){
	pthread_mutex_unlock(&(s->ctr_mutex));
	
	/* do the reading */
	if((a=read(data->fd, p->buf+p->free, p->size-p->free))>0){
		p->free+=a;
		if(p->free==p->size){ /* deliver packet */
			gettimeofday(&tv, NULL);
			p->timestamp=(((int64)tv.tv_sec)<<20)|(tv.tv_usec);
			pthread_mutex_lock(&(s->ctr_mutex));
			if(!(s->stop_stream & STOP_PRODUCER_THREAD) &&
				(!data->step_frames || !(incoming_frames_count % data->step_frames))){
				deliver_packet(s, p);
				pthread_mutex_unlock(&(s->ctr_mutex));
				p=new_generic_packet(s, data->video_size);
				} else {
				p->free=0;
				pthread_mutex_unlock(&(s->ctr_mutex));
				}
			incoming_frames_count++;
			}
		} else
	if(a<0){
		FD_ZERO(&read_fds);
		FD_SET(data->fd, &read_fds);
		a=select(data->fd+1, &read_fds, NULL, NULL, NULL);
		#if 0
		fprintf(stderr,"a=%d\n", a);
		perror("");
		#endif
		} 
	pthread_mutex_lock(&(s->ctr_mutex));
	}
data->priv=NULL;
s->producer_thread_running=0;
pthread_mutex_unlock(&(s->ctr_mutex));
fprintf(stderr,"v4l_reader_thread finished\n");
pthread_exit(NULL);
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
	{"get_deinterlacing_methods", get_deinterlacing_methods},
	{NULL, NULL}
	};

void init_v4l(Tcl_Interp *interp)
{
long i;

v4l_sc=new_string_cache();

for(i=0;v4l_commands[i].name!=NULL;i++)
	Tcl_CreateCommand(interp, v4l_commands[i].name, v4l_commands[i].command, (ClientData)0, NULL);
}
