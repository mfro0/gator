/*     avview preliminary version

       (C) Vladimir Dergachev 2001-2003
       
       GNU Public License
       
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include <tcl.h>


int linux_getrlimit(ClientData client_data,Tcl_Interp* interp,int argc,const char *argv[])
{
Tcl_Obj *ans;
int resource;
struct rlimit rlim;

Tcl_ResetResult(interp);

if(argc<2){
	Tcl_AppendResult(interp,"ERROR: linux_getrlimit requires one argument", NULL);
	return TCL_ERROR;
	}
	
if(!strcmp(argv[1], "RLIMIT_CPU"))resource=RLIMIT_CPU;
	else
if(!strcmp(argv[1], "RLIMIT_DATA"))resource=RLIMIT_DATA;
	else
if(!strcmp(argv[1], "RLIMIT_FSIZE"))resource=RLIMIT_FSIZE;
	else
if(!strcmp(argv[1], "RLIMIT_STACK"))resource=RLIMIT_STACK;
	else
if(!strcmp(argv[1], "RLIMIT_CORE"))resource=RLIMIT_CORE;
	else
if(!strcmp(argv[1], "RLIMIT_RSS"))resource=RLIMIT_RSS;
	else
if(!strcmp(argv[1], "RLIMIT_NPROC"))resource=RLIMIT_NPROC;
	else
if(!strcmp(argv[1], "RLIMIT_NOFILE"))resource=RLIMIT_NOFILE;
	else
if(!strcmp(argv[1], "RLIMIT_MEMLOCK"))resource=RLIMIT_MEMLOCK;
	else
	resource=RLIMIT_DATA;
if(getrlimit(resource, &rlim)<0){	
	Tcl_AppendResult(interp,"RUNTIME ERROR: linux_getrlimit: ", NULL);
	Tcl_AppendResult(interp,strerror(errno), NULL);
	return TCL_ERROR;
	}	
ans=Tcl_NewListObj(0, NULL);
if(rlim.rlim_cur!=RLIM_INFINITY)Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(rlim.rlim_cur));
	else
	Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("infinity", -1));
if(rlim.rlim_max!=RLIM_INFINITY)Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(rlim.rlim_max));
	else
	Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("infinity", -1));
Tcl_SetObjResult(interp, ans);
return TCL_OK;	
}


struct {
	char *name;
	Tcl_CmdProc *command;
	} linux_commands[]={
	{"linux_getrlimit", linux_getrlimit},
	{NULL, NULL}
	};

void init_linux(Tcl_Interp *interp)
{
long i;
for(i=0;linux_commands[i].name!=NULL;i++)
	Tcl_CreateCommand(interp, linux_commands[i].name, linux_commands[i].command, (ClientData)0, NULL);
}
