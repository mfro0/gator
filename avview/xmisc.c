/*     avview preliminary version

       (C) Vladimir Dergachev 2001-2002
       
       GNU Public License
       
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/dpms.h>
#include <X11/Xutil.h>
#include <tcl.h>
#include <tk.h>

int xmisc_getscreensaver(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
Tk_Window tkwin;
Window win;
Display *d;
int timeout, interval, pb, ae;
Tcl_Obj *ans;
CARD16 dpms_standby,dpms_suspend,dpms_off,dpms_current_level;
BOOL dpms_enabled;

Tcl_ResetResult(interp);

if(argc<2){
	Tcl_AppendResult(interp,"ERROR: xmisc_getscreensaver requires one argument", NULL);
	return TCL_ERROR;
	}


tkwin=Tk_NameToWindow(interp,argv[1], Tk_MainWindow(interp));

if(tkwin==NULL){
	Tcl_AppendResult(interp,"ERROR: xmisc_getscreensaver: first argument must be an existing toplevel or frame window", NULL);
	return TCL_ERROR;
	}

d=Tk_Display(tkwin);
win=Tk_WindowId(tkwin);

if((d==NULL)||(win==(Window)NULL)){
	Tcl_AppendResult(interp,"ERROR: xmisc_getscreensaver: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}

XGetScreenSaver(d, &timeout, &interval, &pb, &ae);
DPMSGetTimeouts(d, &dpms_standby, &dpms_suspend, &dpms_off);
DPMSInfo(d, &dpms_current_level, &dpms_enabled);

ans=Tcl_NewListObj(0, NULL);

Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(timeout));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(interval));
switch(pb){
	case DontPreferBlanking:
		Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("DontPreferBlanking", 18));
		break;
	case PreferBlanking:
		Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("PreferBlanking", 14));
		break;
	case DefaultBlanking:
		Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("DefaultBlanking", 15));
		break;
	}
switch(ae){
	case DontAllowExposures:
		Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("DontAllowExposures", 18));
		break;
	case AllowExposures:
		Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("AllowExposures", 14));
		break;
	case DefaultExposures:
		Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("DefaultExposures", 16));
		break;
	}

Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(dpms_standby));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(dpms_suspend));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(dpms_off));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(dpms_current_level));
if(dpms_enabled)Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("on",2));
	else Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("off",3));

Tcl_SetObjResult(interp, ans);


return 0;
}

int xmisc_setscreensaver(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
Tk_Window tkwin;
Window win;
Display *d;
int timeout, interval, pb, ae;
CARD16 dpms_standby,dpms_suspend,dpms_off,dpms_current_level,a;
BOOL b;

Tcl_ResetResult(interp);

if(argc<11){
	Tcl_AppendResult(interp,"ERROR: xmisc_setscreensaver requires ten arguments", NULL);
	return TCL_ERROR;
	}


tkwin=Tk_NameToWindow(interp,argv[1], Tk_MainWindow(interp));

if(tkwin==NULL){
	Tcl_AppendResult(interp,"ERROR: xmisc_setscreensaver: first argument must be an existing toplevel or frame window", NULL);
	return TCL_ERROR;
	}

d=Tk_Display(tkwin);
win=Tk_WindowId(tkwin);

if((d==NULL)||(win==(Window)NULL)){
	Tcl_AppendResult(interp,"ERROR: xmisc_setscreensaver: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}
timeout=atoi(argv[2]);
interval=atoi(argv[3]);
pb=DefaultBlanking;
if(!strcmp("DontPreferBlanking", argv[4]))pb=DontPreferBlanking;
if(!strcmp("PreferBlanking", argv[4]))pb=PreferBlanking;
if(!strcmp("DefaultBlanking", argv[4]))pb=DefaultBlanking;

ae=DefaultExposures;
if(!strcmp("DontAllowExposures", argv[5]))ae=DontAllowExposures;
if(!strcmp("AllowExposures", argv[5]))ae=AllowExposures;
if(!strcmp("DefaultExposures", argv[5]))ae=DefaultExposures;

XSetScreenSaver(d, timeout, interval, pb, ae);

dpms_standby=atoi(argv[6]);
dpms_suspend=atoi(argv[7]);
dpms_off=atoi(argv[8]);

DPMSSetTimeouts(d, dpms_standby, dpms_suspend, dpms_off);


if(!strcmp("on",argv[10]))DPMSEnable(d);
if(!strcmp("off",argv[10]))DPMSDisable(d);

/* obtain current settings */
DPMSInfo(d, &a, &b);

dpms_current_level=atoi(argv[9]);
if(b && (a!=dpms_current_level))DPMSForceLevel(d, dpms_current_level);

return 0;
}

int xmisc_hidecursor(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
Tk_Window tkwin;
Tk_Cursor cursor;
Window win;
Display *d;
char bits,mask;

Tcl_ResetResult(interp);

if(argc<2){
	Tcl_AppendResult(interp,"ERROR: xmisc_hidecursor requires one argument", NULL);
	return TCL_ERROR;
	}


tkwin=Tk_NameToWindow(interp,argv[1], Tk_MainWindow(interp));

if(tkwin==NULL){
	Tcl_AppendResult(interp,"ERROR: xmisc_hidecursor: first argument must be an existing toplevel or frame window", NULL);
	return TCL_ERROR;
	}

d=Tk_Display(tkwin);
win=Tk_WindowId(tkwin);
if((d==NULL)||(win==(Window)NULL)){
	Tk_MapWindow(tkwin);
	d=Tk_Display(tkwin);
	win=Tk_WindowId(tkwin);
	}

if((d==NULL)||(win==(Window)NULL)){
	Tcl_AppendResult(interp,"ERROR: xmisc_hidecursor: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}

bits=0x00;
mask=0x00;
cursor=Tk_GetCursorFromData(interp, tkwin, &bits, &mask, 1, 1, 0, 0, Tk_GetUid("red"), Tk_GetUid("blue"));
Tk_DefineCursor(tkwin, cursor);

return 0;
}

int xmisc_setfullscreen(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
Tk_Window tkwin;
Window win;
Display *d;
Screen *s;
XWindowAttributes xwa;
XSetWindowAttributes xswa;

Tcl_ResetResult(interp);

if(argc<2){
	Tcl_AppendResult(interp,"ERROR: xmisc_setfullscreen requires one argument", NULL);
	return TCL_ERROR;
	}


tkwin=Tk_NameToWindow(interp,argv[1], Tk_MainWindow(interp));

if(tkwin==NULL){
	Tcl_AppendResult(interp,"ERROR: xmisc_setfullscreen: first argument must be an existing toplevel or frame window", NULL);
	return TCL_ERROR;
	}

d=Tk_Display(tkwin);
win=Tk_WindowId(tkwin);
s=Tk_Screen(tkwin);
if((d==NULL)||(win==(Window)NULL)||(s==NULL)){
	Tcl_AppendResult(interp,"ERROR: xmisc_setfullscreen: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}
XUnmapWindow(d, win);
XSync(d, False);
XGetWindowAttributes(d, win, &xwa);
while(xwa.map_state != IsUnmapped){
	XSync(d, False);
	XGetWindowAttributes(d, win, &xwa);
	}
xswa.override_redirect=True;
XChangeWindowAttributes(d, win, CWOverrideRedirect, &xswa);
XSync(d, False);
XGetWindowAttributes(d, win, &xwa);
while(!xwa.override_redirect){
	XSync(d, False);
	XGetWindowAttributes(d, win, &xwa);
	}
XMapRaised(d, win);
XSync(d, False);
XGetWindowAttributes(d, win, &xwa);
while((xwa.map_state == IsUnmapped)){
	XSync(d, False);
	XGetWindowAttributes(d, win, &xwa);
	}
Tk_MoveWindow(tkwin, 0,0);
Tk_ResizeWindow(tkwin, WidthOfScreen(s), HeightOfScreen(s));
return TCL_OK;
}

#if 0
#include "avview-16x16.2.icon"

struct {
	char *name;
	long size;
	unsigned long *data; }  icons[]={
	{ "avview1", sizeof(avview1), avview1},
	{ NULL, 0, NULL}
	};
#endif

int xmisc_seticon(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
Tk_Window tkwin;
Window win,win2;
Display *d;
Screen *s;
int i,j;
Atom property, type;
Tk_PhotoImageBlock pib;
Tk_PhotoHandle image;
unsigned long *data,*p;
unsigned char *a,*r,*g,*b;

Tcl_ResetResult(interp);

if(argc<3){
	Tcl_AppendResult(interp,"ERROR: xmisc_seticon requires aat least two arguments", NULL);
	return TCL_ERROR;
	}


tkwin=Tk_NameToWindow(interp,argv[1], Tk_MainWindow(interp));

if(tkwin==NULL){
	Tcl_AppendResult(interp,"ERROR: xmisc_seticon: first argument must be an existing toplevel or frame window", NULL);
	return TCL_ERROR;
	}

d=Tk_Display(tkwin);
win=Tk_WindowId(tkwin);
s=Tk_Screen(tkwin);
if((d==NULL)||(win==(Window)NULL)||(s==NULL)){
	Tcl_AppendResult(interp,"ERROR: xmisc_seticon: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}
image=Tk_FindPhoto(interp, argv[2]);
if(image==NULL){
	Tcl_AppendResult(interp,"ERROR: xmisc_seticon: second argument does not refer to valid existing photo image", NULL);
	return TCL_ERROR;
	}
Tk_PhotoGetImage(image, &pib);
data=alloca(pib.width*pib.height*4+8);
data[0]=pib.width;
data[1]=pib.height;
for(j=0;j<pib.height;j++){
	a=&(pib.pixelPtr[j*pib.pitch]);
	r=&(a[pib.offset[0]]);
	g=&(a[pib.offset[1]]);
	b=&(a[pib.offset[2]]);
	a=&(a[pib.offset[3]]);
	p=&(data[2+j*pib.width]);
	for(i=0;i<pib.width;i++){
		*p=(*a<<24)|(*r<<16)|(*g<<8)|*b;
		p++;
		a+=pib.pixelSize;
		r+=pib.pixelSize;
		g+=pib.pixelSize;
		b+=pib.pixelSize;		
		}
	}

win2=0;
if(argc>3)win2=atol(argv[3]);
if(win2==0)win2=win;
property=Tk_InternAtom(tkwin, "_NET_WM_ICON");
type=Tk_InternAtom(tkwin, "CARDINAL");
XSync(d, False);
XChangeProperty(d, win2, property, type, 32, PropModeReplace, (unsigned char *)data, pib.width*pib.height+2);
XSync(d, False);
return TCL_OK;
}

int xmisc_settextproperty(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
Tk_Window tkwin;
Window win;
Display *d;
Screen *s;
Atom property;
XTextProperty xtp;

Tcl_ResetResult(interp);

if(argc<3){
	Tcl_AppendResult(interp,"ERROR: xmisc_settextproperty requires at least two arguments", NULL);
	return TCL_ERROR;
	}


tkwin=Tk_NameToWindow(interp,argv[1], Tk_MainWindow(interp));

if(tkwin==NULL){
	Tcl_AppendResult(interp,"ERROR: xmisc_settextproperty: first argument must be an existing toplevel or frame window", NULL);
	return TCL_ERROR;
	}

d=Tk_Display(tkwin);
win=Tk_WindowId(tkwin);
s=Tk_Screen(tkwin);
fprintf(stderr,"%d %d\n", Tk_IsTopLevel(tkwin), Tk_IsEmbedded(tkwin));
if((d==NULL)||(win==None)||(s==NULL)){
	Tcl_AppendResult(interp,"ERROR: xmisc_settextproperty: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}
property=Tk_InternAtom(tkwin, argv[2]);
XStringListToTextProperty((char **)&(argv[3]), argc-3, &xtp);
XSync(d, False);
XSetTextProperty(d, win, &xtp, property);
XSync(d, False);
XFree(xtp.value);
return TCL_OK;
}


int xmisc_querytree(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
Tk_Window tkwin;
Window win,win2;
Display *d;
Screen *s;
int i;
Window root, parent, *children;
unsigned int n_children;
Tcl_Obj *ans, *c;

Tcl_ResetResult(interp);

if(argc<2){
	Tcl_AppendResult(interp,"ERROR: xmisc_querytree requires at least one argument", NULL);
	return TCL_ERROR;
	}
tkwin=Tk_NameToWindow(interp,argv[1], Tk_MainWindow(interp));

if(tkwin==NULL){
	Tcl_AppendResult(interp,"ERROR: xmisc_seticon: first argument must be an existing toplevel or frame window", NULL);
	return TCL_ERROR;
	}

d=Tk_Display(tkwin);
win=Tk_WindowId(tkwin);
s=Tk_Screen(tkwin);
if((d==NULL)||(win==(Window)NULL)||(s==NULL)){
	Tcl_AppendResult(interp,"ERROR: xmisc_seticon: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}
win2=None;
if(argc>2)win2=atol(argv[2]);
if(win2==None)win2=win;
if(!XQueryTree(d, win2, &root, &parent, &children, &n_children)){
	Tcl_AppendResult(interp,"ERROR: xmisc_seticon: XQueryTree failed", NULL);
	return TCL_ERROR;
	}
ans=Tcl_NewListObj(0, NULL);

Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("root", -1));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(root));

Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("parent", -1));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(parent));

Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("children", -1));

c=Tcl_NewListObj(0, NULL);
for(i=0;i<n_children;i++){
	Tcl_ListObjAppendElement(interp, c, Tcl_NewIntObj(children[i]));	
	}
Tcl_ListObjAppendElement(interp, ans, c);

Tcl_SetObjResult(interp, ans);
return TCL_OK;
}

struct {
	char *name;
	Tcl_CmdProc *command;
	} xmisc_commands[]={
	{"xmisc_getscreensaver", xmisc_getscreensaver},
	{"xmisc_setscreensaver", xmisc_setscreensaver},	
	{"xmisc_hidecursor", xmisc_hidecursor},
	{"xmisc_setfullscreen", xmisc_setfullscreen},
	{"xmisc_seticon", xmisc_seticon},
	{"xmisc_settextproperty", xmisc_settextproperty},
	{"xmisc_querytree", xmisc_querytree},
	{NULL, NULL}
	};

void init_xmisc(Tcl_Interp *interp)
{
long i;
for(i=0;xmisc_commands[i].name!=NULL;i++)
	Tcl_CreateCommand(interp, xmisc_commands[i].name, xmisc_commands[i].command, (ClientData)0, NULL);
}
