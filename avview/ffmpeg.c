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
#include <math.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <tcl.h>
#include <tk.h>
#include <linux/videodev.h>

#include "global.h"

#if USE_FFMPEG

#include <pthread.h>
#include "avcodec.h"
#include "avformat.h"
#include "v4l.h"
#include "formats.h"
#include "packet_stream.h"
#include "alsa.h"

typedef struct {
	long type;
	long width;
	long height;
	long stop;
	long video_size;
	long frame_count;
	long step_frames;
	long frames_encoded;
	int fd_out;
	AVCodec *video_codec;
	AVCodecContext video_codec_context;
	AVCodec *audio_codec;
	AVCodecContext audio_codec_context;
	AVFormatContext format_context;
	int audio_stream_num;
	int video_stream_num;
	pthread_mutex_t format_context_mutex;
	PACKET_STREAM *video_s;
	PACKET_STREAM *audio_s;
	pthread_t video_reader_thread;
	pthread_t audio_reader_thread;
	V4L_DATA *v4l_data;
	ALSA_PARAMETERS alsa_param;
	} FFMPEG_ENCODING_DATA;

/* this assumes only one encoding process goes on at a time.. not too unreasonable */

FFMPEG_ENCODING_DATA *sdata=NULL;

int ffmpeg_present(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tcl_ResetResult(interp);
Tcl_AppendResult(interp,"yes", NULL);
return 0;
}

void ffmpeg_preprocess_frame(V4L_DATA *data, FFMPEG_ENCODING_DATA *sdata, 
		PACKET *f, AVPicture *picture)
{
char *p;
switch(data->mode){
	case MODE_SINGLE_FRAME:
		convert_422_to_420p(sdata->width, sdata->height, sdata->width*2, f->buf, picture->data[0]);
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
PACKET *f,*fn;
V4L_DATA *data;
AVPicture picture;
char *output_buf;
long ob_size, ob_free, ob_written;
long incoming_frames_count=0;

data=(V4L_DATA *)s->priv;
if((data==NULL)||(sdata==NULL)||(sdata->type!=FFMPEG_CAPTURE_KEY)||(sdata->video_s==NULL))pthread_exit(NULL);
ob_size=1024*1024;
ob_free=0;
output_buf=do_alloc(ob_size, sizeof(char));
picture.data[0]=do_alloc((3*sdata->video_codec_context.width*sdata->video_codec_context.height)/2+4096, sizeof(char));
picture.data[1]=picture.data[0]+sdata->video_codec_context.width*sdata->video_codec_context.height;
picture.data[2]=picture.data[1]+(sdata->video_codec_context.width*sdata->video_codec_context.height)/4;
picture.linesize[0]=sdata->video_codec_context.width;
picture.linesize[1]=sdata->video_codec_context.width/2;
picture.linesize[2]=sdata->video_codec_context.width/2;
while(1){
	/* wait for data to arrive */
/*	sleep(1); */
	pthread_mutex_lock(&(s->ctr_mutex));
	f=s->first;
	pthread_mutex_unlock(&(s->ctr_mutex));
	while((f!=NULL)&&(f->next!=NULL)){
		if(!sdata->step_frames || !(incoming_frames_count % sdata->step_frames)){
			/* encode frame */
			ffmpeg_preprocess_frame(data, sdata, f, &picture);
			ob_free=avcodec_encode_video(&(sdata->video_codec_context), output_buf, ob_size, &picture);
			/* write out data */
			ob_written=0;
			while(ob_written<ob_free){
				pthread_mutex_lock(&(sdata->format_context_mutex));
				if(sdata->format_context.format!=NULL){
					sdata->format_context.format->write_packet(&(sdata->format_context),sdata->video_stream_num, output_buf+ob_written, ob_free-ob_written);
					i=ob_free-ob_written;
					} else {
					i=write(sdata->fd_out, output_buf+ob_written, ob_free-ob_written);
					}
				pthread_mutex_unlock(&(sdata->format_context_mutex));
				if(i>=0)ob_written+=i;
				}
			}
		incoming_frames_count++;
		/* get next one */
		f->discard=1;
		f=f->next;
		pthread_mutex_lock(&(s->ctr_mutex));
		discard_packets(s); 
		pthread_mutex_unlock(&(s->ctr_mutex));
		}
#if 0
	fprintf(stderr,"frame_count=%d  stop=%d\n", sdata->frame_count, sdata->stop);
#endif
	pthread_mutex_lock(&(s->ctr_mutex));
	if(s->stop_stream){
		avcodec_close(&(sdata->video_codec_context));
		for(f=s->first;f!=NULL;f=f->next)f->discard=1;
		discard_packets(s);
		free(picture.data[0]);
		s->consumer_thread_running=0;
		pthread_mutex_unlock(&(s->ctr_mutex));
		fprintf(stderr,"Video encoding finished\n");
		pthread_exit(NULL);
		}
	pthread_mutex_unlock(&(s->ctr_mutex));
	}
pthread_exit(NULL);
}

void ffmpeg_audio_encoding_thread(PACKET_STREAM *s)
{
unsigned char *out_buf;
long ob_size, ob_free, ob_written, i;
PACKET *f;
ob_size=sdata->alsa_param.chunk_size;
out_buf=do_alloc(ob_size, 1);
while(1){
	pthread_mutex_lock(&(s->ctr_mutex));
	f=s->first;
	if(!s->stop_stream &&((f==NULL)||(f->next==NULL))){
		/* no data to encode - terminate thread instead of spinning */
		do_free(out_buf);
		s->consumer_thread_running=0;
		pthread_mutex_unlock(&(s->ctr_mutex));
		pthread_exit(NULL);
		}
	pthread_mutex_unlock(&(s->ctr_mutex));
	while((f!=NULL)&&(f->next!=NULL)){
		ob_free=avcodec_encode_audio(&(sdata->audio_codec_context), out_buf, ob_size, f->buf);	
		if(ob_free>0){
			ob_written=0;
			while(ob_written<ob_free){
				pthread_mutex_lock(&(sdata->format_context_mutex));
				if(sdata->format_context.format!=NULL){
					sdata->format_context.format->write_packet(&(sdata->format_context),sdata->audio_stream_num, out_buf+ob_written, ob_free-ob_written);
					i=ob_free-ob_written;
					} else {
					i=write(sdata->fd_out, out_buf+ob_written, ob_free-ob_written);
					}
				pthread_mutex_unlock(&(sdata->format_context_mutex));
				if(i>=0)ob_written+=i;
				}
			}
		f->discard=1;
		f=f->next;
		pthread_mutex_lock(&(s->ctr_mutex));
		discard_packets(s); 
		pthread_mutex_unlock(&(s->ctr_mutex));
		}
	pthread_mutex_lock(&(s->ctr_mutex));	
	if(s->stop_stream){
		avcodec_close(&(sdata->audio_codec_context));
		for(f=s->first;f!=NULL;f=f->next)f->discard=1;
		discard_packets(s);
		s->consumer_thread_running=0;
		pthread_mutex_unlock(&(s->ctr_mutex));
		free(out_buf);
		fprintf(stderr,"Audio encoding finished\n");
		pthread_exit(NULL);
		}
	pthread_mutex_unlock(&(s->ctr_mutex));
	}
}

void v4l_reader_thread(V4L_DATA *data)
{
PACKET *p;
PACKET_STREAM *s=sdata->video_s;
fd_set read_fds;
int a;
/* lock mutex before testing s->stop_stream */
p=new_generic_packet(sdata->video_size);
pthread_mutex_lock(&(s->ctr_mutex));
while(!s->stop_stream){
	pthread_mutex_unlock(&(s->ctr_mutex));
	
	/* do the reading */
	if((a=read(data->fd, p->buf+p->free, p->size-p->free))>0){
		p->free+=a;
		if(p->free==p->size){ /* deliver packet */
			pthread_mutex_lock(&(s->ctr_mutex));
			if(!s->stop_stream){
				deliver_packet(s, p);
				p=new_generic_packet(sdata->video_size);
				}
			pthread_mutex_unlock(&(s->ctr_mutex));
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
sdata->v4l_data=NULL;
s->producer_thread_running=0;
pthread_mutex_unlock(&(s->ctr_mutex));
fprintf(stderr,"v4l_reader_thread finished\n");
pthread_exit(NULL);
}



static int file_write(FFMPEG_ENCODING_DATA *sdata, unsigned char *buf, int size)
{
int a;
int done;
done=0;
while(done<size){
	a=write(sdata->fd_out, buf+done, size-done);
	if(a>0)done+=a;
	}
return 1;
}

offset_t file_seek(FFMPEG_ENCODING_DATA *sdata, offset_t pos, int whence)
{
fprintf(stderr,"file_seek pos=%ld whence=%d\n", (int)pos, whence);
return lseek(sdata->fd_out, pos, whence);
}

int ffmpeg_create_video_codec(Tcl_Interp* interp, int argc, char * argv[])
{
V4L_DATA *data;
char *arg_v4l_handle;
char *arg_video_codec;
char *arg_v4l_mode;
char *arg_v4l_rate;
char *arg_video_bitrate;
char *arg_deinterlace_mode;
struct video_picture vpic;
struct video_window vwin;
double a,b;

arg_v4l_handle=get_value(argc, argv, "-v4l_handle");
arg_video_codec=get_value(argc, argv, "-video_codec");
arg_v4l_mode=get_value(argc, argv, "-v4l_mode");
arg_v4l_rate=get_value(argc, argv, "-v4l_rate");
arg_video_bitrate=get_value(argc, argv, "-video_bitrate");
arg_deinterlace_mode=get_value(argc, argv, "-deinterlace_mode");

if((arg_v4l_handle==NULL)||
	(!strcmp(arg_v4l_handle, "none"))||
	((data=get_v4l_device_from_handle(arg_v4l_handle))==NULL))return 0;

if(data->priv!=NULL){
	Tcl_AppendResult(interp,"ERROR: ffmpeg_encode_v4l_stream: v4l device busy", NULL);
	return 0;
	}
if(ioctl(data->fd, VIDIOCGWIN, &vwin)<0){
	Tcl_AppendResult(interp,"ERROR: ffmpeg_encode_v4l_stream: error getting window parameters", NULL);
	return 0;
	}
if(ioctl(data->fd, VIDIOCGPICT, &vpic)<0){
	Tcl_AppendResult(interp,"ERROR: ffmpeg_encode_v4l_stream: error getting picture parameters", NULL);
	return 0;
	}

data->mode=MODE_SINGLE_FRAME;
if(arg_deinterlace_mode==NULL){
	/* nothing */
	} else
if(!strcmp("deinterlace-bob", arg_deinterlace_mode)){
	data->mode=MODE_DEINTERLACE_BOB;
	} else
if(!strcmp("deinterlace-weave", arg_deinterlace_mode)){
	data->mode=MODE_DEINTERLACE_WEAVE;
	} else
if(!strcmp("half-width", arg_deinterlace_mode)){
	data->mode=MODE_DEINTERLACE_HALF_WIDTH;
	}

sdata->width=vwin.width;
sdata->height=vwin.height;
sdata->video_size=vwin.width*vwin.height*2;
sdata->frame_count=1;
sdata->frames_encoded=0;

if(arg_video_codec==NULL){
	sdata->video_codec=avcodec_find_encoder(CODEC_ID_MPEG4);
	} else
if(!strcmp("MPEG-1", arg_video_codec)){
	sdata->video_codec=avcodec_find_encoder(CODEC_ID_MPEG1VIDEO);
	} else
if(!strcmp("MPEG-4", arg_video_codec)){
	sdata->video_codec=avcodec_find_encoder(CODEC_ID_MPEG4);
	} else
if(!strcmp("MJPEG", arg_video_codec)){
	sdata->video_codec=avcodec_find_encoder(CODEC_ID_MJPEG);
	} else
if(!strcmp("MSMPEG-4", arg_video_codec)){
	sdata->video_codec=avcodec_find_encoder(CODEC_ID_MSMPEG4);
	} else
if(!strcmp("H263", arg_video_codec)){
	sdata->video_codec=avcodec_find_encoder(CODEC_ID_H263);
	} else
if(!strcmp("RV10", arg_video_codec)){
	sdata->video_codec=avcodec_find_encoder(CODEC_ID_RV10);
	} else
if(!strcmp("H263P", arg_video_codec)){
	sdata->video_codec=avcodec_find_encoder(CODEC_ID_H263P);
	} else
if(!strcmp("H263I", arg_video_codec)){
	sdata->video_codec=avcodec_find_encoder(CODEC_ID_H263I);
	} else 
	sdata->video_codec=NULL;
if(sdata->video_codec==NULL){
	return 0;
	}
memset(&(sdata->video_codec_context), 0, sizeof(AVCodecContext));
sdata->video_codec_context.codec_id=sdata->video_codec->id;
sdata->video_codec_context.codec_type=sdata->video_codec->type;

switch(data->mode){
	case MODE_SINGLE_FRAME:
		sdata->video_codec_context.width=vwin.width;
		sdata->video_codec_context.height=vwin.height;
		break;
	case MODE_DEINTERLACE_BOB:
	case MODE_DEINTERLACE_WEAVE:
		sdata->video_codec_context.width=vwin.width;
		sdata->video_codec_context.height=vwin.height*2;
		break;
	case MODE_DEINTERLACE_HALF_WIDTH:
		sdata->video_codec_context.width=vwin.width/2;
		sdata->video_codec_context.height=vwin.height;
		break;
	}
sdata->video_codec_context.frame_rate=60*FRAME_RATE_BASE;
if(sdata->step_frames>0)sdata->video_codec_context.frame_rate=sdata->video_codec_context.frame_rate/sdata->step_frames;
a=(((800000.0*vwin.width)*vwin.height)*sdata->video_codec_context.frame_rate);
b=(352.0*288.0*25.0*FRAME_RATE_BASE);
sdata->video_codec_context.bit_rate=rint(a/b);
if(arg_video_bitrate!=NULL)sdata->video_codec_context.bit_rate=atol(arg_video_bitrate);
fprintf(stderr,"Using bitrate=%d, frame_rate=%d a=%g b=%g\n", sdata->video_codec_context.bit_rate, sdata->video_codec_context.frame_rate,a,b);
sdata->video_codec_context.pix_fmt=PIX_FMT_YUV422;
sdata->video_codec_context.flags=CODEC_FLAG_QSCALE;
sdata->video_codec_context.quality=2;
if(avcodec_open(&(sdata->video_codec_context), sdata->video_codec)<0){
	return 0;
	}
data->priv=sdata;
sdata->v4l_data=data;
return 1;
}

int ffmpeg_create_audio_codec(Tcl_Interp* interp, int argc, char * argv[])
{
char *arg_audio_codec;
if(alsa_setup_reader_thread(sdata->audio_s, argc, argv, &(sdata->alsa_param))<0){
	return 0;
	}
arg_audio_codec=get_value(argc, argv, "-audio_codec");
sdata->audio_codec=avcodec_find_encoder(CODEC_ID_PCM_S16LE); 
if(arg_audio_codec==NULL){
	/* nothing */
	} else
if(!strcasecmp(arg_audio_codec,"MPEG-2")){
	sdata->audio_codec=avcodec_find_encoder(CODEC_ID_MP2); 
	} else 
if(!strcasecmp(arg_audio_codec,"AC-3")){
	sdata->audio_codec=avcodec_find_encoder(CODEC_ID_AC3); 
	}
if(sdata->audio_codec==NULL){
	fprintf(stderr,"Could not find audio codec\n");
	return 0;
	}
memset(&(sdata->audio_codec_context), 0, sizeof(AVCodecContext));
sdata->audio_codec_context.bit_rate=64000;
sdata->audio_codec_context.sample_rate=sdata->alsa_param.sample_rate;
sdata->audio_codec_context.channels=sdata->alsa_param.channels;
sdata->audio_codec_context.codec_id=sdata->audio_codec->id;
sdata->audio_codec_context.sample_fmt=SAMPLE_FMT_S16;
sdata->audio_codec_context.codec_type=sdata->audio_codec->type;
if(avcodec_open(&(sdata->audio_codec_context), sdata->audio_codec)<0){
	return 0;
	}
if(sdata->audio_codec_context.frame_size>1)
	sdata->alsa_param.chunk_size=sdata->audio_codec_context.frame_size*2*sdata->alsa_param.channels;
return 1;
}

int ffmpeg_encode_v4l_stream(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
int k;
char *arg_filename;
char *arg_step_frames;
char *arg_av_format;

Tcl_ResetResult(interp);

if(argc<5){
	Tcl_AppendResult(interp,"ERROR: ffmpeg_encode_v4l_stream requires at least four arguments", NULL);
	return TCL_ERROR;
	}

arg_filename=get_value(argc, argv, "-filename");
arg_step_frames=get_value(argc, argv, "-step_frames");
arg_av_format=get_value(argc, argv, "-av_format");

if(arg_filename==NULL){
	Tcl_AppendResult(interp,"ERROR: ffmpeg_encode_v4l_stream: no file to write data to specified", NULL);
	return TCL_ERROR;
	}

sdata=do_alloc(1, sizeof(FFMPEG_ENCODING_DATA));
sdata->type=FFMPEG_CAPTURE_KEY;
sdata->stop=0;
pthread_mutex_init(&(sdata->format_context_mutex), NULL);

sdata->step_frames=0;
if(arg_step_frames==NULL){
	/* nothing */
	} else
if(!strcmp("one half", arg_step_frames))sdata->step_frames=2;
	else
if(!strcmp("one quarter", arg_step_frames))sdata->step_frames=4;

sdata->fd_out=open(arg_filename, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
if(sdata->fd_out<0){
	free(sdata);
	Tcl_AppendResult(interp, "failed: ", NULL);
	Tcl_AppendResult(interp, strerror(errno), NULL);
	return 0;
	}
motion_estimation_method=ME_FULL;
memset(&(sdata->format_context), 0, sizeof(AVFormatContext));

sdata->format_context.nb_streams=0;
sdata->video_stream_num=-1;
sdata->audio_stream_num=-1;
if(ffmpeg_create_video_codec(interp, argc, argv)){
	k=sdata->format_context.nb_streams;
	sdata->video_stream_num=k;
	sdata->format_context.nb_streams++;
	sdata->format_context.streams[k]=do_alloc(1, sizeof(AVStream));
	memset(sdata->format_context.streams[k], 0, sizeof(AVStream));
	memcpy(&(sdata->format_context.streams[k]->codec), &(sdata->video_codec_context), sizeof(AVCodecContext));	
	}
sdata->audio_s=new_packet_stream();
if(ffmpeg_create_audio_codec(interp, argc, argv)){
	k=sdata->format_context.nb_streams;
	sdata->audio_stream_num=k;
	sdata->format_context.nb_streams++; 
	sdata->format_context.streams[k]=do_alloc(1, sizeof(AVStream));
	memset(sdata->format_context.streams[k], 0, sizeof(AVStream));
	memcpy(&(sdata->format_context.streams[k]->codec), &(sdata->audio_codec_context), sizeof(AVCodecContext));	
	}
fprintf(stderr,"video stream num=%d audio stream num=%d\n",
	sdata->video_stream_num, sdata->audio_stream_num);
if(sdata->format_context.nb_streams==0){
	close(sdata->fd_out);
	free(sdata);
	sdata=NULL;
	Tcl_AppendResult(interp,"ERROR: ffmpeg_encode_v4l_stream: no streams to encode", NULL);
	return TCL_ERROR;
	}
sdata->format_context.format=NULL;
if(arg_av_format==NULL){
	/* nothing */
	} else
if(!strcasecmp("avi", arg_av_format)){
	sdata->format_context.format=&avi_format;
	} else
if(!strcasecmp("asf", arg_av_format)){
	sdata->format_context.format=&asf_format;
	} else 
if(!strcasecmp("mpg", arg_av_format)){
	sdata->format_context.format=&mpeg_mux_format;
	} else
if(!strcasecmp("mpeg", arg_av_format)){
	sdata->format_context.format=&mpeg_mux_format;
	}
if(sdata->format_context.format!=NULL){
	sdata->format_context.pb.write_packet=file_write;
	sdata->format_context.pb.seek=file_seek;
	sdata->format_context.pb.buffer_size=1024*1024; /* 1 meg should do fine */
	sdata->format_context.pb.buffer=do_alloc(sdata->format_context.pb.buffer_size, sizeof(1));
	sdata->format_context.pb.buf_ptr=sdata->format_context.pb.buffer;
	sdata->format_context.pb.buf_end=sdata->format_context.pb.buffer+sdata->format_context.pb.buffer_size;
	sdata->format_context.pb.opaque=sdata;
	sdata->format_context.pb.packet_size=1;
	sdata->format_context.pb.write_flag=1;
	strcpy(sdata->format_context.title, arg_filename);
	sdata->format_context.format->write_header(&(sdata->format_context));
	}
sdata->video_s=new_packet_stream();
sdata->video_s->priv=sdata->v4l_data;
sdata->video_s->consume_func=ffmpeg_v4l_encoding_thread;
sdata->audio_s->consume_func=ffmpeg_audio_encoding_thread;
/* set threshhold to two frames worth of data */
sdata->video_s->threshhold=sdata->video_size*2;
if(sdata->video_stream_num>=0){
	sdata->video_s->producer_thread_running=1;
	pthread_create(&(sdata->video_reader_thread), NULL, v4l_reader_thread, sdata->v4l_data); 
	}
if(sdata->audio_stream_num>=0){
	sdata->audio_s->producer_thread_running=1;
	pthread_create(&(sdata->audio_reader_thread), NULL, alsa_reader_thread, sdata->audio_s); 
	}
return 0;
}

int ffmpeg_stop_encoding(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
V4L_DATA *data;
pthread_t thread;
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
if((sdata==NULL)||(sdata->type!=FFMPEG_CAPTURE_KEY)){
	return 0;
	}
pthread_mutex_lock(&(sdata->video_s->ctr_mutex));
pthread_mutex_lock(&(sdata->audio_s->ctr_mutex));
sdata->video_s->stop_stream=1;
sdata->audio_s->stop_stream=1;
pthread_mutex_unlock(&(sdata->audio_s->ctr_mutex));
pthread_mutex_unlock(&(sdata->video_s->ctr_mutex));
return 0;
}

int ffmpeg_incoming_fifo_size(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
V4L_DATA *data;
pthread_t thread;
struct video_picture vpic;
struct video_window vwin;
long total;

Tcl_ResetResult(interp);
/* Report 0 bytes when no such device exists or it is not capturing */

if(argc<2){
	Tcl_AppendResult(interp,"ERROR: ffmpeg_incoming_fifo_size requires one argument", NULL);
	return TCL_ERROR;
	}
#if 0  /* not necessary now */
data=get_v4l_device_from_handle(argv[1]);
if(data==NULL){
	Tcl_SetObjResult(interp, Tcl_NewIntObj(0));
	return 0;
	}
#endif
if((sdata==NULL)||(sdata->type!=FFMPEG_CAPTURE_KEY)){
	Tcl_SetObjResult(interp, Tcl_NewIntObj(0));
	return 0;
	}
if(sdata->video_s!=NULL){
	pthread_mutex_lock(&(sdata->video_s->ctr_mutex));
	}
if(sdata->audio_s!=NULL){
	pthread_mutex_lock(&(sdata->audio_s->ctr_mutex));
	}
if((sdata->video_s!=NULL) && sdata->video_s->stop_stream && 
	!sdata->video_s->consumer_thread_running &&
	!sdata->video_s->producer_thread_running){
	data->priv=NULL;
	free(sdata->video_s);
	sdata->video_s=NULL;
	}
if((sdata->audio_s!=NULL) && sdata->audio_s->stop_stream && 
	!sdata->audio_s->consumer_thread_running &&
	!sdata->audio_s->producer_thread_running){
	free(sdata->audio_s);
	sdata->audio_s=NULL;
	}
total=0;
if(sdata->video_s!=NULL){
	total+=sdata->video_s->total;
	fprintf(stderr,"video fifo size=%d\n", sdata->video_s->total);
	}
if(sdata->audio_s!=NULL){
	total+=sdata->audio_s->total;
	fprintf(stderr,"audio fifo size=%d\n", sdata->audio_s->total);
	}

Tcl_SetObjResult(interp, Tcl_NewIntObj(total));
if((sdata!=NULL)&&(sdata->audio_s==NULL)&&(sdata->video_s==NULL)){
	pthread_mutex_lock(&(sdata->format_context_mutex));
	if(sdata->format_context.format!=NULL)
		sdata->format_context.format->write_trailer(&(sdata->format_context));
	pthread_mutex_unlock(&(sdata->format_context_mutex));
	close(sdata->fd_out);
	free(sdata);
	sdata=NULL;
	fprintf(stderr,"Recording finished\n");
	return TCL_OK; 
	}
if(sdata->video_s!=NULL){
	pthread_mutex_unlock(&(sdata->video_s->ctr_mutex));
	}
if(sdata->audio_s!=NULL){
	pthread_mutex_unlock(&(sdata->audio_s->ctr_mutex));
	}
fprintf(stderr,"total=%d sdata=%p\n", total, sdata);
return TCL_OK;
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
