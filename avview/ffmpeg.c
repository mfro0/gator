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
#include "formats.h"
#include "packet_stream.h"

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
	long step_frames;
	long frames_encoded;
	int fd_out;
	AVCodec *codec;
	AVCodecContext codec_context;
	PACKET_STREAM *s;
	pthread_t reader_thread;
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

f=calloc(1, sizeof(FFMPEG_INCOMING_FRAME));
if(f==NULL)return NULL;
f->next=NULL;
f->prev=NULL;
f->size=size;
f->free=0;
f->data=malloc(size);
if(f->data==NULL){
	free(f);
	return NULL;
	}
return f;
}

#if 0
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
#endif

void ffmpeg_preprocess_frame(V4L_DATA *data, FFMPEG_V4L_ENCODING_DATA *sdata, 
		PACKET *f, AVPicture *picture)
{
switch(data->mode){
	case MODE_SINGLE_FRAME:
		break;
	case MODE_DEINTERLACE_BOB:
	case MODE_DEINTERLACE_WEAVE:
		deinterlace_422_bob_to_420p(sdata->width, sdata->height, sdata->width*2, f->buf, picture->data[0]);
		break;
	case MODE_DEINTERLACE_HALF_WIDTH:
		deinterlace_422_half_width_to_420p(sdata->width, sdata->height, sdata->width*2, f->buf, picture->data[0]);
		break;
	}
}

void ffmpeg_v4l_encoding_thread(PACKET_STREAM *s)
{
int i;
int retval;
FFMPEG_V4L_ENCODING_DATA *sdata;
PACKET *f,*fn;
V4L_DATA *data;
AVPicture picture;
char *output_buf;
long ob_size, ob_free, ob_written;
long incoming_frames_count=0;

data=(V4L_DATA *)s->priv;
if((data==NULL)||((sdata=(FFMPEG_V4L_ENCODING_DATA *)data->priv)==NULL)||(sdata->type!=FFMPEG_V4L_CAPTURE_KEY)||(sdata->s==NULL))pthread_exit(NULL);
s=sdata->s;
ob_size=1000000;
ob_free=0;
output_buf=do_alloc(ob_size, sizeof(char));
picture.data[0]=do_alloc((3*sdata->codec_context.width*sdata->codec_context.height)/2, sizeof(char));
picture.data[1]=picture.data[0]+sdata->codec_context.width*sdata->codec_context.height;
picture.data[2]=picture.data[1]+(sdata->codec_context.width*sdata->codec_context.height)/4;
picture.linesize[0]=sdata->codec_context.width;
picture.linesize[1]=sdata->codec_context.width/2;
picture.linesize[2]=sdata->codec_context.width/2;
while(1){
	/* wait for data to arrive */
/*	sleep(1); */
	f=s->first;
	fprintf(stderr,"total=%d  stop=%d\n", s->total, s->stop_stream);
	while((f!=NULL)&&(f->next!=NULL)){
		if(!sdata->step_frames || !(incoming_frames_count % sdata->step_frames)){
			/* encode frame */
			ffmpeg_preprocess_frame(data, sdata, f, &picture);
			ob_free=avcodec_encode_video(&(sdata->codec_context), output_buf, ob_size, &picture);
			/* write out data */
			ob_written=0;
			while(ob_written<ob_free){
				i=write(sdata->fd_out, output_buf+ob_written, ob_free-ob_written);
				if(i>=0)ob_written+=i;
				}
			}
		incoming_frames_count++;
		/* get next one */
		f->discard=1;
		discard_packets(s);
		f=s->first;
		}
#if 0
	fprintf(stderr,"frame_count=%d  stop=%d\n", sdata->frame_count, sdata->stop);
#endif
	if(s->stop_stream){
		avcodec_close(&(sdata->codec_context));
		close(sdata->fd_out);
		data->priv=NULL;
		for(f=s->first;f!=NULL;f=f->next)f->discard=1;
		discard_packets(s);
		free(s);
		free(sdata);
		fprintf(stderr,"Recording finished\n");
		pthread_exit(NULL);
		}
	}
pthread_exit(NULL);
}

#if 0

void ffmpeg_transfer_handler(V4L_DATA *data, int mask)
{
long status;
FFMPEG_V4L_ENCODING_DATA *sdata;
FFMPEG_INCOMING_FRAME *f;

if(!(mask & TCL_READABLE))return;

sdata=(FFMPEG_V4L_ENCODING_DATA *)data->priv;
if((sdata==NULL)||(sdata->type!=FFMPEG_V4L_CAPTURE_KEY)){
	fprintf(stderr,"INTERNAL ERROR: incorrect data->priv in ffmpeg_transfer_callback\n");
	Tcl_DeleteFileHandler(data->fd);
	return;
	}
f=sdata->first;
if(f->free==f->size){
	/* frame complete */
	if(sdata->unused==NULL){
		f=ffmpeg_allocate_frame(sdata->size);
		while(f==NULL){
			Tcl_DoOneEvent(TCL_WINDOW_EVENTS|TCL_TIMER_EVENTS|TCL_IDLE_EVENTS|TCL_DONT_WAIT);	
			f=ffmpeg_allocate_frame(sdata->size);	
			}
		sdata->frame_count++;
		} else {
		f=sdata->unused;
		sdata->unused=NULL;
		}
	f->next=sdata->first;
	sdata->first->prev=f;
	sdata->first=f;
	/* wake up encoding thread */
	pthread_mutex_unlock(&(sdata->incoming_wait));
	pthread_mutex_lock(&(sdata->incoming_wait)); 
	#if 0
	fprintf(stderr,"fifo size %ld\n", sdata->size*sdata->frame_count);
	#endif
	Tcl_DoOneEvent(TCL_WINDOW_EVENTS|TCL_TIMER_EVENTS|TCL_IDLE_EVENTS|TCL_DONT_WAIT);
	}
status=read(data->fd, f->data+f->free, f->size-f->free);
if(status<0)return;
f->free+=status;
}

#endif

void v4l_reader_thread(V4L_DATA *data)
{
FFMPEG_V4L_ENCODING_DATA *sdata=(FFMPEG_V4L_ENCODING_DATA *)data->priv;
PACKET *p;
PACKET_STREAM *s=sdata->s;
fd_set read_fds;
int a;
/* lock mutex before testing s->stop_stream */
p=new_generic_packet(sdata->size);
pthread_mutex_lock(&(s->ctr_mutex));
while(!s->stop_stream){
	pthread_mutex_unlock(&(s->ctr_mutex));
	
	/* do the reading */
	if((a=read(data->fd, p->buf+p->free, p->size-p->free))>0){
		p->free+=a;
		if(p->free==p->size){ /* deliver packet */
			deliver_packet(s, p);
			p=new_generic_packet(sdata->size);
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
pthread_mutex_unlock(&(s->ctr_mutex));
pthread_exit(NULL);
}

int ffmpeg_encode_v4l_stream(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
V4L_DATA *data;
pthread_t thread;
FFMPEG_V4L_ENCODING_DATA *sdata;
struct video_picture vpic;
struct video_window vwin;

Tcl_ResetResult(interp);

if(argc<6){
	Tcl_AppendResult(interp,"ERROR: ffmpeg_encode_v4l_stream requires five arguments", NULL);
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
data->mode=MODE_SINGLE_FRAME;
if(!strcmp("deinterlace-bob", argv[3])){
	data->mode=MODE_DEINTERLACE_BOB;
	} else
if(!strcmp("deinterlace-weave", argv[3])){
	data->mode=MODE_DEINTERLACE_WEAVE;
	} else
if(!strcmp("half-width", argv[3])){
	data->mode=MODE_DEINTERLACE_HALF_WIDTH;
	}

sdata=do_alloc(1, sizeof(FFMPEG_V4L_ENCODING_DATA));
sdata->type=FFMPEG_V4L_CAPTURE_KEY;
sdata->stop=0;
sdata->width=vwin.width;
sdata->height=vwin.height;
sdata->size=vwin.width*vwin.height*2;
sdata->frame_count=1;
sdata->frames_encoded=0;
sdata->step_frames=0;
if(!strcmp("one half", argv[4]))sdata->step_frames=2;
	else
if(!strcmp("one quarter", argv[4]))sdata->step_frames=4;
sdata->fd_out=open("test3.mpg", O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
if(sdata->fd_out<0){
	free(sdata);
	Tcl_AppendResult(interp, "failed: ", NULL);
	Tcl_AppendResult(interp, strerror(errno), NULL);
	return 0;
	}
if(!strcmp("MPEG-1", argv[2]))
	sdata->codec=avcodec_find_encoder(CODEC_ID_MPEG1VIDEO);
	else
if(!strcmp("MPEG-4", argv[2]))
	sdata->codec=avcodec_find_encoder(CODEC_ID_MPEG4);
	else
if(!strcmp("MJPEG", argv[2]))
	sdata->codec=avcodec_find_encoder(CODEC_ID_MJPEG);
	else
if(!strcmp("MSMPEG-4", argv[2]))
	sdata->codec=avcodec_find_encoder(CODEC_ID_MSMPEG4);
	else
if(!strcmp("H263", argv[2]))
	sdata->codec=avcodec_find_encoder(CODEC_ID_H263);
	else
if(!strcmp("RV10", argv[2]))
	sdata->codec=avcodec_find_encoder(CODEC_ID_RV10);
	else
if(!strcmp("H263P", argv[2]))
	sdata->codec=avcodec_find_encoder(CODEC_ID_H263P);
	else
if(!strcmp("H263I", argv[2]))
	sdata->codec=avcodec_find_encoder(CODEC_ID_H263I);
	else
	sdata->codec=NULL;
if(sdata->codec==NULL){
	close(sdata->fd_out);
	free(sdata);
	Tcl_AppendResult(interp,"ERROR: ffmpeg_encode_v4l_stream: codec not supported", NULL);
	return TCL_ERROR;
	}
memset(&(sdata->codec_context), 0, sizeof(AVCodecContext));
switch(data->mode){
	case MODE_SINGLE_FRAME:
		sdata->codec_context.width=vwin.width;
		sdata->codec_context.height=vwin.height;
		break;
	case MODE_DEINTERLACE_BOB:
	case MODE_DEINTERLACE_WEAVE:
		sdata->codec_context.width=vwin.width;
		sdata->codec_context.height=vwin.height*2;
		break;
	case MODE_DEINTERLACE_HALF_WIDTH:
		sdata->codec_context.width=vwin.width/2;
		sdata->codec_context.height=vwin.height;
		break;
	}
sdata->codec_context.frame_rate=60*FRAME_RATE_BASE;
if(sdata->step_frames>0)sdata->codec_context.frame_rate=sdata->codec_context.frame_rate/sdata->step_frames;
sdata->codec_context.bit_rate=400000;
sdata->codec_context.pix_fmt=PIX_FMT_YUV422;
if(avcodec_open(&(sdata->codec_context), sdata->codec)<0){
	close(sdata->fd_out);
	free(sdata);
	Tcl_AppendResult(interp,"ERROR: ffmpeg_encode_v4l_stream: codec not supported", NULL);
	return TCL_ERROR;
	}
data->priv=sdata;
sdata->s=new_packet_stream();
sdata->s->priv=data;
sdata->s->consume_func=ffmpeg_v4l_encoding_thread;
/* set threshhold to two frames worth of data */
sdata->s->threshhold=sdata->size*2;
pthread_create(&(sdata->reader_thread), NULL, v4l_reader_thread, data);
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
pthread_mutex_lock(&(sdata->s->ctr_mutex));
sdata->s->stop_stream=1;
pthread_mutex_unlock(&(sdata->s->ctr_mutex));
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
/* Report 0 bytes when no such device exists or it is not capturing */

if(argc<2){
	Tcl_AppendResult(interp,"ERROR: ffmpeg_incoming_fifo_size requires one argument", NULL);
	return TCL_ERROR;
	}
data=get_v4l_device_from_handle(argv[1]);
if(data==NULL){
	Tcl_SetObjResult(interp, Tcl_NewIntObj(0));
	return 0;
	}
sdata=(FFMPEG_V4L_ENCODING_DATA *)data->priv;
if((sdata==NULL)||(sdata->type!=FFMPEG_V4L_CAPTURE_KEY)){
	Tcl_SetObjResult(interp, Tcl_NewIntObj(0));
	return 0;
	}

Tcl_SetObjResult(interp, Tcl_NewIntObj(sdata->s->total));
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
