/*     avview preliminary version

       (C) Vladimir Dergachev 2001
       
       GNU Public License
       
*/


#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xvlib.h>
#include <tcl.h>
#include <tk.h>

void xv_notify_handler(void *p, XEvent *xev)
{
fprintf(stderr,"type=%d+\n", xev->type);
}

int xv_numadaptors(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tk_Window tkwin;
Window win;
Display *d;
int num_adaptors;
XvAdaptorInfo *xvi;

Tcl_ResetResult(interp);

if(argc<2){
	Tcl_AppendResult(interp,"ERROR: xv_numadaptors requires one argument", NULL);
	return TCL_ERROR;
	}


tkwin=Tk_NameToWindow(interp,argv[1], Tk_MainWindow(interp));

if(tkwin==NULL){
	Tcl_AppendResult(interp,"ERROR: xv_numadaptors: first argument must be an existing toplevel or frame window", NULL);
	return TCL_ERROR;
	}

d=Tk_Display(tkwin);
win=Tk_WindowId(tkwin);

if((d==NULL)||(win==(Window)NULL)){
	Tcl_AppendResult(interp,"ERROR: xv_numadaptors: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}

XvQueryAdaptors(d, win, &num_adaptors, &xvi);

Tcl_SetObjResult(interp, Tcl_NewIntObj(num_adaptors));

XvFreeAdaptorInfo(xvi);

return 0;
}

int xv_adaptor_name(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tk_Window tkwin;
Window win;
Display *d;
int num_adaptors;
int adaptor;
XvAdaptorInfo *xvi;

Tcl_ResetResult(interp);

if(argc<3){
	Tcl_AppendResult(interp,"ERROR: xv_adaptor_name requires two arguments", NULL);
	return TCL_ERROR;
	}


tkwin=Tk_NameToWindow(interp,argv[1], Tk_MainWindow(interp));

if(tkwin==NULL){
	Tcl_AppendResult(interp,"ERROR: xv_adaptor_name: first argument must be an existing toplevel or frame window", NULL);
	return TCL_ERROR;
	}

d=Tk_Display(tkwin);
win=Tk_WindowId(tkwin);

if((d==NULL)||(win==(Window)NULL)){
	Tcl_AppendResult(interp,"ERROR: xv_adaptor_name: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}

XvQueryAdaptors(d, win, &num_adaptors, &xvi);
adaptor=atoi(argv[2]);
if((adaptor<0)||(adaptor>=num_adaptors)){
	XvFreeAdaptorInfo(xvi);
	Tcl_AppendResult(interp,"ERROR: xv_adaptor_name: no such adaptor", NULL);
	return TCL_ERROR;
	}

Tcl_AppendResult(interp, xvi[adaptor].name, NULL);

XvFreeAdaptorInfo(xvi);

return 0;
}

int xv_adaptor_type(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tk_Window tkwin;
Window win;
Display *d;
int num_adaptors;
int adaptor;
XvAdaptorInfo *xvi;
Tcl_Obj *ans;

Tcl_ResetResult(interp);

if(argc<3){
	Tcl_AppendResult(interp,"ERROR: xv_adaptor_type requires two arguments", NULL);
	return TCL_ERROR;
	}


tkwin=Tk_NameToWindow(interp,argv[1], Tk_MainWindow(interp));

if(tkwin==NULL){
	Tcl_AppendResult(interp,"ERROR: xv_adaptor_type: first argument must be an existing toplevel or frame window", NULL);
	return TCL_ERROR;
	}

d=Tk_Display(tkwin);
win=Tk_WindowId(tkwin);

if((d==NULL)||(win==(Window)NULL)){
	Tcl_AppendResult(interp,"ERROR: xv_adaptor_type: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}

XvQueryAdaptors(d, win, &num_adaptors, &xvi);
adaptor=atoi(argv[2]);
if((adaptor<0)||(adaptor>=num_adaptors)){
	XvFreeAdaptorInfo(xvi);
	Tcl_AppendResult(interp,"ERROR: xv_adaptor_type: no such adaptor", NULL);
	return TCL_ERROR;
	}

ans=Tcl_NewListObj(0, NULL);

if(xvi[adaptor].type & XvInputMask)Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("input", 5));

if(xvi[adaptor].type & XvOutputMask)Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("output", 6));
if(xvi[adaptor].type & XvVideoMask)Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("video", 5));
if(xvi[adaptor].type & XvStillMask)Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("still", 5));
if(xvi[adaptor].type & XvImageMask)Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("image", 5));

Tcl_SetObjResult(interp, ans);

XvFreeAdaptorInfo(xvi);

return 0;
}

int xv_adaptor_ports(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tk_Window tkwin;
Window win;
Display *d;
int num_adaptors;
int adaptor;
XvAdaptorInfo *xvi;
Tcl_Obj *ans;
int i;

Tcl_ResetResult(interp);

if(argc<3){
	Tcl_AppendResult(interp,"ERROR: xv_adaptor_ports requires two arguments", NULL);
	return TCL_ERROR;
	}


tkwin=Tk_NameToWindow(interp,argv[1], Tk_MainWindow(interp));

if(tkwin==NULL){
	Tcl_AppendResult(interp,"ERROR: xv_adaptor_ports: first argument must be an existing toplevel or frame window", NULL);
	return TCL_ERROR;
	}

d=Tk_Display(tkwin);
win=Tk_WindowId(tkwin);

if((d==NULL)||(win==(Window)NULL)){
	Tcl_AppendResult(interp,"ERROR: xv_adaptor_ports: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}

XvQueryAdaptors(d, win, &num_adaptors, &xvi);
adaptor=atoi(argv[2]);
if((adaptor<0)||(adaptor>=num_adaptors)){
	XvFreeAdaptorInfo(xvi);
	Tcl_AppendResult(interp,"ERROR: xv_adaptor_ports: no such adaptor", NULL);
	return TCL_ERROR;
	}
	
ans=Tcl_NewListObj(0, NULL);

for(i=0;i<xvi[adaptor].num_ports;i++)
	Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(xvi[adaptor].base_id+i));
Tcl_SetObjResult(interp, ans);

XvFreeAdaptorInfo(xvi);

return 0;
}

int xv_num_port_encodings(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tk_Window tkwin;
Window win;
Display *d;
int num_encodings;
int port;
XvEncodingInfo *xei;
Tcl_Obj *ans;

Tcl_ResetResult(interp);

if(argc<3){
	Tcl_AppendResult(interp,"ERROR: xv_adaptor_type requires two arguments", NULL);
	return TCL_ERROR;
	}


tkwin=Tk_NameToWindow(interp,argv[1], Tk_MainWindow(interp));

if(tkwin==NULL){
	Tcl_AppendResult(interp,"ERROR: xv_adaptor_type: first argument must be an existing toplevel or frame window", NULL);
	return TCL_ERROR;
	}

d=Tk_Display(tkwin);
win=Tk_WindowId(tkwin);

if((d==NULL)||(win==(Window)NULL)){
	Tcl_AppendResult(interp,"ERROR: xv_adaptor_type: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}

port=atoi(argv[2]);
if((port<0)){
	Tcl_AppendResult(interp,"ERROR: xv_adaptor_type: no such port", NULL);
	return TCL_ERROR;
	}

XvQueryEncodings(d, port, &num_encodings, &xei);

Tcl_SetObjResult(interp, Tcl_NewIntObj(num_encodings));

XvFreeEncodingInfo(xei);

return 0;
}

int xv_port_encoding_name(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tk_Window tkwin;
Window win;
Display *d;
int encoding;
int num_encodings;
int port;
XvEncodingInfo *xei;
Tcl_Obj *ans;

Tcl_ResetResult(interp);

if(argc<4){
	Tcl_AppendResult(interp,"ERROR: xv_encoding_name requires three arguments", NULL);
	return TCL_ERROR;
	}


tkwin=Tk_NameToWindow(interp,argv[1], Tk_MainWindow(interp));

if(tkwin==NULL){
	Tcl_AppendResult(interp,"ERROR: xv_encoding_name: first argument must be an existing toplevel or frame window", NULL);
	return TCL_ERROR;
	}

d=Tk_Display(tkwin);
win=Tk_WindowId(tkwin);

if((d==NULL)||(win==(Window)NULL)){
	Tcl_AppendResult(interp,"ERROR: xv_encoding_name: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}

port=atoi(argv[2]);
if((port<0)){
	Tcl_AppendResult(interp,"ERROR: xv_encoding_name: no such port", NULL);
	return TCL_ERROR;
	}
encoding=atoi(argv[3]);

XvQueryEncodings(d, port, &num_encodings, &xei);

if((encoding<0)||(encoding>=num_encodings)){
	XvFreeEncodingInfo(xei);
	Tcl_AppendResult(interp,"ERROR: xv_encoding_name: no such encoding", NULL);
	return TCL_ERROR;
	}

Tcl_AppendResult(interp,xei[encoding].name, NULL);

XvFreeEncodingInfo(xei);

return 0;
}

int xv_port_encoding_id(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tk_Window tkwin;
Window win;
Display *d;
int encoding;
int num_encodings;
int port;
XvEncodingInfo *xei;
Tcl_Obj *ans;

Tcl_ResetResult(interp);

if(argc<4){
	Tcl_AppendResult(interp,"ERROR: xv_port_encoding_id requires three arguments", NULL);
	return TCL_ERROR;
	}


tkwin=Tk_NameToWindow(interp,argv[1], Tk_MainWindow(interp));

if(tkwin==NULL){
	Tcl_AppendResult(interp,"ERROR: xv_port_encoding_id: first argument must be an existing toplevel or frame window", NULL);
	return TCL_ERROR;
	}

d=Tk_Display(tkwin);
win=Tk_WindowId(tkwin);

if((d==NULL)||(win==(Window)NULL)){
	Tcl_AppendResult(interp,"ERROR: xv_port_encoding_id: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}

port=atoi(argv[2]);
if((port<0)){
	Tcl_AppendResult(interp,"ERROR: xv_port_encoding_id: no such port", NULL);
	return TCL_ERROR;
	}
encoding=atoi(argv[3]);

XvQueryEncodings(d, port, &num_encodings, &xei);

if((encoding<0)||(encoding>=num_encodings)){
	XvFreeEncodingInfo(xei);
	Tcl_AppendResult(interp,"ERROR: xv_port_encoding_id: no such encoding", NULL);
	return TCL_ERROR;
	}


Tcl_SetObjResult(interp, Tcl_NewIntObj(xei[encoding].encoding_id));

XvFreeEncodingInfo(xei);

return 0;
}

int xv_port_encoding_size(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tk_Window tkwin;
Window win;
Display *d;
int encoding;
int num_encodings;
int port;
XvEncodingInfo *xei;
Tcl_Obj *ans;

Tcl_ResetResult(interp);

if(argc<4){
	Tcl_AppendResult(interp,"ERROR: xv_port_encoding_size requires three arguments", NULL);
	return TCL_ERROR;
	}


tkwin=Tk_NameToWindow(interp,argv[1], Tk_MainWindow(interp));

if(tkwin==NULL){
	Tcl_AppendResult(interp,"ERROR: xv_port_encoding_size: first argument must be an existing toplevel or frame window", NULL);
	return TCL_ERROR;
	}

d=Tk_Display(tkwin);
win=Tk_WindowId(tkwin);

if((d==NULL)||(win==(Window)NULL)){
	Tcl_AppendResult(interp,"ERROR: xv_port_encoding_size: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}

port=atoi(argv[2]);
if((port<0)){
	Tcl_AppendResult(interp,"ERROR: xv_port_encoding_size: no such port", NULL);
	return TCL_ERROR;
	}
encoding=atoi(argv[3]);

XvQueryEncodings(d, port, &num_encodings, &xei);

if((encoding<0)||(encoding>=num_encodings)){
	XvFreeEncodingInfo(xei);
	Tcl_AppendResult(interp,"ERROR: xv_port_encoding_size: no such encoding", NULL);
	return TCL_ERROR;
	}
ans=Tcl_NewListObj(0,NULL);

Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(xei[encoding].width));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(xei[encoding].height));

Tcl_SetObjResult(interp, ans);


XvFreeEncodingInfo(xei);

return 0;
}


int xv_num_port_attributes(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tk_Window tkwin;
Window win;
Display *d;
int num_attributes;
int port;
XvAttribute *xa;
Tcl_Obj *ans;
int i;

Tcl_ResetResult(interp);

if(argc<3){
	Tcl_AppendResult(interp,"ERROR: xv_num_port_attributes requires two arguments", NULL);
	return TCL_ERROR;
	}


tkwin=Tk_NameToWindow(interp,argv[1], Tk_MainWindow(interp));

if(tkwin==NULL){
	Tcl_AppendResult(interp,"ERROR: xv_num_port_attributes: first argument must be an existing toplevel or frame window", NULL);
	return TCL_ERROR;
	}

d=Tk_Display(tkwin);
win=Tk_WindowId(tkwin);

if((d==NULL)||(win==(Window)NULL)){
	Tcl_AppendResult(interp,"ERROR: xv_num_port_attributes: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}

port=atoi(argv[2]);
if((port<0)){
	Tcl_AppendResult(interp,"ERROR: xv_num_port_attributes: no such port", NULL);
	return TCL_ERROR;
	}

xa=XvQueryPortAttributes(d, port, &num_attributes);

Tcl_SetObjResult(interp, Tcl_NewIntObj(num_attributes));

if(xa)XFree(xa);

return 0;
}

int xv_port_attribute_name(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tk_Window tkwin;
Window win;
Display *d;
int attribute;
int num_attributes;
int port;
XvAttribute *xa;
Tcl_Obj *ans;

Tcl_ResetResult(interp);

if(argc<4){
	Tcl_AppendResult(interp,"ERROR: xv_port_attribute_name requires three arguments", NULL);
	return TCL_ERROR;
	}


tkwin=Tk_NameToWindow(interp,argv[1], Tk_MainWindow(interp));

if(tkwin==NULL){
	Tcl_AppendResult(interp,"ERROR: xv_port_attribute_name: first argument must be an existing toplevel or frame window", NULL);
	return TCL_ERROR;
	}

d=Tk_Display(tkwin);
win=Tk_WindowId(tkwin);

if((d==NULL)||(win==(Window)NULL)){
	Tcl_AppendResult(interp,"ERROR: xv_port_attribute_name: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}

port=atoi(argv[2]);
if((port<0)){
	Tcl_AppendResult(interp,"ERROR: xv_port_attribute_name: no such port", NULL);
	return TCL_ERROR;
	}
attribute=atoi(argv[3]);

xa=XvQueryPortAttributes(d, port, &num_attributes);

if((attribute<0)||(attribute>=num_attributes)){
	if(xa)XFree(xa);
	Tcl_AppendResult(interp,"ERROR: xv_port_attribute_name: no such attribute", NULL);
	return TCL_ERROR;
	}

Tcl_AppendResult(interp,xa[attribute].name, NULL);

if(xa)XFree(xa);

return 0;
}

int xv_port_attribute_type(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tk_Window tkwin;
Window win;
Display *d;
int attribute;
int num_attributes;
int port;
XvAttribute *xa;
Tcl_Obj *ans;

Tcl_ResetResult(interp);

if(argc<4){
	Tcl_AppendResult(interp,"ERROR: xv_port_attribute_type requires three arguments", NULL);
	return TCL_ERROR;
	}


tkwin=Tk_NameToWindow(interp,argv[1], Tk_MainWindow(interp));

if(tkwin==NULL){
	Tcl_AppendResult(interp,"ERROR: xv_port_attribute_type: first argument must be an existing toplevel or frame window", NULL);
	return TCL_ERROR;
	}

d=Tk_Display(tkwin);
win=Tk_WindowId(tkwin);

if((d==NULL)||(win==(Window)NULL)){
	Tcl_AppendResult(interp,"ERROR: xv_port_attribute_type: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}

port=atoi(argv[2]);
if((port<0)){
	Tcl_AppendResult(interp,"ERROR: xv_port_attribute_type: no such port", NULL);
	return TCL_ERROR;
	}
attribute=atoi(argv[3]);

xa=XvQueryPortAttributes(d, port, &num_attributes);

if((attribute<0)||(attribute>=num_attributes)){
	if(xa)XFree(xa);
	Tcl_AppendResult(interp,"ERROR: xv_port_attribute_type: no such attribute", NULL);
	return TCL_ERROR;
	}


ans=Tcl_NewListObj(0, NULL);

if(xa[attribute].flags & XvGettable)Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("gettable", 8));

if(xa[attribute].flags & XvSettable)Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("settable", 8));

Tcl_SetObjResult(interp, ans);


if(xa)XFree(xa);

return 0;
}

int xv_port_attribute_range(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tk_Window tkwin;
Window win;
Display *d;
int attribute;
int num_attributes;
int port;
XvAttribute *xa;
Tcl_Obj *ans;

Tcl_ResetResult(interp);

if(argc<4){
	Tcl_AppendResult(interp,"ERROR: xv_port_attribute_range requires three arguments", NULL);
	return TCL_ERROR;
	}


tkwin=Tk_NameToWindow(interp,argv[1], Tk_MainWindow(interp));

if(tkwin==NULL){
	Tcl_AppendResult(interp,"ERROR: xv_port_attribute_range: first argument must be an existing toplevel or frame window", NULL);
	return TCL_ERROR;
	}

d=Tk_Display(tkwin);
win=Tk_WindowId(tkwin);

if((d==NULL)||(win==(Window)NULL)){
	Tcl_AppendResult(interp,"ERROR: xv_port_attribute_range: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}

port=atoi(argv[2]);
if((port<0)){
	Tcl_AppendResult(interp,"ERROR: xv_port_attribute_range: no such port", NULL);
	return TCL_ERROR;
	}
attribute=atoi(argv[3]);

xa=XvQueryPortAttributes(d, port, &num_attributes);

if((attribute<0)||(attribute>=num_attributes)){
	if(xa)XFree(xa);
	Tcl_AppendResult(interp,"ERROR: xv_port_attribute_range: no such attribute", NULL);
	return TCL_ERROR;
	}


ans=Tcl_NewListObj(0, NULL);

Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(xa[attribute].min_value));

Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(xa[attribute].max_value));

Tcl_SetObjResult(interp, ans);


if(xa)XFree(xa);

return 0;
}



int xv_putvideo(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tk_Window tkwin;
Window win;
Display *d;
XvPortID port;
long x,y,w,h;
XGCValues xgcv;
GC gc;
int r;
XRectangle rect;

Tcl_ResetResult(interp);

if(argc<6){
	Tcl_AppendResult(interp,"ERROR: xv_putvideo requires 7 arguments", NULL);
	return TCL_ERROR;
	}

tkwin=Tk_NameToWindow(interp,argv[1], Tk_MainWindow(interp));

if(tkwin==NULL){
	Tcl_AppendResult(interp,"ERROR: xv_putvideo: first argument must be an existing toplevel or frame window", NULL);
	return TCL_ERROR;
	}

d=Tk_Display(tkwin);
win=Tk_WindowId(tkwin);

if((d==NULL)||(win==(Window)NULL)){
	Tcl_AppendResult(interp,"ERROR: xv_putvideo: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}


port=atoi(argv[2]);
x=atoi(argv[3]);
y=atoi(argv[4]);
w=atoi(argv[5]);
h=atoi(argv[6]);

xgcv.subwindow_mode=IncludeInferiors;
xgcv.clip_x_origin=0;
xgcv.clip_y_origin=0;
/*
Tk_CreateEventHandler(tkwin, XvVideoNotify, xv_notify_handler, NULL);
*/
gc=Tk_GetGC(tkwin, GCSubwindowMode, &xgcv);

XvPutVideo(d, port, win, gc, x, y, w, h, 0, 0, Tk_Width(tkwin), Tk_Height(tkwin));
Tk_FreeGC(d, gc);
return 0;
}

int xv_getportattribute(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tk_Window tkwin;
Window win;
Display *d;
XvPortID port;
Atom attr_atom;
int value;

Tcl_ResetResult(interp);

if(argc<4){
	Tcl_AppendResult(interp,"ERROR: xv_getportattribute requires 3 arguments", NULL);
	return TCL_ERROR;
	}

tkwin=Tk_NameToWindow(interp,argv[1], Tk_MainWindow(interp));

if(tkwin==NULL){
	Tcl_AppendResult(interp,"ERROR: xv_getportattribute: first argument must be an existing toplevel or frame window", NULL);
	return TCL_ERROR;
	}

d=Tk_Display(tkwin);
win=Tk_WindowId(tkwin);

if((d==NULL)||(win==(Window)NULL)){
	Tcl_AppendResult(interp,"ERROR: xv_getportattribute: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}

port=atoi(argv[2]);
attr_atom=XInternAtom(d, argv[3], 0);
XvGetPortAttribute(d, port, attr_atom, &value);

Tcl_SetObjResult(interp, Tcl_NewIntObj(value));

return 0;
}

int xv_setportattribute(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tk_Window tkwin;
Window win;
Display *d;
XvPortID port;
Atom attr_atom;
int value;

Tcl_ResetResult(interp);

if(argc<5){
	Tcl_AppendResult(interp,"ERROR: xv_setportattribute requires 4 arguments", NULL);
	return TCL_ERROR;
	}

tkwin=Tk_NameToWindow(interp,argv[1], Tk_MainWindow(interp));

if(tkwin==NULL){
	Tcl_AppendResult(interp,"ERROR: xv_setportattribute: first argument must be an existing toplevel or frame window", NULL);
	return TCL_ERROR;
	}

d=Tk_Display(tkwin);
win=Tk_WindowId(tkwin);

if((d==NULL)||(win==(Window)NULL)){
	Tcl_AppendResult(interp,"ERROR: xv_setportattribute: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}

port=atoi(argv[2]);
attr_atom=XInternAtom(d, argv[3], 0);
value=atoi(argv[4]);
XvSetPortAttribute(d, port, attr_atom, value);

return 0;
}

int xv_getwindowbackgroundpixel(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tk_Window tkwin;
Window win;
Display *d;
Screen *s;
int value;
XSetWindowAttributes *xswa;

Tcl_ResetResult(interp);

if(argc<2){
	Tcl_AppendResult(interp,"ERROR: xv_getwindowbackgroundpixel requires one argument", NULL);
	return TCL_ERROR;
	}

tkwin=Tk_NameToWindow(interp,argv[1], Tk_MainWindow(interp));

if(tkwin==NULL){
	Tcl_AppendResult(interp,"ERROR: xv_getwindowbackgroundpixel: first argument must be an existing toplevel or frame window", NULL);
	return TCL_ERROR;
	}

xswa=Tk_Attributes(tkwin);
if((xswa==NULL)){
	Tcl_AppendResult(interp,"ERROR: xv_getwindowbackgroundpixel: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}

Tcl_SetObjResult(interp, Tcl_NewIntObj(xswa->background_pixel));
return 0;
}



struct {
	char *name;
	Tcl_CmdProc *command;
	} xv_commands[]={
	{"xv_numadaptors", xv_numadaptors},
	{"xv_adaptor_name", xv_adaptor_name},
	{"xv_adaptor_type", xv_adaptor_type},
	{"xv_adaptor_ports", xv_adaptor_ports},
	{"xv_num_port_encodings", xv_num_port_encodings},
	{"xv_port_encoding_name", xv_port_encoding_name},
	{"xv_port_encoding_id", xv_port_encoding_id},
	{"xv_port_encoding_size", xv_port_encoding_size},
	{"xv_num_port_attributes", xv_num_port_attributes},
	{"xv_port_attribute_name", xv_port_attribute_name},
	{"xv_port_attribute_type", xv_port_attribute_type},
	{"xv_port_attribute_range", xv_port_attribute_range},
	{"xv_putvideo", xv_putvideo},
	{"xv_getportattribute", xv_getportattribute},
	{"xv_setportattribute", xv_setportattribute},
	{"xv_getwindowbackgroundpixel", xv_getwindowbackgroundpixel},
	{NULL, NULL}
	};

void init_xv(Tcl_Interp *interp)
{
long i;
for(i=0;xv_commands[i].name!=NULL;i++)
	Tcl_CreateCommand(interp, xv_commands[i].name, xv_commands[i].command, (ClientData)0, NULL);
}
