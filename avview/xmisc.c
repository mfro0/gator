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
#include <X11/extensions/dpms.h>
#include <tcl.h>
#include <tk.h>

int xmisc_getscreensaver(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
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

int xmisc_setscreensaver(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tk_Window tkwin;
Window win;
Display *d;
int timeout, interval, pb, ae;
CARD16 dpms_standby,dpms_suspend,dpms_off,dpms_current_level,a;
BOOL dpms_enabled,b;
Tcl_Obj *ans;

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

int xmisc_hidecursor(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tk_Window tkwin;
Tk_Cursor cursor;
Window win;
Display *d;
int timeout, interval, pb, ae;
Tcl_Obj *ans;
CARD16 dpms_standby,dpms_suspend,dpms_off,dpms_current_level;
BOOL dpms_enabled;
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
	Tcl_AppendResult(interp,"ERROR: xmisc_hidecursor: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}

bits=0x00;
mask=0x00;
cursor=Tk_GetCursorFromData(interp, tkwin, &bits, &mask, 1, 1, 0, 0, Tk_GetUid("red"), Tk_GetUid("blue"));
Tk_DefineCursor(tkwin, cursor);

return 0;
}

int xmisc_setfullscreen(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
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

if((d==NULL)||(win==(Window)NULL)){
	Tcl_AppendResult(interp,"ERROR: xmisc_setfullscreen: first argument must be a mapped toplevel or frame window", NULL);
	return TCL_ERROR;
	}
XUnmapWindow(d, win);
XFlush(d);
XMapRaised(d, win);
XFlush(d);
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
	{NULL, NULL}
	};

void init_xmisc(Tcl_Interp *interp)
{
long i;
for(i=0;xmisc_commands[i].name!=NULL;i++)
	Tcl_CreateCommand(interp, xmisc_commands[i].name, xmisc_commands[i].command, (ClientData)0, NULL);
}
