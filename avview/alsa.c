/*     avview preliminary version

       (C) Vladimir Dergachev 2001
       
       GNU Public License
       
*/


#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <tcl.h>
#include <tk.h>
#include "string_cache.h"

#include "config.h"

#if USE_ALSA

#include <alsa/asoundlib.h>

STRING_CACHE *alsa_sc;

snd_pcm_info_t *pcminfo;
snd_rawmidi_info_t *rawmidiinfo;

typedef struct {
	snd_ctl_t *ctl;
	snd_ctl_card_info_t *info;
	snd_ctl_elem_list_t *elist;
	snd_ctl_elem_info_t *einfo;
	} ALSA_DATA;

#include "global.h"
#include "alsa.h"

int alsa_card_get_name(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
char *card_name;
int a;
Tcl_ResetResult(interp);
if(argc<2){
	Tcl_AppendResult(interp,"ERROR: alsa_card_get_name requires one argument", NULL);
	return TCL_ERROR;
	}
if((a=snd_card_get_name(atoi(argv[1]), &card_name))<0){
	Tcl_AppendResult(interp,"ERROR: alsa_card_get_name: ", NULL);
	Tcl_AppendResult(interp, snd_strerror(a), NULL);
	return TCL_ERROR;
	}
Tcl_AppendResult(interp, card_name, NULL);
return 0;
}

int alsa_card_next(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
int next_card;
int a;
Tcl_ResetResult(interp);
if(argc<2){
	Tcl_AppendResult(interp,"ERROR: alsa_card_next requires one argument", NULL);
	return TCL_ERROR;
	}
if((a=snd_card_get_name(atoi(argv[1]), &next_card))<0){
	Tcl_AppendResult(interp,"ERROR: alsa_card_next: ", NULL);
	Tcl_AppendResult(interp, snd_strerror(a), NULL);
	return TCL_ERROR;
	}
Tcl_SetObjResult(interp, Tcl_NewIntObj(next_card));
return 0;
}

int alsa_card_get_longname(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
char *card_longname;
int a;
Tcl_ResetResult(interp);
if(argc<2){
	Tcl_AppendResult(interp,"ERROR: alsa_card_get_longname requires one argument", NULL);
	return TCL_ERROR;
	}
if((a=snd_card_get_longname(atoi(argv[1]), &card_longname))<0){
	Tcl_AppendResult(interp,"ERROR: alsa_card_get_longname: ", NULL);
	Tcl_AppendResult(interp, snd_strerror(a), NULL);
	return TCL_ERROR;
	}
Tcl_AppendResult(interp, card_longname, NULL);
return 0;
}

int alsa_card_load(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
int a;
Tcl_ResetResult(interp);
if(argc<2){
	Tcl_AppendResult(interp,"ERROR: alsa_card_load requires one argument", NULL);
	return TCL_ERROR;
	}
if((a=snd_card_load(atoi(argv[1])))<0){
	Tcl_AppendResult(interp,"ERROR: alsa_card_load: ", NULL);
	Tcl_AppendResult(interp, snd_strerror(a), NULL);
	return TCL_ERROR;
	}
return 0;
}

int alsa_ctl_open(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
int a,i;
int mode;
ALSA_DATA *ad;
Tcl_Obj *ans;

Tcl_ResetResult(interp);
if(argc<3){
	Tcl_AppendResult(interp,"ERROR: alsa_ctl_open requires two arguments", NULL);
	return TCL_ERROR;
	}
i=add_string(alsa_sc, argv[1]);
if(alsa_sc->data[i]!=NULL){
	Tcl_AppendResult(interp,"ERROR: alsa_ctl_open: this control is already open", NULL);
	return TCL_ERROR;
	}
alsa_sc->data[i]=do_alloc(1, sizeof(ALSA_DATA));
mode=0;
ad=(ALSA_DATA*)alsa_sc->data[i];
if((a=snd_ctl_open(&(ad->ctl), argv[1], mode))<0){
	Tcl_AppendResult(interp,"ERROR: alsa_ctl_open: ", NULL);
	Tcl_AppendResult(interp, snd_strerror(a), NULL);
	free(alsa_sc->data[i]);
	alsa_sc->data[i]=NULL;
	return TCL_ERROR;
	}
snd_ctl_card_info_alloca(&(ad->info));
if((a=snd_ctl_card_info(ad->ctl, ad->info))<0){
	Tcl_AppendResult(interp,"ERROR: alsa_ctl_open: ", NULL);
	Tcl_AppendResult(interp, snd_strerror(a), NULL);
	snd_ctl_close(ad->ctl);
	free(ad->info);
	free(alsa_sc->data[i]);
	alsa_sc->data[i]=NULL;
	return TCL_ERROR;
	}
snd_ctl_elem_list_alloca(&(ad->elist));
if((a=snd_ctl_elem_list(ad->ctl, ad->elist))<0){
	Tcl_AppendResult(interp,"ERROR: alsa_ctl_open: ", NULL);
	Tcl_AppendResult(interp, snd_strerror(a), NULL);
	snd_ctl_close(ad->ctl);
	free(ad->elist);
	free(ad->info);
	free(alsa_sc->data[i]);
	alsa_sc->data[i]=NULL;
	return TCL_ERROR;
	}
snd_ctl_elem_info_alloca(&(ad->einfo));
if((a=snd_ctl_elem_list(ad->ctl, ad->einfo))<0){
	Tcl_AppendResult(interp,"ERROR: alsa_ctl_open: ", NULL);
	Tcl_AppendResult(interp, snd_strerror(a), NULL);
	free(ad->einfo);
	snd_ctl_elem_list_free_space(ad->elist);
	snd_ctl_close(ad->ctl);
	free(ad->info);
	free(alsa_sc->data[i]);
	alsa_sc->data[i]=NULL;
	return TCL_ERROR;
	}

ans=Tcl_NewListObj(0, NULL);
/* ok, we can't simply count on the order of entries being fixed */
Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("-card", -1));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(snd_ctl_card_info_get_card(ad->info)));

Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("-id", -1));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj(snd_ctl_card_info_get_id(ad->info), -1));

Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("-driver", -1));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj(snd_ctl_card_info_get_driver(ad->info), -1));

Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("-name", -1));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj(snd_ctl_card_info_get_name(ad->info), -1));

Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("-longname", -1));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj(snd_ctl_card_info_get_longname(ad->info), -1));

Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("-mixer", -1));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj(snd_ctl_card_info_get_mixername(ad->info), -1));

Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("-components", -1));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj(snd_ctl_card_info_get_components(ad->info), -1));

Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("-controls", -1));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(snd_ctl_elem_list_get_count(ad->elist)));

Tcl_SetObjResult(interp, ans);
return TCL_OK;
}


int alsa_ctl_get_element_info(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
int a,i;
int mode;
ALSA_DATA *ad;
Tcl_Obj *ans;
snd_ctl_elem_id_t *elem_id;
snd_ctl_elem_iface_t iface;
snd_ctl_elem_type_t type;
int index;

Tcl_ResetResult(interp);
if(argc<3){
	Tcl_AppendResult(interp,"ERROR: alsa_ctl_get_element_info requires two arguments", NULL);
	return TCL_ERROR;
	}
i=lookup_string(alsa_sc, argv[1]);
if((i<0)||((ad=(ALSA_DATA *)alsa_sc->data[i])==NULL)){
	Tcl_AppendResult(interp,"ERROR: alsa_ctl_get_element_info: no such control open", NULL);
	return TCL_ERROR;
	}
index=atoi(argv[2]);
snd_ctl_elem_id_alloca(&elem_id);
/* snd_ctl_elem_list_get_id(ad->elist, index, elem_id); 
  iface=snd_ctl_elem_id_get_interface(elem_id);
*/
ans=Tcl_NewListObj(0, NULL);
/* ok, we can't simply count on the order of entries being fixed */
Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("-interface", -1));
Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj(snd_ctl_elem_iface_name(snd_ctl_elem_list_get_interface(ad->elist, index)), -1));
/* snd_ctl_elem_id_free(elem_id); */
return TCL_OK;
}

int alsa_present(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tcl_ResetResult(interp);
Tcl_AppendResult(interp,"yes", NULL);
return 0;
}

struct {
	char *name;
	Tcl_CmdProc *command;
	} alsa_commands[]={
	{"alsa_present", alsa_present},
	{"alsa_card_get_name", alsa_card_get_name},
	{"alsa_card_get_longname", alsa_card_get_longname},
	{"alsa_card_next", alsa_card_next},
	{"alsa_card_load", alsa_card_load},
	{"alsa_ctl_open", alsa_ctl_open},
	{"alsa_ctl_get_element_info", alsa_ctl_get_element_info},
	{NULL, NULL}
	};

void init_alsa(Tcl_Interp *interp)
{
long i;

alsa_sc=new_string_cache();

snd_pcm_info_alloca(&pcminfo);
snd_rawmidi_info_alloca(&rawmidiinfo);

for(i=0;alsa_commands[i].name!=NULL;i++)
	Tcl_CreateCommand(interp, alsa_commands[i].name, alsa_commands[i].command, (ClientData)0, NULL);
}
#else /* USE_ALSA */

int alsa_present(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
Tcl_ResetResult(interp);
Tcl_AppendResult(interp,"no", NULL);
return 0;
}

struct {
	char *name;
	Tcl_CmdProc *command;
	} alsa_commands[]={
	{"alsa_present", alsa_present},
	{NULL, NULL}
	};

void init_alsa(Tcl_Interp *interp)
{
long i;

for(i=0;alsa_commands[i].name!=NULL;i++)
	Tcl_CreateCommand(interp, alsa_commands[i].name, alsa_commands[i].command, (ClientData)0, NULL);
}
#endif /* USE_ALSA */

