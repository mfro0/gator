/*     avview preliminary version

       (C) Vladimir Dergachev 2001
       
       GNU Public License
       
*/

#ifndef __V4L_H__
#define __V4L_H__

#include <linux/videodev.h>


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



void init_v4l(Tcl_Interp *interp);
V4L_DATA *get_v4l_device_from_handle(char *handle);


#endif
