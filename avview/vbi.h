/*     avview preliminary version

       (C) Vladimir Dergachev 2001-2003
       
       GNU Public License
       
*/

#ifndef __VBI_H__
#define __VBI_H__

#include <libzvbi.h>

typedef struct {
	pthread_mutex_t mutex;
	pthread_mutex_t pw_mutex;
	vbi_capture * cap;
	vbi_raw_decoder * par;
	vbi_decoder *dec;
	int fd[2];  /* pipe - this is used to signal Tcl/Tk */
	char *event_command;
	Tcl_Interp *interp;
	} VBI_DATA;

void init_vbi(Tcl_Interp *interp);

#endif
