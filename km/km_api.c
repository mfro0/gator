#include <linux/proc_fs.h>

#include <linux/types.h>
#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>

#include "km_api.h"

struct proc_dir_entry  *km_root=NULL;

#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

MODULE_DESCRIPTION("kmultimedia");

#ifdef MODULE
static int __init init_module(void)
{
int result;
long i;

km_root=create_proc_entry("km", S_IFDIR, &proc_root);
if(km_root==NULL){
	printk("km_api: unable to initialize /proc/km\n");
	return -EACCES;
	}

printk("Kmultimedia API module version %s loaded\n", KM_API_VERSION);

return 0;
}

void cleanup_module(void)
{

remove_proc_entry("km", proc_root_fs);

return;
}


#endif

