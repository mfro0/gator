/*     avview preliminary version

       (C) Vladimir Dergachev 2001-2003
       
       GNU Public License
       
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libzvbi.h>

#include "global.h"
#include "string_cache.h"
#include "vbi.h"



STRING_CACHE *vbi_sc=NULL;

int vbi_open_device(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
long i;
VBI_DATA *data;
unsigned int services;
char *errstr;
int type;

Tcl_ResetResult(interp);

if(argc<3){
	Tcl_AppendResult(interp,"ERROR: vbi_open_device requires two arguments", NULL);
	return TCL_ERROR;
	}
i=add_string(vbi_sc, (char*)argv[1]);
if(vbi_sc->data[i]!=NULL){
	data=(VBI_DATA*) vbi_sc->data[i];
	vbi_capture_delete(data->cap);
	} else {
	vbi_sc->data[i]=do_alloc(1, sizeof(V4L_DATA));
	}
data=(V4L_DATA*) vbi_sc->data[i];
memset(data, 0, sizeof(*data));

services = VBI_SLICED_VBI_525 | VBI_SLICED_VBI_625
		| VBI_SLICED_TELETEXT_B | VBI_SLICED_CAPTION_525
		| VBI_SLICED_CAPTION_625 | VBI_SLICED_VPS
		| VBI_SLICED_WSS_625 | VBI_SLICED_WSS_CPR1204;

type=525; /* fix as NTSC for now .. */
do {		
	/* Try v4l2 interface first.. */
	data->cap=vbi_capture_v4l2_new(argv[2], 5, &services, -1, &errstr, 0);
	if(data->cap!=NULL)break;
	
	fprintf(stderr,"Failed to open %s as V4L2 VBI device: %s\n", argv[2], errstr);
	free(errstr);
	
	/* Try regular v4l */
	data->cap=vbi_capture_v4l_new(argv[2], type, &services, -1, &errstr, 0);
	if(data->cap!=NULL)break;
	
	fprintf(stderr, "Failed to open %s as V4L VBI device: %s\n", argv[2], errstr);
	free(errstr);
	
	/* Now try bktr.. */
	data->cap=vbi_capture_bktr_new(argv[2], type, &services, -1, &errstr, 0);
	if(data->cap!=NULL)break;
	
	fprintf(stderr, "Failed to open %s as BKTR VBI device: %s\n", argv[2], errstr);
	free(errstr);
	
	free(data);
	vbi_sc->data[i]=NULL;
	Tcl_AppendResult(interp, "failed to open VBI device, see stderr for details\n", NULL);
	return TCL_ERROR;
	} while (0); 

data->par=vbi_capture_parameters(data->cap);
if(data->par==NULL){
	vbi_capture_delete(data->cap);
	free(data);
	vbi_sc->data[i]=NULL;
	Tcl_AppendResult(interp, "failed to open VBI device: error getting capture parameters\n", NULL);
	return TCL_ERROR;	
	}
return TCL_OK;
}

int vbi_close_device(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
long i;
VBI_DATA *data;

Tcl_ResetResult(interp);

if(argc<2){
	Tcl_AppendResult(interp,"ERROR: vbi_close_device requires one argument", NULL);
	return TCL_ERROR;
	}

/* produce no errors if device has never been opened */

i=lookup_string(vbi_sc, argv[1]);
if(i<0){
	return 0;
	}
data=(V4L_DATA *)vbi_sc->data[i];	
if(data==NULL){
	return 0;
	}
vbi_capture_delete(data->cap);
free(data);
vbi_sc->data[i]=NULL;
return 0;
}


struct {
	char *name;
	Tcl_CmdProc *command;
	} vbi_commands[]={
	{"vbi_open_device", vbi_open_device},
	{"vbi_close_device", vbi_close_device},
	{NULL, NULL}
	};

void init_vbi(Tcl_Interp *interp)
{
long i;

vbi_sc=new_string_cache();
snapshot_data=NULL;
minfo=NULL;

for(i=0;vbi_commands[i].name!=NULL;i++)
	Tcl_CreateCommand(interp, vbi_commands[i].name, vbi_commands[i].command, (ClientData)0, NULL);
}
