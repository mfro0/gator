/*     avview preliminary version

       (C) Vladimir Dergachev 2001-2002
       
       GNU Public License
       
*/
#ifndef __ALSA_H__
#define __ALSA_H__

#include "packet_stream.h"

typedef struct {
	long sample_rate;  /* in Hz */
	long format;
	long channels;
	long chunk_size;
	} ALSA_PARAMETERS;

void alsa_reader_thread(PACKET_STREAM *s);
int alsa_setup_reader_thread(PACKET_STREAM *s, int argc, char *argv[], ALSA_PARAMETERS *param);

void init_alsa(Tcl_Interp *interp);
int alsa_present(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[]);


#endif
