#include <linux/proc_fs.h>

#include <linux/types.h>
#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/poll.h>

#include "km_api.h"

struct proc_dir_entry  *km_root=NULL;

KM_DEVICE *devices=NULL;
long devices_size=0;
long devices_free=0;

#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

MODULE_DESCRIPTION("kmultimedia");
EXPORT_SYMBOL(add_km_device);
EXPORT_SYMBOL(remove_km_device);

static int km_control_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
char c='X';
printk("km_control_read: off=%d count=%d\n", off, count);
if(copy_to_user(page, &c, 1)<0)return -EFAULT;
*eof=0;
*start=page+1;
return 1;
}

static int km_control_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
printk("km_control_write: count=%d\n", count);
return count;
}

static void expand_devices(void)
{
KM_DEVICE *d;
devices_size+=devices_size+10;
d=kmalloc(devices_size*sizeof(KM_DEVICE), GFP_KERNEL);
if(devices_free>0)memcpy(d, devices, devices_free*sizeof(KM_DEVICE));
kfree(devices);
devices=d;
}

char temp[20];

int add_km_device(KM_FIELD *kmfl, void *priv)
{
long i,num;
num=-1;
for(i=0;(num<0)&&(i<devices_free);i++){
	if(devices[i].number<0)num=i;
	}
if(num<0){
	num=devices_free;
	if(devices_free>=devices_size)expand_devices();
	}
devices[num].number=num;
devices[num].fields=kmfl;
devices[num].priv=priv;
sprintf(temp, "control%d", num);
devices[num].control=create_proc_entry(temp, S_IFREG | S_IRUGO | S_IWUSR, km_root);
sprintf(temp, "data%d", num);
devices[num].data=create_proc_entry(temp, S_IFREG | S_IRUGO | S_IWUSR, km_root);
devices[num].control->read_proc=km_control_read;
devices[num].control->write_proc=km_control_write;
MOD_INC_USE_COUNT;
return num;
}

int remove_km_device(int num)
{
if(num<0)return -EINVAL;
if(num>devices_free)return -EINVAL;
if(devices[num].number<0)return -EINVAL;
sprintf(temp, "control%d", num);
remove_proc_entry(temp, km_root);
sprintf(temp, "data%d", num);
remove_proc_entry(temp, km_root);
devices[num].number=-1;
devices[num].control=NULL;
devices[num].data=NULL;
devices[num].fields=NULL;
devices[num].priv=NULL;
MOD_DEC_USE_COUNT;
return 0;
}

#ifdef MODULE
static int __init init_module(void)
{
int result;
long i;

km_root=create_proc_entry("km", S_IFDIR, &proc_root);
if(km_root==NULL){
	printk(KERN_ERR "km_api: unable to initialize /proc/km\n");
	return -EACCES;
	}

devices_size=10;
devices_free=0;
devices=kmalloc(devices_size*sizeof(KM_DEVICE), GFP_KERNEL);
if(devices==NULL){
	printk(KERN_ERR "Could not allocate memory for devices array\n");
	cleanup_module();
	return -ENOMEM;
	}

printk("Kmultimedia API module version %s loaded\n", KM_API_VERSION);

return 0;
}

void cleanup_module(void)
{
kfree(devices);
if(km_root!=NULL)remove_proc_entry("km", &proc_root);

return;
}


#endif

