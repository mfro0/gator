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

typedef struct S_FFMPEG_INCOMING_FRAME{
	struct S_FFMPEG_INCOMING_FRAME *next;
	struct S_FFMPEG_INCOMING_FRAME *prev;
	char *data;
	long size;
	long free;
	} FFMPEG_INCOMING_FRAME;

typedef struct {
	long type;
	long width;
	long height;
	long stop;
	long size;
	long frame_count;
	int fd_out;
	AVCodec *codec;
	AVCodecContext codec_context;
	pthread_mutex_t  incoming_wait;
	FFMPEG_INCOMING_FRAME *first;
	FFMPEG_INCOMING_FRAME *last;
	} FFMPEG_V4L_ENCODING_DATA;

int ffmpeg_present(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tcl_ResetResult(interp);
Tcl_AppendResult(interp,"yes", NULL);
return 0;
}

FFMPEG_INCOMING_FRAME *ffmpeg_allocate_frame(long size)
{
FFMPEG_INCOMING_FRAME *f;

f=do_alloc(1, sizeof(FFMPEG_INCOMING_FRAME));
f->next=NULL;
f->prev=NULL;
f->size=size;
f->free=0;
f->data=do_alloc(size, 1);
return f;
}

void free_frame_list(FFMPEG_INCOMING_FRAME *f1)
{
FFMPEG_INCOMING_FRAME *f;
f=f1;
if(f==NULL)return;
while(f->prev!=NULL)f=f->prev;
while(f->next!=NULL){
	free(f->data);
	f=f->next;
	free(f->prev);
	f->prev=NULL;
	}
free(f->data);
free(f);
}

void *ffmpeg_v4l_encoding_thread(V4L_DATA *data)
{
int i;
int retval;
FFMPEG_V4L_ENCODING_DATA *sdata;
FFMPEG_INCOMING_FRAME *f;
AVPicture picture;

sdata=(FFMPEG_V4L_ENCODING_DATA *)data->priv;
if((sdata==NULL)||(sdata->type!=FFMPEG_V4L_CAPTURE_KEY))pthread_exit(NULL);
while(1){
	/* wait for data to arrive */
	sleep(1);
	f=sdata->last;
	while((f!=NULL)&&(f->free==f->size)&&(f->prev!=NULL)){
		free(f->data);
		f=f->prev;
		free(sdata->last);
		sdata->last=f;
		f->next=NULL;
		sdata->frame_count--;
		}
	fprintf(stderr,"frame_count=%d  stop=%d\n", sdata->frame_count, sdata->stop);
	if(sdata->stop){
		avcodec_close(&(sdata->codec_context));
		close(sdata->fd_out);
		data->priv=NULL;
		free_frame_list(sdata->last);
		free(sdata);
		pthread_exit(NULL);
		}
	pthread_mutex_lock(&(sdata->incoming_wait));
	pthread_mutex_unlock(&(sdata->incoming_wait));
	}
pthread_exit(NULL);
}

void ffmpeg_transfer_handler(V4L_DATA *data, int mask)
{
long status;
FFMPEG_V4L_ENCODING_DATA *sdata;
FFMPEG_INCOMING_FRAME *f;

sdata=(FFMPEG_V4L_ENCODING_DATA *)data->priv;
if((sdata==NULL)||(sdata->type!=FFMPEG_V4L_CAPTURE_KEY)){
	fprintf(stderr,"INTERNAL ERROR: incorrect data->priv in ffmpeg_transfer_callback\n");
	Tcl_DeleteFileHandler(data->fd);
	return;
	}
f=sdata->first;
if(f->free<f->size){
	status=read(data->fd, f->data+f->free, f->size-f->free);
	if(status<0)return;
	f->free+=status;
	} else { /* frame complete */
	f=ffmpeg_allocate_frame(sdata->size);
	f->next=sdata->first;
	sdata->first->prev=f;
	sdata->first=f;
	sdata->frame_count++;
	/* wake up encoding thread */
	pthread_mutex_unlock(&(sdata->incoming_wait));
	pthread_mutex_lock(&(sdata->incoming_wait)); 
	fprintf(stderr,"fifo size %ld\n", sdata->size*sdata->frame_count);
	return;
	}
}

int ffmpeg_encode_v4l_stream(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
V4L_DATA *data;
pthread_t thread;
FFMPEG_V4L_ENCODING_DATA *sdata;
struct video_picture vpic;
struct video_window vwin;

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
if(data->priv!=NULL){
	Tcl_AppendResult(interp,"ERROR: ffmpeg_encode_v4l_stream: v4l device busy", NULL);
	return TCL_ERROR;
	}
if(ioctl(data->fd, VIDIOCGWIN, &vwin)<0){
	Tcl_AppendResult(interp,"ERROR: ffmpeg_encode_v4l_stream: error getting window parameters", NULL);
	return TCL_ERROR;
	}
if(ioctl(data->fd, VIDIOCGPICT, &vpic)<0){
	Tcl_AppendResult(interp,"ERROR: ffmpeg_encode_v4l_stream: error getting picture parameters", NULL);
	return TCL_ERROR;
	}
sdata=do_alloc(1, sizeof(FFMPEG_V4L_ENCODING_DATA));
sdata->type=FFMPEG_V4L_CAPTURE_KEY;
sdata->stop=0;
sdata->width=vwin.width;
sdata->height=vwin.height;
sdata->size=vwin.width*vwin.height*2;
sdata->last=sdata->first;
sdata->frame_count=1;
sdata->fd_out=open("test3.mpg", O_WRONLY|O_CREAT|O_TRUNC);
if(sdata->fd_out<0){
	free(sdata);
	Tcl_AppendResult(interp, "failed: ", NULL);
	Tcl_AppendResult(interp, strerror(errno), NULL);
	return 0;
	}
sdata->codec=avcodec_find_encoder(CODEC_ID_MPEG4);
if(sdata->codec==NULL){
	close(sdata->fd_out);
	free(sdata);
	Tcl_AppendResult(interp,"ERROR: ffmpeg_encode_v4l_stream: codec not supported", NULL);
	return TCL_ERROR;
	}
memset(&(sdata->codec_context), 0, sizeof(AVCodecContext));
sdata->codec_context.width=vwin.width;
sdata->codec_context.height=vwin.height;
sdata->codec_context.frame_rate=30*FRAME_RATE_BASE;
sdata->codec_context.bit_rate=400000;
if(avcodec_open(&(sdata->codec_context), sdata->codec)<0){
	close(sdata->fd_out);
	free(sdata);
	Tcl_AppendResult(interp,"ERROR: ffmpeg_encode_v4l_stream: codec not supported", NULL);
	return TCL_ERROR;
	}
data->priv=sdata;
sdata->first=ffmpeg_allocate_frame(sdata->size);
sdata->last=sdata->first;
pthread_mutex_init(&(sdata->incoming_wait), NULL);
pthread_mutex_lock(&(sdata->incoming_wait));
if(pthread_create(&thread, NULL, ffmpeg_v4l_encoding_thread, data)<0){
	free_frame_list(sdata->first);
	avcodec_close(&(sdata->codec_context));
	close(sdata->fd_out);
	free(sdata);
	Tcl_AppendResult(interp,"ERROR: ffmpeg_encode_v4l_stream: error creating encoding thread, ", NULL);
	Tcl_AppendResult(interp, strerror(errno), NULL);
	return TCL_ERROR;
	}
Tcl_CreateFileHandler(data->fd, TCL_READABLE, ffmpeg_transfer_handler, data);
return 0;
}

int ffmpeg_stop_encoding(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
V4L_DATA *data;
pthread_t thread;
FFMPEG_V4L_ENCODING_DATA *sdata;
struct video_picture vpic;
struct video_window vwin;

Tcl_ResetResult(interp);

/* no don raise errors if no capture is going on */

if(argc<2){
	Tcl_AppendResult(interp,"ERROR: ffmpeg_stop_encoding requires one argument", NULL);
	return TCL_ERROR;
	}
data=get_v4l_device_from_handle(argv[1]);
if(data==NULL){
	return 0;
	}
sdata=(FFMPEG_V4L_ENCODING_DATA *)data->priv;
if((sdata==NULL)||(sdata->type!=FFMPEG_V4L_CAPTURE_KEY)){
	return 0;
	}
Tcl_DeleteFileHandler(data->fd);
sdata->stop=1;
pthread_mutex_unlock(&(sdata->incoming_wait));
return 0;
}

int ffmpeg_incoming_fifo_size(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
V4L_DATA *data;
pthread_t thread;
FFMPEG_V4L_ENCODING_DATA *sdata;
struct video_picture vpic;
struct video_window vwin;

Tcl_ResetResult(interp);

if(argc<2){
	Tcl_AppendResult(interp,"ERROR: ffmpeg_incoming_fifo_size requires one argument", NULL);
	return TCL_ERROR;
	}
data=get_v4l_device_from_handle(argv[1]);
if(data==NULL){
	Tcl_AppendResult(interp,"ERROR: ffmpeg_incoming_fifo_size: no such v4l device", NULL);
	return TCL_ERROR;
	}
sdata=(FFMPEG_V4L_ENCODING_DATA *)data->priv;
if((sdata==NULL)||(sdata->type!=FFMPEG_V4L_CAPTURE_KEY)){
	Tcl_AppendResult(interp,"ERROR: ffmpeg_incoming_fifo_size: v4l device not capturing", NULL);
	return TCL_ERROR;
	}

Tcl_SetObjResult(interp, Tcl_NewIntObj(sdata->size*sdata->frame_count));
return 0;
}

struct {
	char *name;
	Tcl_CmdProc *command;
	} ffmpeg_commands[]={
	{"ffmpeg_present", ffmpeg_present},
	{"ffmpeg_encode_v4l_stream", ffmpeg_encode_v4l_stream},
	{"ffmpeg_stop_encoding", ffmpeg_stop_encoding},
	{"ffmpeg_incoming_fifo_size", ffmpeg_incoming_fifo_size},
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
