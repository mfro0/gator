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

int km_allocate_data_virtual_block(KM_DATA_VIRTUAL_BLOCK *dvb, mode_t mode)
{
char temp[32];
return -1;
}

void km_deallocate_data(int data_unit)
{
char temp[32];
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
