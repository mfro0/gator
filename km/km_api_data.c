/*     km preliminary version

       (C) Vladimir Dergachev 2001-2002
       
       GNU Public License
       
*/

#include <linux/autoconf.h>
#if defined(MODULE) && defined(CONFIG_MODVERSIONS)
#define MODVERSIONS
#include <linux/modversions.h>
#endif

#include <linux/proc_fs.h>

#include <linux/types.h>
#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/poll.h>

#include "km_api_data.h"

extern struct proc_dir_entry *km_root;

KM_DATA_UNIT *data_units=NULL;
long du_size=0;
long du_free=0;

int km_allocate_data_unit(mode_t mode)
{
char temp[32];
long i,k;
void *p;
KM_DATA_UNIT *kdu;
k=-1;
for(i=0;i<du_free;i++){
	if(data_units[i].use_count<=0){k=i; break;}
	}
if(k<0){
	k=du_free;
	if(du_free>=du_size){
		du_size+=du_size+10;
		p=kmalloc(du_size*sizeof(KM_DATA_UNIT), GFP_KERNEL);
		if(p==NULL){
			return -1;
			}
		if(du_free>0)memcpy(p, data_units, du_free*sizeof(KM_DATA_UNIT));
		kfree(data_units);
		data_units=p;
		}
	du_free++;
	}
MOD_INC_USE_COUNT;
kdu=&(data_units[k]);
spin_lock_init(&(kdu->lock));
spin_lock(&(kdu->lock));
kdu->use_count=1;
kdu->type=KDU_TYPE_GENERIC;
kdu->mode=mode;
kdu->number=k;
sprintf(temp, "data%ld", k);
kdu->data=create_proc_entry(temp, mode, km_root);
kdu->data->data=kdu;
kdu->data_private=NULL;
kdu->free_private=NULL;
spin_unlock(&(kdu->lock));
return k;
}

int km_allocate_data_virtual_block(KM_DATA_VIRTUAL_BLOCK *dvb, mode_t mode)
{
return -1;
}

void km_deallocate_data(long data_unit)
{
char temp[32];
KM_DATA_UNIT *kdu;
if(data_unit<0)return;
if(data_unit>=du_free)return;
kdu=&(data_units[data_unit]);
spin_lock(&(kdu->lock));
kdu->use_count--;
if(kdu->use_count>0){
	spin_unlock(&(kdu->lock));
	return; /* something is still using it */
	}
/* free the data */
sprintf(temp, "data%ld", data_unit);
remove_proc_entry(temp, km_root);
if(kdu->free_private!=NULL)kdu->free_private(kdu);
spin_unlock(&(kdu->lock));
MOD_DEC_USE_COUNT;
}

int __init init_km_data_units(void)
{
du_size=10;
du_free=0;
data_units=kmalloc(du_size*sizeof(KM_DATA_UNIT), GFP_KERNEL);
if(data_units==NULL){
	printk(KERN_ERR "Could not allocate memory for data units array\n");
	return -ENOMEM;
	}
return 0;
}

void cleanup_km_data_units(void)
{
if(data_units!=NULL)kfree(data_units);
data_units=NULL;
du_size=0;
du_free=0;
}
