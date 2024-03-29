/*     avview preliminary version

       (C) Vladimir Dergachev 2001-2004
       
       GNU Public License
       
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>
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

#include <sys/time.h>
#include <pthread.h>
#include <assert.h>
#include "avcodec.h"
#include "avformat.h"
#include "v4l.h"
#include "formats.h"
#include "packet_stream.h"
#include "alsa.h"

typedef void *(*pthread_start_fn)(void *);

typedef struct {
	long type;
	long width;
	long height;
	long stop;
	/* statistics */
	long frame_count;
	long frames_encoded;
	int64 encoded_stream_size;

	/* values to write into picture struct for ffmpeg */
	int quality;
	int qscale_type;
	
	V4L_DATA *v4l_device;
	int fd_out;
	AVCodec *video_codec;
	AVCodecContext video_codec_context;
	AVCodec *audio_codec;
	AVCodecContext audio_codec_context;
	AVFormatContext format_context;
	int audio_stream_num;
	int video_stream_num;

	int64 last_audio_timestamp;
	int64 last_video_timestamp;

	int64 audio_samples;
	int64 audio_sample_sum_left;
	int64 audio_sample_sum_right;
	int   audio_sample_top_left;
	int   audio_sample_top_right;
	
	int64 luma_hist[32];
		
	pthread_mutex_t format_context_mutex;
	PACKET_STREAM *video_s;
	PACKET_STREAM *audio_s;
	pthread_t audio_reader_thread;
	ALSA_PARAMETERS alsa_param;
	} FFMPEG_ENCODING_DATA;

/* this assumes only one encoding process goes on at a time.. not too unreasonable */

FFMPEG_ENCODING_DATA *sdata=NULL;

/* #define DEBUG_TIMESTAMPS */

/* make pts (90KHz clock) from timestamps */
static int make_pts(int64 start, int64 now)
{
int64 diff=now-start;
return (int)(diff/11);
}

static int64 timestamp_now(void)
{
struct timeval tv;
gettimeofday(&tv, NULL);
return (int64)tv.tv_sec*1000000+(int64)tv.tv_usec;
}

int ffmpeg_present(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
Tcl_ResetResult(interp);
Tcl_AppendResult(interp,"yes", NULL);
return 0;
}

void ffmpeg_preprocess_frame(V4L_DATA *data, FFMPEG_ENCODING_DATA *sdata, 
		PACKET *f, AVFrame *picture)
{
switch(data->mode){
	case MODE_SINGLE_FRAME:
		convert_422_to_420p(sdata->width, sdata->height, sdata->width*2, f->buf, picture->data[0], sdata->luma_hist);
		break;
	case MODE_DEINTERLACE_BOB:
	case MODE_DEINTERLACE_WEAVE:
		deinterlace_422_bob_to_420p(sdata->width, sdata->height, sdata->width*2, f->buf, picture->data[0], sdata->luma_hist);
		break;
	case MODE_DEINTERLACE_HALF_WIDTH:
		deinterlace_422_half_width_to_420p(sdata->width, sdata->height, sdata->width*2, f->buf, picture->data[0], sdata->luma_hist);
		break;
	case MODE_DOUBLE_INTERPOLATE:
		deinterlace_422_double_interpolate_to_420p(sdata->width, sdata->height, sdata->width*2, f->buf, picture->data[0], sdata->luma_hist);
		break;		
	}
picture->pts=f->timestamp;
picture->quality=sdata->quality;
picture->type=FF_BUFFER_TYPE_SHARED;
picture->qscale_table=NULL;
picture->qscale_type=sdata->qscale_type;
}

void ffmpeg_v4l_encoding_thread(PACKET_STREAM *s)
{
int64 start_ts;
int i;
PACKET *f;
V4L_DATA *data;
AVFrame picture;
char *output_buf;
long ob_size, ob_free, ob_written;
long frames=0;

fprintf(stderr,"v4l_encoding pid %d\n", getpid());
/* encode in the background */
nice(1);
data=(V4L_DATA *)s->priv;
if((data==NULL)||(sdata==NULL)||(sdata->type!=FFMPEG_CAPTURE_KEY)||(sdata->video_s==NULL)){
	fprintf(stderr,"INTERNAL ERROR: Exit 5\n");
	pthread_mutex_lock(&(s->ctr_mutex));
	s->consumer_thread_running=0;
	pthread_mutex_unlock(&(s->ctr_mutex));
	pthread_exit(NULL);
	}
ob_size=1024*1024;
ob_free=0;
output_buf=do_alloc(ob_size, sizeof(char));
memset(&picture, 0, sizeof(picture));
picture.base[0]=picture.data[0]=do_alloc((3*sdata->video_codec_context.width*sdata->video_codec_context.height)/2+4096, sizeof(char));
picture.base[1]=picture.data[1]=picture.data[0]+sdata->video_codec_context.width*sdata->video_codec_context.height;
picture.base[2]=picture.data[2]=picture.data[1]+(sdata->video_codec_context.width*sdata->video_codec_context.height)/4;
picture.base[3]=picture.data[3]=NULL;
picture.linesize[0]=sdata->video_codec_context.width;
picture.linesize[1]=sdata->video_codec_context.width/2;
start_ts=timestamp_now();
picture.linesize[2]=sdata->video_codec_context.width/2;
pthread_mutex_lock(&(s->ctr_mutex));
while(1){
	f=get_packet(s);
	if(!(s->stop_stream & STOP_PRODUCER_THREAD) &&((f==NULL))){
		/* no data to encode - pause thread instead of spinning */
		pthread_cond_wait(&(s->suspend_consumer_thread), &(s->ctr_mutex));
		}
	pthread_mutex_unlock(&(s->ctr_mutex));
	while((f!=NULL)&&!(s->stop_stream & STOP_PRODUCER_THREAD)){
		if(1){
			/* encode frame */			
			ffmpeg_preprocess_frame(data, sdata, f, &picture);
			frames++;
			picture.age=frames;
			ob_free=avcodec_encode_video(&(sdata->video_codec_context), output_buf, ob_size, &picture);
			
			if(ob_free>0)sdata->encoded_stream_size+=ob_free; 
			/* write out data */
			ob_written=0;
			while(ob_written<ob_free){
				pthread_mutex_lock(&(sdata->format_context_mutex));
				if(sdata->format_context.oformat!=NULL){
					sdata->format_context.oformat->write_packet(&(sdata->format_context),sdata->video_stream_num, output_buf+ob_written, ob_free-ob_written, make_pts(start_ts, f->timestamp));
					i=ob_free-ob_written;
					} else {
					i=write(sdata->fd_out, output_buf+ob_written, ob_free-ob_written);
					}
				sdata->last_video_timestamp=f->timestamp;
				if(sdata->last_audio_timestamp > (f->timestamp+500000)){
					/* audio encoding is faster than us */
					if(sdata->audio_s!=NULL){
						pthread_mutex_lock(&(sdata->audio_s->ctr_mutex));
						sdata->audio_s->stop_stream |= STOP_CONSUMER_THREAD;
						pthread_mutex_unlock(&(sdata->audio_s->ctr_mutex));
						#ifdef DEBUG_TIMESTAMPS
						fprintf(stderr,"- Stopping audio thread\n");
						#endif
						}
					} else {
					/* we caught up with audio encoding */
					if((sdata->audio_s!=NULL) && (sdata->audio_stream_num>=0)){
						pthread_mutex_lock(&(sdata->audio_s->ctr_mutex));
						sdata->audio_s->stop_stream &= ~STOP_CONSUMER_THREAD;
						pthread_cond_broadcast(&(sdata->audio_s->suspend_consumer_thread));
							#ifdef DEBUG_TIMESTAMPS
							fprintf(stderr,"+ Restarting audio thread\n");
							#endif
						pthread_mutex_unlock(&(sdata->audio_s->ctr_mutex));
						}
					}
				pthread_mutex_unlock(&(sdata->format_context_mutex));
				if(i>=0)ob_written+=i;
				}
			#ifdef DEBUG_TIMESTAMPS
			fprintf(stderr,"Done encoding video packet with timestamp %lld\n", f->timestamp);
			#endif
			}
		sdata->frames_encoded++;
		/* get next one */
		f->free_func(f);
		pthread_mutex_lock(&(s->ctr_mutex));
		f=get_packet(s);
		if(s->stop_stream & STOP_CONSUMER_THREAD){
			pthread_cond_wait(&(s->suspend_consumer_thread), &(s->ctr_mutex));
			}
		pthread_mutex_unlock(&(s->ctr_mutex));
		}

	pthread_mutex_lock(&(s->ctr_mutex));
	if((s->stop_stream & STOP_PRODUCER_THREAD) && !s->producer_thread_running){
		avcodec_close(&(sdata->video_codec_context));
		while((f=get_packet(s))!=NULL){
			f->free_func(f);
			}
		do_free(picture.data[0]);
		do_free(output_buf);
		s->consumer_thread_running=0;
		pthread_mutex_unlock(&(s->ctr_mutex));
		fprintf(stderr,"Video encoding finished\n");
		pthread_exit(NULL);
		}
	if(s->stop_stream & STOP_CONSUMER_THREAD){
		pthread_cond_wait(&(s->suspend_consumer_thread), &(s->ctr_mutex));
		}
	}
s->consumer_thread_running=0;
pthread_mutex_unlock(&(s->ctr_mutex));
pthread_exit(NULL);
}

void ffmpeg_gather_audio_stats(PACKET *f)
{
signed short *p;
signed short left,right;
long i;
if((f==NULL)||(sdata==NULL))return;
p=(signed short *)f->buf;
for(i=0;i<(f->free>>2);i++){
	left=p[i<<1];
	right=p[(i<<1)|1];
	if(left<0)left=-left;
	if(right<0)right=-right;
	sdata->audio_sample_sum_left+=left;
	sdata->audio_sample_sum_right+=right;
	if(left>sdata->audio_sample_top_left)sdata->audio_sample_top_left=left;
	if(right>sdata->audio_sample_top_right)sdata->audio_sample_top_right=right;
	}
sdata->audio_samples+=(f->free>>2);
}

static void os_write_packet(int fd, unsigned char *buf, int size)
{
int written=0, count;
while(written<size){
       count=write(fd, buf+written, size-written);
       if(count<0&&count!=EAGAIN&&count!=EINTR){
               perror("writing audio");
               return;
               }
       written+=count;
       }
}


void ffmpeg_audio_encoding_thread(PACKET_STREAM *s)
{
unsigned char *out_buf;
int64 start_ts;
long ob_size, ob_free;
PACKET *f;
ob_size=sdata->audio_codec_context.frame_size*sdata->audio_codec_context.channels*sizeof(short);
out_buf=do_alloc(ob_size, 1);
fprintf(stderr,"audio_encoding pid %d\n", getpid());
/* encode in the background */
nice(1);
pthread_mutex_lock(&(s->ctr_mutex));
start_ts=timestamp_now();
while(1){
	f=get_packet(s);
	if(!(s->stop_stream & STOP_PRODUCER_THREAD) &&((f==NULL))){
		/* no data to encode - pause thread instead of spinning */
		pthread_cond_wait(&(s->suspend_consumer_thread), &(s->ctr_mutex));
		}
	pthread_mutex_unlock(&(s->ctr_mutex));
	while((f!=NULL)&& !(s->stop_stream & STOP_PRODUCER_THREAD)){
		ffmpeg_gather_audio_stats(f);
		ob_free=avcodec_encode_audio(&(sdata->audio_codec_context), out_buf, ob_size, (short *) f->buf);	
		if(ob_free>0){
			sdata->encoded_stream_size+=ob_free; 
                       	pthread_mutex_lock(&(sdata->format_context_mutex));
                       	/* write output buffer */
                       	if(sdata->format_context.oformat!=NULL){
                               	/* write using libavcodec format output */
                               	sdata->format_context.oformat->write_packet(&(sdata->format_context),sdata->audio_stream_num, out_buf, ob_free, make_pts(start_ts, f->timestamp));
                               	} else {
                               	/* write using os function */
                               	os_write_packet(sdata->fd_out, out_buf, ob_free);
                               	}
                       	/* check if we are too fast */
                       	sdata->last_audio_timestamp=f->timestamp;
                       	if(((sdata->last_video_timestamp+500000) < f->timestamp) && 
                               	(sdata->video_stream_num>=0)){
                               	pthread_mutex_lock(&(s->ctr_mutex));
                               	s->stop_stream|=STOP_CONSUMER_THREAD;
                               	pthread_mutex_unlock(&(s->ctr_mutex));
                               	#ifdef DEBUG_TIMESTAMPS
                               	fprintf(stderr,"* Stopping audio thread (self)\n");
                               	#endif
				}
                        pthread_mutex_unlock(&(sdata->format_context_mutex));
			#ifdef DEBUG_TIMESTAMPS
			fprintf(stderr,"Done encoding audio packet with timestamp %lld\n", f->timestamp);
			#endif
			}
		f->free_func(f);
		pthread_mutex_lock(&(s->ctr_mutex));
		f=get_packet(s);
		if((s->stop_stream & STOP_CONSUMER_THREAD)){
			pthread_cond_wait(&(s->suspend_consumer_thread), &(s->ctr_mutex));
			}
		pthread_mutex_unlock(&(s->ctr_mutex));
		}
	pthread_mutex_lock(&(s->ctr_mutex));	
	if((s->stop_stream & STOP_PRODUCER_THREAD) && !s->producer_thread_running){
		avcodec_close(&(sdata->audio_codec_context));
		while((f=get_packet(s))!=NULL)f->free_func(f);
		s->consumer_thread_running=0;
		pthread_mutex_unlock(&(s->ctr_mutex));
		do_free(out_buf);
		fprintf(stderr,"Audio encoding finished\n");
		pthread_exit(NULL);
		}
	if((s->stop_stream & STOP_CONSUMER_THREAD)){
		pthread_cond_wait(&(s->suspend_consumer_thread), &(s->ctr_mutex));
		}
	}
pthread_mutex_unlock(&(s->ctr_mutex));
}




static void file_write(void *opaque, unsigned char *buf, int size)
{
FFMPEG_ENCODING_DATA *sdata = (FFMPEG_ENCODING_DATA *) opaque;
int a;
int done;
done=0;
while(done<size){
	a=write(sdata->fd_out, buf+done, size-done);
	if(a>0)done+=a;
	 else 
	if(errno==ENOSPC){
		fprintf(stderr, "Please free up some disk space\n");
		sleep(1);
		}
	}
}

int file_seek(void *opaque, offset_t pos, int whence)
{
FFMPEG_ENCODING_DATA *sdata = (FFMPEG_ENCODING_DATA *) opaque;
fprintf(stderr,"file_seek pos=%lld whence=%d\n", pos, whence);
return lseek64(sdata->fd_out, pos, whence);
}

int ffmpeg_get_buffer(AVCodecContext *s, AVFrame *pic)
{
pic->type=FF_BUFFER_TYPE_SHARED;
return 0;
}

int ffmpeg_create_video_codec(Tcl_Interp* interp, int argc, const char * argv[])
{
V4L_DATA *data;
const char *arg_v4l_handle;
const char *arg_video_codec;
const char *arg_v4l_mode;
const char *arg_v4l_rate;
const char *arg_video_bitrate;
const char *arg_deinterlace_mode;
const char *arg_step_frames;
const char *arg_video_quality;
const char *arg_video_bitrate_control;
const char *arg_gop_size;
struct video_picture vpic;
double a,b;
AVCodecContext *enc=&(sdata->video_codec_context);

int qmin=2;

arg_v4l_handle=get_value(argc, argv, "-v4l_handle");
arg_gop_size=get_value(argc, argv, "-gop_size");
arg_video_codec=get_value(argc, argv, "-video_codec");
arg_v4l_mode=get_value(argc, argv, "-v4l_mode");
arg_v4l_rate=get_value(argc, argv, "-v4l_rate");
arg_video_bitrate=get_value(argc, argv, "-video_bitrate");
arg_deinterlace_mode=get_value(argc, argv, "-deinterlace_mode");
arg_step_frames=get_value(argc, argv, "-step_frames");
arg_video_quality=get_value(argc, argv, "-video_quality");
arg_video_bitrate_control=get_value(argc, argv, "-video_bitrate_control");

if((arg_v4l_handle==NULL)||
	(!strcmp(arg_v4l_handle, "none"))||
	((data=get_v4l_device_from_handle((char*)arg_v4l_handle))==NULL))return 0;

if(data->priv!=NULL){
	Tcl_AppendResult(interp,"ERROR: ffmpeg_encode_v4l_stream: v4l device busy", NULL);
	return 0;
	}
if(ioctl(data->fd, VIDIOCGWIN, &data->vwin)<0){
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
if(!strcmp("double-interpolate", arg_deinterlace_mode)){
	data->mode=MODE_DOUBLE_INTERPOLATE;
	} else
if(!strcmp("half-width", arg_deinterlace_mode)){
	data->mode=MODE_DEINTERLACE_HALF_WIDTH;
	}

data->step_frames=0;
if(arg_step_frames==NULL){
	/* nothing */
	} else
if(!strcmp("one half", arg_step_frames))data->step_frames=2;
	else
if(!strcmp("one quarter", arg_step_frames))data->step_frames=4;

sdata->width=data->vwin.width;
sdata->height=data->vwin.height;
data->video_size=data->vwin.width*data->vwin.height*2;
sdata->frame_count=1;
sdata->frames_encoded=0;
sdata->last_video_timestamp=0;
sdata->qscale_type=FF_QSCALE_TYPE_MPEG2;

if(arg_video_codec==NULL){
	sdata->video_codec=avcodec_find_encoder(CODEC_ID_MPEG2VIDEO);
	} else
if(!strcmp("MPEG-1", arg_video_codec)){
	sdata->video_codec=avcodec_find_encoder(CODEC_ID_MPEG1VIDEO);
	sdata->qscale_type=FF_QSCALE_TYPE_MPEG1;
	} else
if(!strcmp("MPEG-2", arg_video_codec)){
	sdata->video_codec=avcodec_find_encoder(CODEC_ID_MPEG2VIDEO);
	} else
if(!strcmp("MPEG-4", arg_video_codec)){
	sdata->video_codec=avcodec_find_encoder(CODEC_ID_MPEG4);
	} else
if(!strcmp("MJPEG", arg_video_codec)){
	sdata->video_codec=avcodec_find_encoder(CODEC_ID_MJPEG);
	} else
if(!strcmp("MSMPEG-4", arg_video_codec)){
	sdata->video_codec=avcodec_find_encoder(CODEC_ID_MSMPEG4V3);
	qmin=3;
	} else
if(!strcmp("H263", arg_video_codec)){
	sdata->video_codec=avcodec_find_encoder(CODEC_ID_H263);
	qmin=3;
	} else
if(!strcmp("RV10", arg_video_codec)){
	sdata->video_codec=avcodec_find_encoder(CODEC_ID_RV10);
	} else
if(!strcmp("H263P", arg_video_codec)){
	sdata->video_codec=avcodec_find_encoder(CODEC_ID_H263P);
	qmin=3;
	} else
if(!strcmp("H263I", arg_video_codec)){
	sdata->video_codec=avcodec_find_encoder(CODEC_ID_H263I);
	} else 
	sdata->video_codec=NULL;
if(sdata->video_codec==NULL){
	return 0;
	}
memset(&(sdata->video_codec_context), 0, sizeof(AVCodecContext));
avcodec_get_context_defaults(&(sdata->audio_codec_context));
enc->codec_id=sdata->video_codec->id;
enc->codec_type=sdata->video_codec->type;

switch(data->mode){
	case MODE_SINGLE_FRAME:
		enc->width=data->vwin.width;
		enc->height=data->vwin.height;
		break;
	case MODE_DOUBLE_INTERPOLATE:
	case MODE_DEINTERLACE_BOB:
	case MODE_DEINTERLACE_WEAVE:
		enc->width=data->vwin.width;
		enc->height=data->vwin.height*2;
		break;
	case MODE_DEINTERLACE_HALF_WIDTH:
		enc->width=data->vwin.width/2;
		enc->height=data->vwin.height;
		break;
	}

#ifdef FRAME_RATE_BASE
	/* ffmpeg 0.4.6 */
	#define FFMPEG_FRAME_RATE_BASE  FRAME_RATE_BASE
#else
#ifdef DEFAULT_FRAME_RATE_BASE
	/* ffmpeg from current CVS */
	#define FFMPEG_FRAME_RATE_BASE  DEFAULT_FRAME_RATE_BASE
#else
	#error "FRAME_RATE_BASE is not defined"
#endif
#endif

enc->frame_rate=0;
if(arg_v4l_rate!=NULL)enc->frame_rate=rint(atof(arg_v4l_rate)*FFMPEG_FRAME_RATE_BASE);
if(enc->frame_rate<=0)enc->frame_rate=60*FFMPEG_FRAME_RATE_BASE;
if(data->step_frames>0)enc->frame_rate=enc->frame_rate/data->step_frames;
a=(((800000.0*data->vwin.width)*data->vwin.height)*enc->frame_rate);
b=(352.0*288.0*25.0*FFMPEG_FRAME_RATE_BASE);
enc->bit_rate=0;
if(arg_video_bitrate!=NULL)enc->bit_rate=atol(arg_video_bitrate);
if(enc->bit_rate< (enc->frame_rate/FFMPEG_FRAME_RATE_BASE+1))
	enc->bit_rate=rint(a/b);
fprintf(stderr,"video: using bitrate=%d, frame_rate=%d\n", enc->bit_rate, enc->frame_rate);
enc->pix_fmt=PIX_FMT_YUV420P;
enc->flags=0;
if((arg_video_bitrate_control!=NULL)&&!strcmp(arg_video_bitrate_control, "Fix quality"))
	enc->flags|=CODEC_FLAG_QSCALE;
enc->qmin=qmin;
enc->qmax=15;
if(arg_video_quality!=NULL)sdata->quality=atoi(arg_video_quality);
if(sdata->quality<qmin)sdata->quality=qmin;
if(sdata->quality>enc->qmax)sdata->quality=enc->qmax;

enc->frame_rate_base=FFMPEG_FRAME_RATE_BASE;
enc->max_qdiff=3;
enc->aspect_ratio=0.0; /* guess assuming square pixels */
enc->me_method=ME_FULL;
enc->qblur=0.5;
enc->qcompress=0.5;
enc->b_quant_factor=1.25;
enc->b_quant_offset=1.25;
enc->i_quant_factor=-0.8;
enc->i_quant_offset=0.0;
enc->get_buffer=avcodec_default_get_buffer;
enc->release_buffer=avcodec_default_release_buffer;
enc->pix_fmt=PIX_FMT_YUV420P;
enc->rc_qsquish=1.0;
enc->rc_max_rate = 0;
enc->rc_min_rate = 0;
enc->rc_buffer_size = 0;
enc->rc_buffer_aggressivity= 1;
enc->rc_initial_cplx= 0;
enc->lumi_masking=0;
enc->temporal_cplx_masking=0;
enc->spatial_cplx_masking=0;
enc->p_masking=0;
enc->dark_masking=0;
enc->global_quality=sdata->quality;
enc->strict_std_compliance=0;
if(enc->codec_id==CODEC_ID_MPEG1VIDEO || 
	enc->codec_id==CODEC_ID_H264)
                enc->flags|= CODEC_FLAG_TRUNCATED;

enc->gop_size=0;
if((arg_video_codec!=NULL)&&(
  !strcasecmp(arg_video_codec, "MPEG-4") ||
  !strcasecmp(arg_video_codec, "MSMPEG-4")))
  	enc->gop_size=12;
if(arg_gop_size!=NULL)enc->gop_size=atol(arg_gop_size);
if(enc->gop_size<0)enc->gop_size=0;

if(sdata->video_codec->priv_data_size==0){
	fprintf(stderr,"BUG: sdata->video_codec->priv_data_size==0, fixing it\n");
	sdata->video_codec->priv_data_size=64*1024; /* 64K should be enough */
	}
if(avcodec_open(&(sdata->video_codec_context), sdata->video_codec)<0){
	return 0;
	}
data->priv=sdata;
sdata->video_s=new_packet_stream();
sdata->video_s->priv=data;
sdata->video_s->consume_func=ffmpeg_v4l_encoding_thread;
sdata->video_s->stop_stream|=STOP_CONSUMER_THREAD;
/* set threshold to two frames worth of data */
sdata->video_s->threshold=data->video_size*2;
v4l_attach_output_stream(data, sdata->video_s);
sdata->v4l_device=data;
return 1;
}

int ffmpeg_create_audio_codec(Tcl_Interp* interp, int argc, const char * argv[])
{
const char *arg_audio_codec;
const char *arg_audio_bitrate;
const char *arg_audio_rate;
long endiannes_test;
if(alsa_setup_reader_thread(sdata->audio_s, argc, argv, &(sdata->alsa_param))<0){
	return 0;
	}
arg_audio_codec=get_value(argc, argv, "-audio_codec");
arg_audio_bitrate=get_value(argc, argv, "-audio_bitrate");
arg_audio_rate=get_value(argc, argv, "-audio_rate");
sdata->audio_codec=avcodec_find_encoder(CODEC_ID_PCM_S16LE); 
sdata->last_audio_timestamp=0;
fprintf(stderr,"Using audio_codec=%s\n", arg_audio_codec);
if(arg_audio_codec==NULL){
	/* default to MPEG-2 */
	sdata->audio_codec=avcodec_find_encoder(CODEC_ID_MP2);
	} else
if(!strcasecmp(arg_audio_codec,"MPEG-2")){
	sdata->audio_codec=avcodec_find_encoder(CODEC_ID_MP2);
	} else 
if(!strcasecmp(arg_audio_codec,"MPEG-3")){
	sdata->audio_codec=avcodec_find_encoder(CODEC_ID_MP3LAME);
	} else 
if(!strcasecmp(arg_audio_codec,"VORBIS")){
	sdata->audio_codec=avcodec_find_encoder(CODEC_ID_VORBIS);
	} else 
if(!strcasecmp(arg_audio_codec,"AC-3")){
	sdata->audio_codec=avcodec_find_encoder(CODEC_ID_AC3); 
	} else
if(!strcasecmp(arg_audio_codec,"PCM")){
	endiannes_test=1;
	if(*((char *)&endiannes_test)){
		sdata->audio_codec=avcodec_find_encoder(CODEC_ID_PCM_S16LE); 
		} else {
		sdata->audio_codec=avcodec_find_encoder(CODEC_ID_PCM_S16BE); 
		}
	}
if(sdata->audio_codec==NULL){
	fprintf(stderr,"Could not find audio codec\n");
	return 0;
	}
memset(&(sdata->audio_codec_context), 0, sizeof(AVCodecContext));
avcodec_get_context_defaults(&(sdata->audio_codec_context));
sdata->audio_codec_context.bit_rate=64000;
if(arg_audio_bitrate!=NULL)sdata->audio_codec_context.bit_rate=atoi(arg_audio_bitrate);
#if 0 /* the correct code below does not work as ffmpeg is only able to encode
         at a small set of rates */
sdata->audio_codec_context.sample_rate=sdata->alsa_param.sample_rate;
#else
sdata->audio_codec_context.sample_rate=24000;
if(arg_audio_rate!=NULL)sdata->audio_codec_context.sample_rate=atol(arg_audio_rate);
#endif
sdata->audio_codec_context.channels=sdata->alsa_param.channels;
sdata->audio_codec_context.codec_id=sdata->audio_codec->id;
sdata->audio_codec_context.sample_fmt=SAMPLE_FMT_S16;
sdata->audio_codec_context.codec_type=sdata->audio_codec->type;
if(avcodec_open(&(sdata->audio_codec_context), sdata->audio_codec)!=0){
	return 0;
	}
sdata->alsa_param.chunk_size=sdata->audio_codec_context.frame_size*2*sdata->alsa_param.channels;
fprintf(stderr,"audio_codec: bit_rate=%d sample_rate=%d frame_size=%d\n",
	sdata->audio_codec_context.bit_rate,
	sdata->audio_codec_context.sample_rate,
	sdata->audio_codec_context.frame_size);

sdata->audio_s->consume_func=ffmpeg_audio_encoding_thread;
return 1;
}

int ffmpeg_encode_v4l_stream(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
int i,k;
const char *arg_filename;
const char *arg_av_format;
AVFormatParameters avfp;

Tcl_ResetResult(interp);

if(argc<5){
	Tcl_AppendResult(interp,"ERROR: ffmpeg_encode_v4l_stream requires at least four arguments", NULL);
	return TCL_ERROR;
	}

arg_filename=get_value(argc, argv, "-filename");
arg_av_format=get_value(argc, argv, "-av_format");

if(arg_filename==NULL){
	Tcl_AppendResult(interp,"ERROR: ffmpeg_encode_v4l_stream: no file to write data to specified", NULL);
	return TCL_ERROR;
	}

sdata=do_alloc(1, sizeof(FFMPEG_ENCODING_DATA));
sdata->type=FFMPEG_CAPTURE_KEY;
sdata->stop=0;
sdata->encoded_stream_size=0;
sdata->audio_samples=0;
sdata->audio_sample_sum_left=0;
sdata->audio_sample_sum_right=0;
sdata->audio_sample_top_left=0;
sdata->audio_sample_top_right=0;
sdata->video_s=NULL;
sdata->audio_s=NULL;
for(i=0;i<32;i++)sdata->luma_hist[i]=0;
pthread_mutex_init(&(sdata->format_context_mutex), NULL);

errno=0;
while(0){}
sdata->fd_out=open64(arg_filename, O_WRONLY|O_CREAT|O_TRUNC, 
		S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
if((sdata->fd_out<0)||(errno!=0)){
	do_free(sdata);
	Tcl_AppendResult(interp, strerror(errno), NULL);
	sdata=NULL;
	return TCL_OK;
	}
#ifdef FRAME_RATE_BASE
/* ffmpeg 0.4.6 */
motion_estimation_method=ME_FULL;
#endif

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
	Tcl_AppendResult(interp,"no streams to encode", NULL);
	return TCL_OK;
	}
sdata->format_context.oformat=NULL;
if(arg_av_format==NULL){
	/* nothing */
	} else
if(!strcasecmp("avi", arg_av_format)){
	sdata->format_context.oformat=guess_format("avi", NULL, NULL);
	} else
if(!strcasecmp("asf", arg_av_format)){
	sdata->format_context.oformat=guess_format("asf", NULL, NULL);
	} else 
if(!strcasecmp("mpg", arg_av_format)){
	sdata->format_context.oformat=guess_format("mpeg", NULL, NULL);
	} else
if(!strcasecmp("vob", arg_av_format)){
	sdata->format_context.oformat=guess_format("vob", NULL, NULL);
	} else
if(!strcasecmp("mpeg", arg_av_format)){
	sdata->format_context.oformat=guess_format("mpeg", NULL, NULL);
	} else
if(!strcasecmp("dv", arg_av_format)){
	sdata->format_context.oformat=guess_format("dv", NULL, NULL);
	} else
if(!strcasecmp("rm", arg_av_format)){
	sdata->format_context.oformat=guess_format("rm", NULL, NULL);
	} else
if(!strcasecmp("mov", arg_av_format)){
	sdata->format_context.oformat=guess_format("mov", NULL, NULL);
	}
if(sdata->format_context.oformat!=NULL){
	sdata->format_context.pb.write_packet=file_write;
	sdata->format_context.pb.seek=file_seek;
	sdata->format_context.pb.buffer_size=1024*1024; /* 1 meg should do fine */
	sdata->format_context.pb.buffer=do_alloc(sdata->format_context.pb.buffer_size, sizeof(1));
	sdata->format_context.pb.buf_ptr=sdata->format_context.pb.buffer;
	sdata->format_context.pb.buf_end=sdata->format_context.pb.buffer+sdata->format_context.pb.buffer_size;
	sdata->format_context.pb.opaque=sdata;
	sdata->format_context.pb.is_streamed=0;
	sdata->format_context.pb.write_flag=1;
	sdata->format_context.pb.max_packet_size=0;
	sdata->format_context.oformat->flags=AVFMT_NOFILE;
	sdata->format_context.flags=AVFMT_NOFILE;
	strcpy(sdata->format_context.title, arg_filename);
	/* no way to extract these from TV signal */
	sdata->format_context.title[0]=0;
	sdata->format_context.copyright[0]=0;
	sdata->format_context.author[0]=0;
	sdata->format_context.comment[0]=0;

	memset(&avfp, 0, sizeof(avfp));

	avfp.frame_rate=sdata->video_codec_context.frame_rate;
	avfp.frame_rate_base=sdata->video_codec_context.frame_rate_base;
	avfp.width=sdata->video_codec_context.width;
	avfp.height=sdata->video_codec_context.height;
	avfp.sample_rate=sdata->audio_codec_context.sample_rate;
	avfp.channels=sdata->audio_codec_context.channels;
	avfp.pix_fmt=sdata->video_codec_context.pix_fmt;

	av_set_parameters(&(sdata->format_context), &avfp);

	if(av_write_header(&(sdata->format_context))<0){
		Tcl_AppendResult(interp,"Could not write header (incorrect codec parameters?)", NULL);
		return TCL_OK;
		}
	} else {
	fprintf(stderr,"Warning: sdata->format_context.oformat==NULL, report to volodya@mindspring.com if you did choose a valid file format\n");
	}
if(sdata->audio_stream_num>=0){
	sdata->audio_s->producer_thread_running=1;
	if(pthread_create(&(sdata->audio_reader_thread), NULL, (pthread_start_fn) alsa_reader_thread, sdata->audio_s)!=0){
		sdata->audio_s->producer_thread_running=0;
		} 
	}
sdata->video_s->stop_stream &= ~STOP_CONSUMER_THREAD;
return TCL_OK;
}

int ffmpeg_switch_file(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
Tcl_ResetResult(interp);
if(argc<2){
	Tcl_AppendResult(interp,"ERROR: ffmpeg_switch_file requires one argument\n");
	return TCL_ERROR;
	}
fprintf(stderr, "Switching recording to file \"%s\" [NOT IMPLEMENTED]\n", argv[1]);
return TCL_OK;
}

void finish_recording()
{
pthread_mutex_lock(&(sdata->format_context_mutex));
if(sdata->format_context.oformat!=NULL)
  av_write_trailer(&(sdata->format_context));
pthread_mutex_unlock(&(sdata->format_context_mutex));
close(sdata->fd_out);
free(sdata);
sdata=NULL;
fprintf(stderr,"Recording finished\n");
}

int ffmpeg_stop_encoding(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{

Tcl_ResetResult(interp);

if((sdata==NULL)||(sdata->type!=FFMPEG_CAPTURE_KEY)){
	return 0;
	}
if(sdata->video_s!=NULL)pthread_mutex_lock(&(sdata->video_s->ctr_mutex));
if(sdata->audio_s!=NULL)pthread_mutex_lock(&(sdata->audio_s->ctr_mutex));
if(sdata->video_s!=NULL){
	sdata->video_s->stop_stream|=STOP_PRODUCER_THREAD;
	start_consumer_thread(sdata->video_s);
	}
if(sdata->audio_s!=NULL){
	sdata->audio_s->stop_stream|=STOP_PRODUCER_THREAD;
	start_consumer_thread(sdata->audio_s);
	}
if(sdata->audio_s!=NULL)pthread_mutex_unlock(&(sdata->audio_s->ctr_mutex));
if(sdata->video_s!=NULL)pthread_mutex_unlock(&(sdata->video_s->ctr_mutex));
if(sdata->video_s!=NULL)v4l_detach_output_stream(sdata->v4l_device, sdata->video_s);
if(sdata->video_s!=NULL){
	pthread_join(sdata->video_s->consumer_thread_id, NULL);
	free(sdata->video_s);
	sdata->video_s=NULL;
	}
if(sdata->audio_s!=NULL){
	pthread_join(sdata->audio_s->consumer_thread_id, NULL);
	free(sdata->audio_s);
	sdata->audio_s=NULL;
	}
finish_recording();
return 0;
}

int ffmpeg_incoming_fifo_size(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
long total;

Tcl_ResetResult(interp);
/* Report 0 bytes when no such device exists or it is not capturing */

if(argc<2){
	Tcl_AppendResult(interp,"ERROR: ffmpeg_incoming_fifo_size requires one argument", NULL);
	return TCL_ERROR;
	}
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
if((sdata->video_s!=NULL) && (sdata->video_s->stop_stream & STOP_PRODUCER_THREAD) && 
	!sdata->video_s->consumer_thread_running &&
	!sdata->video_s->producer_thread_running &&
	(sdata->video_s->total==0)){
	free(sdata->video_s);
	sdata->video_s=NULL;
	}
if((sdata->audio_s!=NULL) && (sdata->audio_s->stop_stream & STOP_PRODUCER_THREAD) && 
	!sdata->audio_s->consumer_thread_running &&
	!sdata->audio_s->producer_thread_running &&
	(sdata->audio_s->total==0)){
	free(sdata->audio_s);
	sdata->audio_s=NULL;
	}
total=0;
if(sdata->video_s!=NULL){
	total+=sdata->video_s->total;
	total+=sdata->video_s->unused_total;
	fprintf(stderr,"video: fifo size=%d recycling stack size=%d\n", 
		sdata->video_s->total, sdata->video_s->unused_total);
	}
if(sdata->audio_s!=NULL){
	total+=sdata->audio_s->total;
	total+=sdata->audio_s->unused_total;
	fprintf(stderr,"audio: fifo size=%d recycling stack size=%d\n", 
		sdata->audio_s->total, sdata->audio_s->unused_total);
	}

Tcl_SetObjResult(interp, Tcl_NewIntObj(total));
if((sdata!=NULL)&&(sdata->audio_s==NULL)&&(sdata->video_s==NULL)){
	finish_recording();
	}
if(sdata->video_s!=NULL){
	pthread_mutex_unlock(&(sdata->video_s->ctr_mutex));
	}
if(sdata->audio_s!=NULL){
	pthread_mutex_unlock(&(sdata->audio_s->ctr_mutex));
	}
fprintf(stderr,"total=%ld sdata=%p\n", total, sdata);
return TCL_OK;
}

int ffmpeg_encoding_status(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
long total_fifo;
int i;
Tcl_Obj *ans, *list;
int64 total;
int a,b;

Tcl_ResetResult(interp);
if((sdata==NULL)||(sdata->type!=FFMPEG_CAPTURE_KEY)){
	return TCL_OK;
	}
if(sdata->video_s!=NULL){
	pthread_mutex_lock(&(sdata->video_s->ctr_mutex));
	}
if(sdata->audio_s!=NULL){
	pthread_mutex_lock(&(sdata->audio_s->ctr_mutex));
	}
if((sdata->video_s!=NULL) && (sdata->video_s->stop_stream & STOP_PRODUCER_THREAD) && 
	!sdata->video_s->consumer_thread_running &&
	!sdata->video_s->producer_thread_running &&
	(sdata->video_s->total==0)){
	free(sdata->video_s);
	sdata->video_s=NULL;
	}
if((sdata->audio_s!=NULL) && (sdata->audio_s->stop_stream & STOP_PRODUCER_THREAD) && 
	!sdata->audio_s->consumer_thread_running &&
	!sdata->audio_s->producer_thread_running &&
	(sdata->audio_s->total==0)){
	free(sdata->audio_s);
	sdata->audio_s=NULL;
	}
ans=Tcl_NewListObj(0, NULL);
total_fifo=0;
if(sdata->video_s!=NULL){
	Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("video_fifo", -1));
	Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(sdata->video_s->total));
	Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("video_recycling_stack", -1));
	Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(sdata->video_s->unused_total));
	total_fifo+=sdata->video_s->total;
	total_fifo+=sdata->video_s->unused_total;
	}
if(sdata->audio_s!=NULL){
	Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("audio_fifo", -1));
	Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(sdata->audio_s->total));
	Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("audio_recycling_stack", -1));
	Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(sdata->audio_s->unused_total));
	total_fifo+=sdata->audio_s->total;
	total_fifo+=sdata->audio_s->unused_total;
	}
Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("total_fifo", -1));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(total_fifo));

Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("frames_encoded", -1));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(sdata->frames_encoded));

Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("encoded_stream_size", -1));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj((long)((sdata->encoded_stream_size+((1<<20)-1))>>20))); /* convert to megabytes */

if(sdata->audio_samples>0){
	Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("avg_left_level", -1));
	Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj((1000*(sdata->audio_sample_sum_left/sdata->audio_samples))/32768));

	Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("avg_right_level", -1));
	Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj((1000*(sdata->audio_sample_sum_right/sdata->audio_samples))/32768));
	}

Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("top_left_level", -1));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(((1000*sdata->audio_sample_top_left)/32768)));

Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("top_right_level", -1));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(((1000*sdata->audio_sample_top_right)/32768)));

Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("luma_hist", -1));

list=Tcl_NewListObj(0, NULL);
total=0;
b=0;
a=0;
for(i=0;i<32;i++)total+=sdata->luma_hist[i];
for(i=0;i<32;i++){
	if(total>0)a=(1000*sdata->luma_hist[i])/total;
	if(a>1000)a=1000;
	if(a>b)b=a;
	Tcl_ListObjAppendElement(interp, list, Tcl_NewIntObj(a));
	}
Tcl_ListObjAppendElement(interp, ans, list);

Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("luma_top_hist", -1));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(b));

Tcl_SetObjResult(interp, ans);

sdata->audio_samples=0;
sdata->audio_sample_sum_left=0;
sdata->audio_sample_sum_right=0;
sdata->audio_sample_top_left=0;
sdata->audio_sample_top_right=0;
for(i=0;i<32;i++)sdata->luma_hist[i]=0;

if((sdata!=NULL)&&(sdata->audio_s==NULL)&&(sdata->video_s==NULL)){
	finish_recording();
	}
if(sdata->video_s!=NULL){
	pthread_mutex_unlock(&(sdata->video_s->ctr_mutex));
	}
if(sdata->audio_s!=NULL){
	pthread_mutex_unlock(&(sdata->audio_s->ctr_mutex));
	}
return TCL_OK;
}

struct {
	char *name;
	Tcl_CmdProc *command;
	} ffmpeg_commands[]={
	{"ffmpeg_present", ffmpeg_present},
	{"ffmpeg_encode_v4l_stream", ffmpeg_encode_v4l_stream},
	{"ffmpeg_switch_file", ffmpeg_switch_file},
	{"ffmpeg_stop_encoding", ffmpeg_stop_encoding},
	{"ffmpeg_incoming_fifo_size", ffmpeg_incoming_fifo_size},
	{"ffmpeg_encoding_status", ffmpeg_encoding_status},
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
av_register_all();
//avcodec_register_all();
if((LIBAVCODEC_VERSION_INT!=avcodec_version())||(LIBAVFORMAT_VERSION_INT!=LIBAVCODEC_VERSION_INT)){
	fprintf(stderr, "Detected mismatch between ffmpeg library headers and/or binaries. This error can only occur if somehow avview compilation used headers and/or binaries from different version of ffmpeg.\n");
	fprintf(stderr,"avcodec.h version:0x%06x\n", LIBAVCODEC_VERSION_INT);
	fprintf(stderr,"avformat.h version:0x%06x\n", LIBAVFORMAT_VERSION_INT);
	fprintf(stderr,"libavcodec version:0x%06x\n", avcodec_version());
	exit(-1);
	}

#endif

for(i=0;ffmpeg_commands[i].name!=NULL;i++)
	Tcl_CreateCommand(interp, ffmpeg_commands[i].name, ffmpeg_commands[i].command, (ClientData)0, NULL);
}
