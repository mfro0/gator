/*     avview preliminary version

       (C) Vladimir Dergachev 2001-2003
       
       GNU Public License
       
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>


#include <X11/X.h>
#include <X11/Xlib.h>
#include <tcl.h>
#include <tk.h>

#include <libzvbi.h>
#include <math.h>

#include "global.h"
#include "string_cache.h"
#include "vbi.h"



STRING_CACHE *vbi_sc=NULL;

#define TYPE_CC1	1
#define TYPE_EVENT	2

typedef struct {
	int type;
	int c1,c2;
	vbi_event ev;
	} DATAGRAM;

void vbi_pipe_handler(ClientData clientData, int mask)
{
VBI_DATA *data=(VBI_DATA *)clientData;
DATAGRAM d;
int r,a;
char str[20];
r=0;
do{
	a=read(data->fd[0], ((unsigned char *)&d)+r, sizeof(d)-r);
	if(a>0)r+=a;
	} while (r<sizeof(d));
if(r<0){
	Tcl_DeleteFileHandler(data->fd[0]);
	fprintf(stderr, "Exiting\n");
	return;
	}
if(data->event_command==NULL)return; /* no handler specified, return */
switch(d.type){
	case TYPE_CC1:
		printf("%c", d.c1 & 0x7f);
		printf("%c", d.c2 & 0x7f);
		/* printf("%d %d\n", d.c1, d.c2); */
		break;
	case TYPE_EVENT:
		switch(d.ev.type){
			case VBI_EVENT_CAPTION:
				sprintf(str, "%d", d.ev.ev.caption.pgno);
				Tcl_VarEval(data->interp, data->event_command, " caption ", str, NULL);
				break;
			case VBI_EVENT_PROG_INFO:
				Tcl_VarEval(data->interp, data->event_command, " program_info {}", NULL);
				break;
			case VBI_EVENT_TTX_PAGE:
				sprintf(str, "%d", d.ev.ev.ttx_page.pgno);
				Tcl_VarEval(data->interp, data->event_command, " ttx_page ", str, NULL);
				break;
			case VBI_EVENT_ASPECT:
				Tcl_VarEval(data->interp, data->event_command, " aspect {}", NULL);
				break;
			
			default:
			}
		break;
	default:
	}
}

void vbi_loop(VBI_DATA *data)
{
int h,w;
unsigned char *buf; 
vbi_sliced *buf_sliced;
double timestamp;
struct timeval tv;
int r;
int lines,i;
DATAGRAM d;

w=data->par->bytes_per_line/1;
h=data->par->count[0]+data->par->count[1];
buf=alloca(w*h);
buf_sliced=alloca(h*sizeof(vbi_sliced));
fprintf(stderr,"Hello\n");
while(1){
	tv.tv_sec=5;
	tv.tv_usec=0;
	r=vbi_capture_read(data->cap, buf, buf_sliced, &lines, &timestamp, &tv);
	if(r<0){
		fprintf(stderr, "Exiting VBI loop\n");
		return;
		}
	pthread_mutex_lock(&(data->mutex));
	vbi_decode(data->dec, buf_sliced, lines, timestamp);
	pthread_mutex_unlock(&(data->mutex));
	}
}

void vbi_event_handler2(vbi_event *event, void *user_data)
{
VBI_DATA *data=(VBI_DATA *)user_data;
DATAGRAM d;
int r,a;
d.type=TYPE_EVENT;
memcpy(&d.ev, event, sizeof(*event));
r=0;
do {
	a=write(data->fd[1], ((unsigned char *)&d)+r, sizeof(d)-r);
	if(a==EPIPE)return; /* close thread */
	if(a>0)r+=a;
	if((a<0)&&(a!=EINTR))return; /* close thread, just in case */
	} while (r<sizeof(d));
}

int vbi_open_device(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
long i;
VBI_DATA *data;
unsigned int services;
char *aerrstr, *berrstr, *cerrstr;
int type,a;

Tcl_ResetResult(interp);

if(argc<3){
	Tcl_AppendResult(interp,"ERROR: vbi_open_device requires two arguments", NULL);
	return TCL_ERROR;
	}
i=add_string(vbi_sc, (char*)argv[1]);
if(vbi_sc->data[i]!=NULL){
	data=(VBI_DATA*) vbi_sc->data[i];
	close(data->fd[0]);
	close(data->fd[1]);
	vbi_capture_delete(data->cap);
	vbi_decoder_delete(data->dec);
	if(data->event_command!=NULL)free(data->event_command);
	} else {
	vbi_sc->data[i]=do_alloc(1, sizeof(VBI_DATA));
	}
data=(VBI_DATA*) vbi_sc->data[i];
memset(data, 0, sizeof(*data));

pthread_mutex_init(&(data->mutex), NULL);
data->interp=interp;

services = VBI_SLICED_VBI_525 | VBI_SLICED_VBI_625
		| VBI_SLICED_TELETEXT_B | VBI_SLICED_CAPTION_525
		| VBI_SLICED_CAPTION_625 | VBI_SLICED_VPS
		| VBI_SLICED_WSS_625 | VBI_SLICED_WSS_CPR1204;

type=525; /* fix as NTSC for now .. */
do {		
	/* Try v4l2 interface first.. */
	data->cap=vbi_capture_v4l2_new(argv[2], 5, &services, -1, &aerrstr, 0);
	if(data->cap!=NULL)break;
	
	/* Try regular v4l */
	data->cap=vbi_capture_v4l_new(argv[2], type, &services, -1, &berrstr, 0);
	if(data->cap!=NULL){
		free(aerrstr);
		break;
		}
	
	/* Now try bktr.. */
	data->cap=vbi_capture_bktr_new(argv[2], type, &services, -1, &cerrstr, 0);
	if(data->cap!=NULL){
		free(aerrstr);
		free(berrstr);
		break;
		}
		
	free(data);
	vbi_sc->data[i]=NULL;
	Tcl_AppendResult(interp, "failed to open VBI device:\n", 
		"V4L2 access method failed: ", aerrstr, "\n",
		"V4L access method failed: ", berrstr, "\n",
		"BKTR access method failed: ", cerrstr, "\n", NULL);
	free(aerrstr);
	free(berrstr);
	free(cerrstr);
	return TCL_ERROR;
	} while (0); 

data->par=vbi_capture_parameters(data->cap);
if(data->par==NULL){
	vbi_capture_delete(data->cap);
	free(data);
	vbi_sc->data[i]=NULL;
	Tcl_AppendResult(interp, "failed to open VBI device: error getting capture parameters", NULL);
	return TCL_ERROR;	
	}
data->dec=vbi_decoder_new();
if(data->dec==NULL){
	Tcl_AppendResult(interp, "failure during open of VBI device: cannot create vbi decoder",NULL);
	vbi_capture_delete(data->cap);
	free(data);
	vbi_sc->data[i]=NULL;
	return TCL_ERROR;
	}
vbi_event_handler_register(data->dec, 
	VBI_EVENT_CLOSE|VBI_EVENT_TTX_PAGE|VBI_EVENT_CAPTION|
	VBI_EVENT_NETWORK|VBI_EVENT_NETWORK|VBI_EVENT_TRIGGER|
	VBI_EVENT_ASPECT|VBI_EVENT_PROG_INFO,
	vbi_event_handler2,
	data);
if((a=pipe(data->fd))<0){
	Tcl_AppendResult(interp, "failure during open of VBI device: cannot create pipe:", strerror(a), NULL);
	vbi_decoder_delete(data->dec);
	vbi_capture_delete(data->cap);
	free(data);
	vbi_sc->data[i]=NULL;
	return TCL_ERROR;
	}
Tcl_CreateFileHandler(data->fd[0], TCL_READABLE, vbi_pipe_handler, data);
if(pthread_create(&(a), NULL, vbi_loop, data)!=0){
	close(data->fd[0]);
	close(data->fd[1]);
	vbi_capture_delete(data->cap);	
	vbi_decoder_delete(data->dec);
	free(data);
	vbi_sc->data[i]=NULL;
	Tcl_AppendResult(interp, "failure during open of VBI device: cannot create loop thread:", strerror(errno), NULL);
	return TCL_ERROR;
	}
return TCL_OK;
}

int vbi_new_channel(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
long i;
VBI_DATA *data;

Tcl_ResetResult(interp);

if(argc<2){
	Tcl_AppendResult(interp,"ERROR: vbi_new_channel requires one argument", NULL);
	return TCL_ERROR;
	}

/* produce no errors if device has never been opened */

i=lookup_string(vbi_sc, argv[1]);
if(i<0){
	return 0;
	}
data=(VBI_DATA *)vbi_sc->data[i];	
if(data==NULL){
	return 0;
	}
pthread_mutex_lock(&(data->mutex));
vbi_channel_switched(data->dec, 0);
pthread_mutex_unlock(&(data->mutex));
return 0;
}

int vbi_set_event_handler(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
long i;
VBI_DATA *data;

Tcl_ResetResult(interp);

if(argc<3){
	Tcl_AppendResult(interp,"ERROR: vbi_new_channel requires two arguments", NULL);
	return TCL_ERROR;
	}

i=lookup_string(vbi_sc, argv[1]);
if(i<0){
	Tcl_AppendResult(interp,"ERROR: vbi_draw_CC_page no such handle", NULL);
	return TCL_ERROR;
	}
data=(VBI_DATA *)vbi_sc->data[i];	
if(data==NULL){
	Tcl_AppendResult(interp,"ERROR: vbi_draw_CC_page no such handle", NULL);
	return TCL_ERROR;
	}
if(data->event_command!=NULL){
	free(data->event_command);
	}
if(!argv[2][0])data->event_command=NULL; /* disable when string is empty */
	else
	data->event_command=strdup(argv[2]);
return 0;
}

int vbi_draw_cc_page2(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
long i;
VBI_DATA *data;
int pageno;
vbi_page page;
Tk_PhotoImageBlock pib;
Tk_PhotoHandle ph;
unsigned char *canvas;

Tcl_ResetResult(interp);

if(argc<4){
	Tcl_AppendResult(interp,"ERROR: vbi_new_channel requires three arguments", NULL);
	return TCL_ERROR;
	}

i=lookup_string(vbi_sc, argv[1]);
if(i<0){
	Tcl_AppendResult(interp,"ERROR: vbi_draw_CC_page no such handle", NULL);
	return TCL_ERROR;
	}
data=(VBI_DATA *)vbi_sc->data[i];	
if(data==NULL){
	Tcl_AppendResult(interp,"ERROR: vbi_draw_CC_page no such handle", NULL);
	return TCL_ERROR;
	}
pageno=atoi(argv[2]);
if((pageno<1)||(pageno>8)){
	Tcl_AppendResult(interp,"ERROR: vbi_draw_CC_page no such handle", NULL);
	return TCL_ERROR;	
	}
ph=Tk_FindPhoto(interp, argv[3]);
pthread_mutex_lock(&(data->mutex));
vbi_fetch_cc_page(data->dec, &page, pageno, TRUE);
pthread_mutex_unlock(&(data->mutex));
canvas=alloca(page.columns*page.rows*16*26*4);
vbi_draw_cc_page(&page, VBI_PIXFMT_RGBA32_LE, canvas);
pib.width=page.columns*16;
pib.height=page.rows*26;
pib.pixelSize=4;
pib.pitch=pib.width*pib.pixelSize;
pib.offset[0]=0;
pib.offset[1]=1;
pib.offset[2]=2;
pib.offset[3]=3;
pib.pixelPtr=canvas;
Tk_PhotoSetSize(ph, pib.width, pib.height);
Tk_PhotoPutBlock(ph, &pib, 0, 0, pib.width, pib.height
#if (TK_MAJOR_VERSION==8) && (TK_MINOR_VERSION==4)
	, TK_PHOTO_COMPOSITE_SET
#endif
	);
return 0;
}

int vbi_draw_cc_page_scaled(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
long i,k,l;
VBI_DATA *data;
int pageno;
vbi_page page;
Tk_PhotoImageBlock pib;
Tk_PhotoHandle ph;
unsigned char *canvas,*p,*s1,*s2,*s3,*s4;
double cr,cc,R,G,B,dx1,dx2,dy1,dy2;
long clr,clc;
long cpitch,bgpixel;
unsigned char Rb,Gb,Bb;

Tcl_ResetResult(interp);

if(argc<5){
	Tcl_AppendResult(interp,"ERROR: vbi_new_channel requires four arguments", NULL);
	return TCL_ERROR;
	}

i=lookup_string(vbi_sc, argv[1]);
if(i<0){
	Tcl_AppendResult(interp,"ERROR: vbi_draw_CC_page no such handle", NULL);
	return TCL_ERROR;
	}
data=(VBI_DATA *)vbi_sc->data[i];	
if(data==NULL){
	Tcl_AppendResult(interp,"ERROR: vbi_draw_CC_page no such handle", NULL);
	return TCL_ERROR;
	}
pageno=atoi(argv[2]);
if((pageno<1)||(pageno>8)){
	Tcl_AppendResult(interp,"ERROR: vbi_draw_CC_page no such handle", NULL);
	return TCL_ERROR;	
	}
ph=Tk_FindPhoto(interp, argv[3]);
bgpixel=atol(argv[4]);
Rb=bgpixel & 0xff;
Gb=(bgpixel >> 8) & 0xff;
Bb=(bgpixel >> 16) & 0xff;
bgpixel|=0xff000000;
pthread_mutex_lock(&(data->mutex));
vbi_fetch_cc_page(data->dec, &page, pageno, TRUE);
pthread_mutex_unlock(&(data->mutex));
canvas=alloca(page.columns*16*(page.rows*26+1)*4+4);
vbi_draw_cc_page(&page, VBI_PIXFMT_RGBA32_LE, canvas);

Tk_PhotoGetSize(ph, &pib.width, &pib.height);
pib.pixelSize=4;
pib.pitch=pib.width*pib.pixelSize;
pib.offset[0]=0;
pib.offset[1]=1;
pib.offset[2]=2;
pib.offset[3]=3;
pib.pixelPtr=do_alloc(pib.pitch*pib.height,1);

cpitch=page.columns*16*4;
for(l=0;l<pib.height;l++){
	cr=(1.0*l*page.rows*26)/pib.height;
	clr=floor(cr);
	dy2=cr-(double)clr;
	dy1=1.0-dy2;
	p=&(pib.pixelPtr[l*pib.pitch]);
	for(k=0;k<pib.width;k++){
		cc=(1.0*k*page.columns*16)/pib.width;
		clc=floor(cc);
		dx2=cc-(double)clc;
		dx1=1.0-dx2;
		s1=&(canvas[clr*cpitch+clc*4]);
		s2=s1+4;
		s3=s1+cpitch;
		s4=s3+4;	
		if(!*s1 && !*s2 && !*s3 && !*s4)R=0;
			else	
		R=*s1*dx1*dy1+*s2*dx2*dy1+
			*s3*dx1*dy2+*s4*dx2*dy2;
		s1++;
		s2++;
		s3++;
		s4++;
		if(!*s1 && !*s2 && !*s3 && !*s4)G=0;
			else	
		G=*s1*dx1*dy1+*s2*dx2*dy1+
			*s3*dx1*dy2+*s4*dx2*dy2;
		s1++;
		s2++;
		s3++;
		s4++;
		if(!*s1 && !*s2 && !*s3 && !*s4)B=0;
			else	
		B=*s1*dx1*dy1+*s2*dx2*dy1+
			*s3*dx1*dy2+*s4*dx2*dy2;
		#define TRIM(a)	   { if(a<0)a=0;else if(a>255)a=255; }
		if(!R && !G && !B){
			memcpy(p, &bgpixel, 4);
			p+=4;
			continue;
			}
		TRIM(R)
		TRIM(G)
		TRIM(B)
		*p=R;		
		p++;
		*p=G;
		p++;
		*p=B;
		p++;
		*p=0xff;
		p++;
		}
	}

Tk_PhotoPutBlock(ph, &pib, 0, 0, pib.width, pib.height
#if (TK_MAJOR_VERSION==8) && (TK_MINOR_VERSION==4)
	, TK_PHOTO_COMPOSITE_SET
#endif
	);
free(pib.pixelPtr);
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
data=(VBI_DATA *)vbi_sc->data[i];	
if(data==NULL){
	return 0;
	}
close(data->fd[0]);
close(data->fd[1]);
vbi_capture_delete(data->cap);
vbi_decoder_delete(data->dec);
if(data->event_command!=NULL)free(data->event_command);
free(data);
vbi_sc->data[i]=NULL;
return 0;
}


struct {
	char *name;
	Tcl_CmdProc *command;
	} vbi_commands[]={
	{"vbi_open_device", vbi_open_device},
	{"vbi_new_channel", vbi_new_channel},
	{"vbi_set_event_handler", vbi_set_event_handler},
	{"vbi_draw_cc_page", vbi_draw_cc_page2},
	{"vbi_draw_cc_page_scaled", vbi_draw_cc_page_scaled},
	{"vbi_close_device", vbi_close_device},
	{NULL, NULL}
	};

void init_vbi(Tcl_Interp *interp)
{
long i;
vbi_export_info *vbi_ei;

vbi_sc=new_string_cache();

fprintf(stderr,"libzvbi available export modules:\n");
for(i=0;;i++){
	vbi_ei=vbi_export_info_enum(i);
	if(vbi_ei==NULL)break;
	fprintf(stderr,"\t%s:keyword \"%s\" mimetype \"%s\" extension \"%s\"\n", 
		vbi_ei->label, vbi_ei->keyword, vbi_ei->mime_type, vbi_ei->extension);
	}

for(i=0;vbi_commands[i].name!=NULL;i++)
	Tcl_CreateCommand(interp, vbi_commands[i].name, vbi_commands[i].command, (ClientData)0, NULL);
}
