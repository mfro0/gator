/*     avview preliminary version

       (C) Vladimir Dergachev 2001-2003
       
       GNU Public License
       
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

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

typedef void *(*pthread_start_fn)(void *);

STRING_CACHE *v4l_sc=NULL;

int v4l_open_device(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
long i;
V4L_DATA *data;

Tcl_ResetResult(interp);

if(argc<3){
	Tcl_AppendResult(interp,"ERROR: v4l_open_device requires two arguments", NULL);
	return TCL_ERROR;
	}
i=add_string(v4l_sc, (char*)argv[1]);
if(v4l_sc->data[i]!=NULL){
	data=(V4L_DATA*) v4l_sc->data[i];
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
data->mode=0;
data->frame_count=0;
data->priv=NULL;
pthread_mutex_init(&data->streams_out_mutex, NULL);
data->streams_out_free=0;
data->streams_out_size=10;
data->streams_out=do_alloc(data->streams_out_size, sizeof(*(data->streams_out)));
data->use_count=1;
/* for now just require that the device supports YUV422 */
/* ++++++++++++++++ FIXME ++++++++++++++++++++++++++++++*/
if(ioctl(data->fd, VIDIOCGPICT, &(data->vpic))<0){
	Tcl_AppendResult(interp,"ERROR: v4l_capture_snapshot: error getting picture parameters", NULL);
	close(data->fd);
	free(data->streams_out);
	free(data);
	v4l_sc->data[i]=NULL;
	return TCL_ERROR;
	}
fprintf(stderr,"Current picture parameters: depth=%d palette=%d\n", 
	data->vpic.depth,
	data->vpic.palette);
/* try to switch to YUV422 */
data->vpic.palette=VIDEO_PALETTE_YUV422;
if(ioctl(data->fd, VIDIOCSPICT, &(data->vpic))>=0) return 0;
fprintf(stderr,"v4l device does not support YUV422\n");
#if 0
/* try to switch to YUYV */
data->vpic.palette=VIDEO_PALETTE_YUYV;
if(ioctl(data->fd, VIDIOCSPICT, &(data->vpic))>=0) return 0;
fprintf(stderr,"v4l device does not support YUYV\n");
/* try to switch to UYVY */
data->vpic.palette=VIDEO_PALETTE_UYVY;
if(ioctl(data->fd, VIDIOCSPICT, &(data->vpic))>=0) return 0;
fprintf(stderr,"v4l device does not support UYVY\n");
/* try to switch to YUV422P */
data->vpic.palette=VIDEO_PALETTE_YUV422P;
if(ioctl(data->fd, VIDIOCSPICT, &(data->vpic))>=0) return 0;
fprintf(stderr,"v4l device does not support YUV422P\n");
/* try to switch to YUV420 */
data->vpic.palette=VIDEO_PALETTE_YUV420;
if(ioctl(data->fd, VIDIOCSPICT, &(data->vpic))>=0) return 0;
fprintf(stderr,"v4l device does not support YUV420\n");
/* try to switch to YUV420P */
data->vpic.palette=VIDEO_PALETTE_YUV420P;
if(ioctl(data->fd, VIDIOCSPICT, &(data->vpic))>=0) return 0;
fprintf(stderr,"v4l device does not support YUV420P\n");
#endif
/* the device does not support any formats we understand.. ignore it*/
fprintf(stderr,"Device \"%s\" located at \"%s\" does not support any format AVview understands.\n", data->vcap.name, argv[2]);
close(data->fd);
free(data->streams_out);
free(data);
v4l_sc->data[i]=NULL;
Tcl_AppendResult(interp,"failed: v4l device cannot support YUV422", NULL);
return 0;
}

V4L_DATA *get_v4l_device_from_handle(char *handle)
{
long i;
i=lookup_string(v4l_sc, handle);
if(i<0)return NULL;
return (V4L_DATA *)v4l_sc->data[i];	
}

void v4l_reader_thread(V4L_DATA *data);

void v4l_attach_output_stream(V4L_DATA *data, PACKET_STREAM *s)
{
PACKET_STREAM **p;
pthread_mutex_lock(&data->streams_out_mutex);
if(data->streams_out_free>=data->streams_out_size){
	data->streams_out_size=2*data->streams_out_size+10;
	p=do_alloc(data->streams_out_size, sizeof(*p));
	if(data->streams_out_free>0)memcpy(p, data->streams_out, data->streams_out_free*sizeof(*p));
	free(data->streams_out);
	data->streams_out=p;
	}
data->streams_out[data->streams_out_free]=s;
data->streams_out_free++;
pthread_mutex_lock(&(s->ctr_mutex));
s->producer_thread_running++;
pthread_mutex_unlock(&(s->ctr_mutex));
if(data->streams_out_free==1){
	/* start reader thread */
	if(pthread_create(&(data->v4l_reader_thread), NULL, (pthread_start_fn) v4l_reader_thread, data)!=0){	
		fprintf(stderr,"Error creating v4l reader thread:");
		perror("");
		}
	}
pthread_mutex_unlock(&data->streams_out_mutex);
}

void v4l_detach_output_stream(V4L_DATA *data, PACKET_STREAM *s)
{
int i;
pthread_mutex_lock(&data->streams_out_mutex);
for(i=0;i<data->streams_out_free;i++)
	if(data->streams_out[i]==s)break;
if(i<(data->streams_out_free-1)){
	memmove(&(data->streams_out[i]), &(data->streams_out[i+1]), data->streams_out_free-i-1);
	}
if(i==data->streams_out_free){
	fprintf(stderr,"V4L: Cannot detach packet stream - not attached\n");
	}
pthread_mutex_lock(&(s->ctr_mutex));
s->producer_thread_running--;
pthread_mutex_unlock(&(s->ctr_mutex));
data->streams_out_free--;
pthread_mutex_unlock(&data->streams_out_mutex);
}

int v4l_close_device(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
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

int v4l_device_type(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
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

int v4l_device_name(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
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
	PACKET_STREAM *stm;
	char *read_buffer;
	long transfer_size;
	long transfer_read;
	Tcl_Interp *interp;
	char *transfer_complete_script;
	char *transfer_failed_script;
	Tk_PhotoHandle ph;
	Tk_PhotoImageBlock pib;
	Tcl_TimerToken timer;
	} V4L_SNAPSHOT_DATA;

V4L_SNAPSHOT_DATA *snapshot_data=NULL;

void v4l_snapshot_timer_callback(ClientData clientData)
{
V4L_DATA *data = (V4L_DATA *) clientData;
char *p=NULL;
PACKET *f1,*f2;
if((snapshot_data==NULL)||(snapshot_data->type!=V4L_SNAPSHOT_KEY)){
	fprintf(stderr,"INTERNAL ERROR: unset data->priv in v4l_snapshot_callback\n");
	return;
	}
if((snapshot_data->stm->total<snapshot_data->transfer_size)){
	snapshot_data->timer=Tcl_CreateTimerHandler(30, v4l_snapshot_timer_callback, data);
	return;
	}
v4l_detach_output_stream(data, snapshot_data->stm);
snapshot_data->pib.pixelPtr=do_alloc(snapshot_data->pib.pitch*snapshot_data->pib.height, 1);
pthread_mutex_lock(&(snapshot_data->stm->ctr_mutex));
f1=get_packet(snapshot_data->stm);
f2=get_packet(snapshot_data->stm);
switch(data->mode){
		case MODE_SINGLE_FRAME:
			data->frame_count++;
			memcpy(snapshot_data->read_buffer, f1->buf, data->video_size);
			break;
		case MODE_DOUBLE_INTERPOLATE:
			p=do_alloc(snapshot_data->pib.width*snapshot_data->pib.height*2, 1);
			deinterlace_422_double_interpolate(snapshot_data->pib.width, snapshot_data->pib.height/2, snapshot_data->pib.width*2,
					f1->buf, f2->buf,
					p);
			free(snapshot_data->read_buffer);
			snapshot_data->read_buffer=p;
			break;
		case MODE_DEINTERLACE_BOB:
			p=do_alloc(snapshot_data->pib.width*snapshot_data->pib.height*2, 1);
			deinterlace_422_bob(snapshot_data->pib.width, snapshot_data->pib.height/2, snapshot_data->pib.width*2,
					f1->buf, f2->buf,
					p);
			free(snapshot_data->read_buffer);
			snapshot_data->read_buffer=p;
			break;
		case MODE_DEINTERLACE_WEAVE:
			p=do_alloc(snapshot_data->pib.width*snapshot_data->pib.height*2, 1);
			deinterlace_422_weave(snapshot_data->pib.width, snapshot_data->pib.height/2, snapshot_data->pib.width*2,
					f1->buf, f2->buf,
					p);
			free(snapshot_data->read_buffer);
			snapshot_data->read_buffer=p;
			break;
		case MODE_DEINTERLACE_HALF_WIDTH:
			p=do_alloc(snapshot_data->pib.width*snapshot_data->pib.height*2, 1);
			deinterlace_422_half_width(snapshot_data->pib.width*2, snapshot_data->pib.height, snapshot_data->pib.width*4,
					f1->buf,p);
			data->frame_count++;
			free(snapshot_data->read_buffer);
			snapshot_data->read_buffer=p;
			break;
		}
if(f1!=NULL)f1->free_func(f1);
if(f2!=NULL)f2->free_func(f2);	
vcvt_422_rgb32(snapshot_data->pib.width, snapshot_data->pib.height, snapshot_data->pib.width, snapshot_data->read_buffer, snapshot_data->pib.pixelPtr);
set_transparency(&(snapshot_data->pib), 0xff);
Tk_PhotoSetSize(snapshot_data->ph, snapshot_data->pib.width, snapshot_data->pib.height);
Tk_PhotoPutBlock(snapshot_data->ph, &(snapshot_data->pib), 0, 0, snapshot_data->pib.width, snapshot_data->pib.height
#if (TK_MAJOR_VERSION==8) && (TK_MINOR_VERSION==4)
	, TK_PHOTO_COMPOSITE_SET
#endif
	);
if(snapshot_data->transfer_complete_script!=NULL)Tcl_Eval(snapshot_data->interp, snapshot_data->transfer_complete_script);
free(snapshot_data->pib.pixelPtr);
snapshot_data->pib.pixelPtr=NULL;

if(snapshot_data->read_buffer!=NULL){
	free(snapshot_data->read_buffer);
	snapshot_data->read_buffer=NULL;
	}
if(snapshot_data->transfer_complete_script!=NULL)free(snapshot_data->transfer_complete_script);
if(snapshot_data->transfer_failed_script!=NULL)free(snapshot_data->transfer_failed_script);
while((f1=get_packet(snapshot_data->stm))!=NULL)f1->free_func(f1);
snapshot_data->stm->consumer_thread_running=0;
pthread_mutex_unlock(&(snapshot_data->stm->ctr_mutex));
free(snapshot_data);
snapshot_data=NULL;
}

int v4l_capture_snapshot(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
long i;
V4L_DATA *data;
struct video_picture vpic;
struct timespec timeout;
PACKET *f1;

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
if(snapshot_data!=NULL){
	if(argc>=6)Tcl_Eval(interp, argv[5]);	
	return 0;
	}
if(data->priv!=NULL){
	} else {
	/* setup device for transfer */
	if(ioctl(data->fd, VIDIOCGWIN, &data->vwin)<0){
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
	data->video_size=data->vwin.width*data->vwin.height*2;
	}
snapshot_data=do_alloc(1, sizeof(V4L_SNAPSHOT_DATA));
snapshot_data->ph=Tk_FindPhoto(interp, argv[2]);
if(snapshot_data->ph==NULL){
	free(snapshot_data);
	Tcl_AppendResult(interp,"ERROR: v4l_capture_snapshot: no such photo image", NULL);
	return TCL_ERROR;
	}
snapshot_data->transfer_complete_script=strdup(argv[4]);
snapshot_data->type=V4L_SNAPSHOT_KEY;
if(argc>=6)snapshot_data->transfer_failed_script=strdup(argv[5]);
snapshot_data->interp=interp;
switch(data->mode){
	case MODE_SINGLE_FRAME:
		snapshot_data->pib.width=data->vwin.width;
		snapshot_data->pib.height=data->vwin.height;
		snapshot_data->transfer_size=data->video_size; 
		break;
	case MODE_DOUBLE_INTERPOLATE:
	case MODE_DEINTERLACE_BOB:
	case MODE_DEINTERLACE_WEAVE:
		snapshot_data->pib.width=data->vwin.width;
		snapshot_data->pib.height=data->vwin.height*2;
		snapshot_data->transfer_size=2*data->video_size; 
		break;
	case MODE_DEINTERLACE_HALF_WIDTH:
		snapshot_data->pib.width=data->vwin.width/2;
		snapshot_data->pib.height=data->vwin.height;
		snapshot_data->transfer_size=data->video_size; 
		break;
	}
snapshot_data->pib.offset[0]=0;
snapshot_data->pib.offset[1]=1;
snapshot_data->pib.offset[2]=2;
snapshot_data->pib.offset[3]=3;
snapshot_data->pib.pixelSize=4;
snapshot_data->pib.pitch=snapshot_data->pib.width*snapshot_data->pib.pixelSize;
snapshot_data->pib.pixelPtr=NULL;
snapshot_data->transfer_read=0;
snapshot_data->read_buffer=do_alloc(snapshot_data->transfer_size, 1);
snapshot_data->stm=new_packet_stream();
snapshot_data->stm->threshold=2*snapshot_data->transfer_size;
v4l_attach_output_stream(data, snapshot_data->stm);

/* wait for data to arrive */
timeout.tv_sec=time(NULL)+2;
timeout.tv_nsec=0;
pthread_mutex_lock(&(snapshot_data->stm->ctr_mutex));
pthread_cond_timedwait(&(snapshot_data->stm->suspend_consumer_thread), &(snapshot_data->stm->ctr_mutex), &timeout);
/* Do we have enough data ? */
if(snapshot_data->stm->total < snapshot_data->stm->threshold){
	/* timer expired, free structure and report failure */
	if(snapshot_data->read_buffer!=NULL){
		free(snapshot_data->read_buffer);
		snapshot_data->read_buffer=NULL;
		}
	if(snapshot_data->transfer_complete_script!=NULL)free(snapshot_data->transfer_complete_script);
	if(snapshot_data->transfer_failed_script!=NULL)free(snapshot_data->transfer_failed_script);
	while((f1=get_packet(snapshot_data->stm))!=NULL)f1->free_func(f1);
	snapshot_data->stm->consumer_thread_running=0;
	pthread_mutex_unlock(&(snapshot_data->stm->ctr_mutex));
	free(snapshot_data);
	snapshot_data=NULL;
	if(argc>=6)Tcl_Eval(interp, argv[5]);	
	return 0;
	}
pthread_mutex_unlock(&(snapshot_data->stm->ctr_mutex));
/* process arrived data */
v4l_snapshot_timer_callback(data);
return 0;
}

int v4l_get_current_window(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
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

int v4l_set_current_window(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
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

int get_deinterlacing_methods(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
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

void v4l_reader_thread(V4L_DATA *data)
{
PACKET *p;
fd_set read_fds;
PACKET_STREAM *stm;
int a;
long incoming_frames_count=0;
struct timeval tv;
int i;
/* lock mutex before testing s->stop_stream */
p=new_generic_packet(NULL, data->video_size);
pthread_mutex_lock(&(data->streams_out_mutex));
while(data->streams_out_free>0){
	pthread_mutex_unlock(&(data->streams_out_mutex));
	
	/* do the reading */
	if((a=read(data->fd, p->buf+p->free, p->size-p->free))>0){
		p->free+=a;
		if(p->free==p->size){ /* deliver packet */
			gettimeofday(&tv, NULL);
			p->timestamp=(int64)tv.tv_sec*1000000+(int64)tv.tv_usec;
			p->use_count=1;
			pthread_mutex_lock(&(data->streams_out_mutex));
			for(i=0;i<data->streams_out_free;i++){
				stm=data->streams_out[i];
				pthread_mutex_lock(&(stm->ctr_mutex));
				if(!(stm->stop_stream & STOP_PRODUCER_THREAD) &&
					(!data->step_frames || !(incoming_frames_count % data->step_frames))){
					p->use_count++;
					deliver_packet(stm, p);
					} 
				pthread_mutex_unlock(&(stm->ctr_mutex));
				}
			pthread_mutex_unlock(&(data->streams_out_mutex));
			p->free_func(p);
			p=new_generic_packet(NULL, data->video_size);
			incoming_frames_count++;
			}
		} else
	if(a<0){
		FD_ZERO(&read_fds);
		FD_SET(data->fd, &read_fds);
		a=select(data->fd+1, &read_fds, NULL, NULL, NULL);
		#if 1
		fprintf(stderr,"a=%d\n", a);
		perror("");
		#endif
		} 
	pthread_mutex_lock(&(data->streams_out_mutex));
	}
data->priv=NULL;
pthread_mutex_unlock(&(data->streams_out_mutex));
fprintf(stderr,"v4l_reader_thread finished\n");
pthread_exit(NULL);
}

typedef struct {
	char *window;
	PACKET_STREAM *s;
	int filedes[2];  /* we cannot call Tcl interpreter from another thread directly.. */
	} MONITOR_INFO;
	
MONITOR_INFO *minfo;

void monitor_consumer(PACKET_STREAM *s)
{
char a=1;
MONITOR_INFO *m;
PACKET *p;
m=(MONITOR_INFO *)s->priv;
while(1){
	fprintf(stderr,"X");
	write(m->filedes[1], &a, 1); /* signal TCL to do something about this */
	fprintf(stderr,"Y");
	pthread_mutex_lock(&(s->ctr_mutex));
	pthread_cond_wait(&(s->suspend_consumer_thread), &(s->ctr_mutex));
	if(s->stop_stream & STOP_CONSUMER_THREAD){
		pthread_mutex_unlock(&(s->ctr_mutex));
		pthread_exit(NULL);
		}
	/* get rid of older packets */
	while(packet_count(s)>3){
		p=get_packet(s);
		if((p!=NULL) && (p->free_func!=NULL))p->free_func(p);
		}
	
	pthread_mutex_unlock(&(s->ctr_mutex));
	fprintf(stderr,"Z");
	}
}

void monitor_pipe_handler(ClientData clientData, int mask)
{
MONITOR_INFO *minfo=(MONITOR_INFO *) clientData;
char a;
fprintf(stderr,"Q");
read(minfo->filedes[0], &a, 1);
}

int start_monitor(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
V4L_DATA *data;
Tcl_ResetResult(interp);
if(argc<3){
	Tcl_AppendResult(interp, "start_monitor requires at least two arguments\n", NULL);
	return TCL_ERROR;
	}
data=get_v4l_device_from_handle((char*)argv[1]);
if(data==NULL){
	Tcl_AppendResult(interp, "start_monitor: v4l device not open\n", NULL);
	return TCL_OK;
	}
if(minfo!=NULL){
	Tcl_AppendResult(interp, "start_monitor: busy\n", NULL);
	return TCL_OK;
	}
minfo=do_alloc(1, sizeof(*minfo));
minfo->window=strdup(argv[2]);
fprintf(stderr,"Monitoring %s\n", minfo->window);
if(pipe(minfo->filedes)<0){
	free(minfo->window);
	free(minfo);
	minfo=NULL;
	return TCL_OK;
	}
minfo->s=new_packet_stream();
minfo->s->priv=minfo;
minfo->s->consume_func=monitor_consumer;
minfo->s->threshold=2*data->video_size;
Tcl_CreateFileHandler(minfo->filedes[0],TCL_READABLE,monitor_pipe_handler, minfo);
v4l_attach_output_stream(data, minfo->s);
return TCL_OK;
}

int stop_monitor(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
return TCL_OK;
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
	{"start_monitor", start_monitor},
	{"stop_monitor", stop_monitor},
	{NULL, NULL}
	};

void init_v4l(Tcl_Interp *interp)
{
long i;

v4l_sc=new_string_cache();
snapshot_data=NULL;
minfo=NULL;

for(i=0;v4l_commands[i].name!=NULL;i++)
	Tcl_CreateCommand(interp, v4l_commands[i].name, v4l_commands[i].command, (ClientData)0, NULL);
}
