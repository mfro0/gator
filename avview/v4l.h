/*     avview preliminary version

       (C) Vladimir Dergachev 2001-2002
       
       GNU Public License
       
*/

#ifndef __V4L_H__
#define __V4L_H__

#include <linux/videodev.h>
#include <pthread.h>
#include "packet_stream.h"

#define MODE_SINGLE_FRAME		1
#define MODE_DEINTERLACE_BOB		2
#define MODE_DEINTERLACE_WEAVE		3
#define MODE_DEINTERLACE_HALF_WIDTH	4
#define MODE_DOUBLE_INTERPOLATE		5

typedef struct S_V4L_DATA{
	int fd;
	/* V4L information fields */
	struct video_capability vcap;
	struct video_picture vpic;
	struct video_window vwin;
	
	int use_count;
	int mode;
	int frame_count; /* to keep track of odd/even fields */
	long step_frames;
	long video_size;
	void *priv;
	pthread_mutex_t streams_out_mutex;
	PACKET_STREAM **streams_out;
	long streams_out_free;
	long streams_out_size;
	pthread_t v4l_reader_thread;
	} V4L_DATA;



void init_v4l(Tcl_Interp *interp);
V4L_DATA *get_v4l_device_from_handle(char *handle);
void v4l_attach_output_stream(V4L_DATA *data, PACKET_STREAM *s);
void v4l_detach_output_stream(V4L_DATA *data, PACKET_STREAM *s);

#define V4L_SNAPSHOT_KEY	1
#define FFMPEG_CAPTURE_KEY	2

#endif
