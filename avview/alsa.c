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
	snd_hctl_t *hctl;
	long elem_count;
	snd_hctl_elem_t **elem;
	snd_ctl_elem_info_t **einfo;
	snd_ctl_elem_type_t *etype;
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
next_card=atoi(argv[1]);
if((a=snd_card_next(&next_card))<0){
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
Tcl_SetObjResult(interp, Tcl_NewIntObj(a));
return 0;
}

int alsa_hctl_open(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
int a,i;
int mode;
ALSA_DATA *ad;
Tcl_Obj *ans;

Tcl_ResetResult(interp);
if(argc<3){
	Tcl_AppendResult(interp,"ERROR: alsa_hctl_open requires two arguments", NULL);
	return TCL_ERROR;
	}
i=add_string(alsa_sc, argv[1]);
if(alsa_sc->data[i]!=NULL){
	Tcl_AppendResult(interp,"ERROR: alsa_hctl_open: this control is already open", NULL);
	return TCL_ERROR;
	}
alsa_sc->data[i]=do_alloc(1, sizeof(ALSA_DATA));
mode=0;
ad=(ALSA_DATA*)alsa_sc->data[i];
if((a=snd_hctl_open(&(ad->hctl), argv[1], mode))<0){
	Tcl_AppendResult(interp,"ERROR: alsa_hctl_open: ", NULL);
	Tcl_AppendResult(interp, snd_strerror(a), NULL);
	free(alsa_sc->data[i]);
	alsa_sc->data[i]=NULL;
	return TCL_ERROR;
	}
if((a=snd_hctl_load(ad->hctl))<0){
	Tcl_AppendResult(interp,"ERROR: alsa_hctl_open: ", NULL);
	Tcl_AppendResult(interp, snd_strerror(a), NULL);
	snd_hctl_close(ad->hctl);
	free(alsa_sc->data[i]);
	alsa_sc->data[i]=NULL;
	return TCL_ERROR;
	}
ad->elem=NULL;
ad->einfo=NULL;
ad->etype=NULL;
ad->elem_count=-1;
return TCL_OK;
}

int alsa_hctl_close(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
int a,i,j,k,items;
int mode;
ALSA_DATA *ad;
Tcl_Obj *ans,*list,*list2;
int count;
snd_hctl_elem_t *elem;

Tcl_ResetResult(interp);
if(argc<2){
	Tcl_AppendResult(interp,"ERROR: alsa_hctl_close requires one argument", NULL);
	return TCL_ERROR;
	}
i=lookup_string(alsa_sc, argv[1]);
if((i<0)||((ad=(ALSA_DATA *)alsa_sc->data[i])==NULL)){
	return TCL_OK;
	}
if(ad->elem_count>0){
	for(k=0;k<ad->elem_count;k++){
		snd_ctl_elem_info_free(ad->einfo[k]);
		}
	free(ad->einfo);
	free(ad->elem);
	free(ad->etype);
	ad->elem_count=0;
	ad->einfo=NULL;
	ad->etype=NULL;
	}
snd_hctl_close(ad->hctl);
free(ad);
alsa_sc->data[i]=NULL;
return TCL_OK;
}

int alsa_hctl_get_elements_info(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
int a,i,j,k,items;
int mode;
ALSA_DATA *ad;
Tcl_Obj *ans,*list,*list2;
int count;
snd_hctl_elem_t *elem;

Tcl_ResetResult(interp);
if(argc<2){
	Tcl_AppendResult(interp,"ERROR: alsa_ctl_get_elements_info requires one argument", NULL);
	return TCL_ERROR;
	}
i=lookup_string(alsa_sc, argv[1]);
if((i<0)||((ad=(ALSA_DATA *)alsa_sc->data[i])==NULL)){
	Tcl_AppendResult(interp,"ERROR: alsa_ctl_get_elements_info: no such control open", NULL);
	return TCL_ERROR;
	}
if(ad->elem_count>0){
	for(k=0;k<ad->elem_count;k++){
		snd_ctl_elem_info_free(ad->einfo[k]);
		}
	free(ad->einfo);
	free(ad->elem);
	free(ad->etype);
	ad->elem_count=0;
	ad->einfo=NULL;
	ad->etype=NULL;
	}
count=snd_hctl_get_count(ad->hctl);
ad->elem_count=count;
ad->elem=do_alloc(count, sizeof(snd_hctl_elem_t *));
ad->einfo=do_alloc(count, sizeof(snd_ctl_elem_info_t *));
ad->etype=do_alloc(count, sizeof(snd_ctl_elem_type_t));
j=0;
ans=Tcl_NewListObj(0, NULL);
for(elem=snd_hctl_first_elem(ad->hctl);elem!=NULL;elem=snd_hctl_elem_next(elem)){
	ad->elem[j]=elem;
	snd_ctl_elem_info_malloc(&(ad->einfo[j]));
        snd_hctl_elem_info(elem, ad->einfo[j]);

	list=Tcl_NewListObj(0, NULL);
	/* ok, we can't simply count on the order of entries being fixed */
	Tcl_ListObjAppendElement(interp, list, Tcl_NewStringObj("-interface", -1));
	Tcl_ListObjAppendElement(interp, list, Tcl_NewStringObj(snd_ctl_elem_iface_name(snd_hctl_elem_get_interface(elem)), -1));

	Tcl_ListObjAppendElement(interp, list, Tcl_NewStringObj("-name", -1));
	Tcl_ListObjAppendElement(interp, list, Tcl_NewStringObj(snd_hctl_elem_get_name(elem), -1));

	ad->etype[j]=snd_ctl_elem_info_get_type(ad->einfo[j]);

	Tcl_ListObjAppendElement(interp, list, Tcl_NewStringObj("-type", -1));
	Tcl_ListObjAppendElement(interp, list, Tcl_NewStringObj(snd_ctl_elem_type_name(ad->etype[j]), -1));

	Tcl_ListObjAppendElement(interp, list, Tcl_NewStringObj("-values_count", -1));
	Tcl_ListObjAppendElement(interp, list, Tcl_NewIntObj(snd_ctl_elem_info_get_count(ad->einfo[j])));

	switch(ad->etype[j]){
		case SND_CTL_ELEM_TYPE_INTEGER:
			Tcl_ListObjAppendElement(interp, list, Tcl_NewStringObj("-min", -1));
			Tcl_ListObjAppendElement(interp, list, Tcl_NewIntObj(snd_ctl_elem_info_get_min(ad->einfo[j])));

			Tcl_ListObjAppendElement(interp, list, Tcl_NewStringObj("-max", -1));
			Tcl_ListObjAppendElement(interp, list, Tcl_NewIntObj(snd_ctl_elem_info_get_max(ad->einfo[j])));

			Tcl_ListObjAppendElement(interp, list, Tcl_NewStringObj("-step", -1));
			Tcl_ListObjAppendElement(interp, list, Tcl_NewIntObj(snd_ctl_elem_info_get_step(ad->einfo[j])));
			break;
		case SND_CTL_ELEM_TYPE_ENUMERATED:
			items=snd_ctl_elem_info_get_items(ad->einfo[j]);
			Tcl_ListObjAppendElement(interp, list, Tcl_NewStringObj("-items", -1));
			list2=Tcl_NewListObj(0, NULL);
			for(k=0;k<items;k++){
				snd_ctl_elem_info_set_item(ad->einfo[j], k);
				if((a=snd_hctl_elem_info(elem, ad->einfo[j]))<0){
					fprintf(stderr,"Alsa error %s\n", snd_strerror(a));
					} else {
					fprintf(stderr,"item %d %s\n", k, snd_ctl_elem_info_get_item_name(ad->einfo[j]));
					Tcl_ListObjAppendElement(interp, list2, Tcl_NewStringObj(snd_ctl_elem_info_get_item_name(ad->einfo[j]), -1));
					}
				}
			Tcl_ListObjAppendElement(interp,list,list2);
			break;
		}
	Tcl_ListObjAppendElement(interp, ans, list);
	j++;
	}

Tcl_SetObjResult(interp, ans);
return TCL_OK;
}

int alsa_hctl_get_element_value(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
long i,e;
int a,k;
ALSA_DATA *ad;
snd_ctl_elem_value_t *value;
Tcl_Obj *ans;
Tcl_ResetResult(interp);
if(argc<3){
	Tcl_AppendResult(interp,"ERROR: alsa_hctl_get_element_value requires two arguments", NULL);
	return TCL_ERROR;
	}
i=lookup_string(alsa_sc, argv[1]);
if((i<0)||((ad=(ALSA_DATA *)alsa_sc->data[i])==NULL)){
	Tcl_AppendResult(interp,"ERROR: alsa_hctl_get_element_value: no such control open", NULL);
	return TCL_ERROR;
	}
if((ad->elem==NULL)||(ad->einfo==NULL)||(ad->etype==NULL)){
	Tcl_AppendResult(interp,"ERROR: alsa_hctl_get_element_value: you must call alsa_ctl_get_elements_info first", NULL);
	return TCL_ERROR;
	}
e=atoi(argv[2]);
if((e<0)||(e>=ad->elem_count)){
	Tcl_AppendResult(interp,"ERROR: alsa_hctl_get_element_value: no such element", NULL);
	return TCL_ERROR;
	}
 
if((a=snd_ctl_elem_value_malloc(&value))<0){
	Tcl_AppendResult(interp,"ERROR: alsa_hctl_get_element_value: ", NULL);
	Tcl_AppendResult(interp, snd_strerror(a), NULL);
	return TCL_ERROR;
	}
if((a=snd_hctl_elem_read(ad->elem[e], value))<0){
	Tcl_AppendResult(interp,"ERROR: alsa_hctl_get_element_value: ", NULL);
	Tcl_AppendResult(interp, snd_strerror(a), NULL);
	free(value);
	return TCL_ERROR;
	}
ans=Tcl_NewListObj(0, NULL);
for(k=0;k<snd_ctl_elem_info_get_count(ad->einfo[e]);k++){
	switch(ad->etype[e]){
		case SND_CTL_ELEM_TYPE_INTEGER:
			Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(snd_ctl_elem_value_get_integer(value,k)));
			break;
		case SND_CTL_ELEM_TYPE_BOOLEAN:
			Tcl_ListObjAppendElement(interp, ans, Tcl_NewIntObj(snd_ctl_elem_value_get_boolean(value,k)));
			break;
		case SND_CTL_ELEM_TYPE_ENUMERATED:
			snd_ctl_elem_info_set_item(ad->einfo[e], snd_ctl_elem_value_get_enumerated(value,k));
			if((a=snd_hctl_elem_info(ad->elem[e], ad->einfo[e]))<0){
				fprintf(stderr,"Alsa error %s\n", snd_strerror(a));
				} else {
				Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj(snd_ctl_elem_info_get_item_name(ad->einfo[e]),-1));			
				}
			break;
		default:
			Tcl_ListObjAppendElement(interp, ans, Tcl_NewStringObj("undecipherable",-1));			
		}
	}
snd_ctl_elem_value_free(value);
Tcl_SetObjResult(interp, ans);
return TCL_OK;
}

int alsa_hctl_set_element_value(ClientData client_data,Tcl_Interp* interp,int argc,char *argv[])
{
long i,j,e;
int a,k;
ALSA_DATA *ad;
snd_ctl_elem_value_t *value;
Tcl_Obj *ans;
Tcl_ResetResult(interp);
if(argc<5){
	Tcl_AppendResult(interp,"ERROR: alsa_hctl_set_element_value requires four arguments", NULL);
	return TCL_ERROR;
	}
i=lookup_string(alsa_sc, argv[1]);
if((i<0)||((ad=(ALSA_DATA *)alsa_sc->data[i])==NULL)){
	Tcl_AppendResult(interp,"ERROR: alsa_hctl_set_element_value: no such control open", NULL);
	return TCL_ERROR;
	}
if((ad->elem==NULL)||(ad->einfo==NULL)||(ad->etype==NULL)){
	Tcl_AppendResult(interp,"ERROR: alsa_hctl_set_element_value: you must call alsa_ctl_set_elements_info first", NULL);
	return TCL_ERROR;
	}
e=atoi(argv[2]);
if((e<0)||(e>=ad->elem_count)){
	Tcl_AppendResult(interp,"ERROR: alsa_hctl_set_element_value: no such element", NULL);
	return TCL_ERROR;
	}
 
if((a=snd_ctl_elem_value_malloc(&value))<0){
	Tcl_AppendResult(interp,"ERROR: alsa_hctl_set_element_value: ", NULL);
	Tcl_AppendResult(interp, snd_strerror(a), NULL);
	return TCL_ERROR;
	}
if((a=snd_hctl_elem_read(ad->elem[e], value))<0){
	Tcl_AppendResult(interp,"ERROR: alsa_hctl_set_element_value: ", NULL);
	Tcl_AppendResult(interp, snd_strerror(a), NULL);
	free(value);
	return TCL_ERROR;
	}
k=atoi(argv[3]);
if((k<0)||(k>=snd_ctl_elem_info_get_count(ad->einfo[e]))){
	Tcl_AppendResult(interp,"ERROR: alsa_hctl_set_element_value: no such value in this element", NULL);
	snd_ctl_elem_value_free(value);
	return TCL_ERROR;
	}
switch(ad->etype[e]){
	case SND_CTL_ELEM_TYPE_INTEGER:
		snd_ctl_elem_value_set_integer(value,k, atoi(argv[4]));
		break;
	case SND_CTL_ELEM_TYPE_BOOLEAN:
		snd_ctl_elem_value_set_boolean(value,k, atoi(argv[4]));
		break;
	case SND_CTL_ELEM_TYPE_ENUMERATED:
		for(j=0;j<snd_ctl_elem_info_get_items(ad->einfo[e]);j++){
			snd_ctl_elem_info_set_item(ad->einfo[e], j);
			if((a=snd_hctl_elem_info(ad->elem[e], ad->einfo[e]))<0){
				fprintf(stderr,"Alsa error %s\n", snd_strerror(a));
				} else 
			if(!strcmp(argv[4],snd_ctl_elem_info_get_item_name(ad->einfo[e]))){
				snd_ctl_elem_value_set_enumerated(value,k,j);				
				}
			}
		break;
	default:
		Tcl_AppendResult(interp,"ERROR: alsa_hctl_set_element_value: don't know how to set value for element of this type", NULL);
		snd_ctl_elem_value_free(value);
		return TCL_ERROR;
	}
if((a=snd_hctl_elem_write(ad->elem[e], value))<0){
	Tcl_AppendResult(interp,"ERROR: alsa_hctl_set_element_value: ", NULL);
	Tcl_AppendResult(interp, snd_strerror(a), NULL);
	snd_ctl_elem_value_free(value);
	return TCL_ERROR;
	}
snd_ctl_elem_value_free(value);
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
	{"alsa_hctl_open", alsa_hctl_open},
	{"alsa_hctl_close", alsa_hctl_close},
	{"alsa_hctl_get_elements_info", alsa_hctl_get_elements_info},
	{"alsa_hctl_get_element_value", alsa_hctl_get_element_value},
	{"alsa_hctl_set_element_value", alsa_hctl_set_element_value},
	{NULL, NULL}
	};

void init_alsa(Tcl_Interp *interp)
{
long i;

alsa_sc=new_string_cache();

snd_pcm_info_malloc(&pcminfo);
snd_rawmidi_info_malloc(&rawmidiinfo);

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

