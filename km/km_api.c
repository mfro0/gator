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

#include "km_api.h"

struct proc_dir_entry  *km_root=NULL;

static struct file_operations km_file_operations;

KM_DEVICE *devices=NULL;
long devices_size=0;
long devices_free=0;

#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

MODULE_DESCRIPTION("kmultimedia");
EXPORT_SYMBOL(add_km_device);
EXPORT_SYMBOL(remove_km_device);

static void expand_buffer(KM_DEVICE *kmd)
{
char *b;
kmd->br_size+=kmd->br_size;
b=kmalloc(kmd->br_size, GFP_KERNEL);
if(kmd->br_free>0)memcpy(b, kmd->buffer_read, kmd->br_free);
kfree(kmd->buffer_read);
kmd->buffer_read=b;
}


static int km_control_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
char s[5]="XYXY";
KM_DEVICE *kmd=data;
if(count>(kmd->br_free-kmd->br_read))count=kmd->br_free-kmd->br_read;
printk("km_control_read: off=%d count=%d %s kmd->number=%d\n", off, count, s, kmd->number);
if(count>0)memcpy(page, kmd->buffer_read+kmd->br_read, count); 
kmd->br_read+=count;
if(kmd->br_read==kmd->br_free){
	/* put here code to handle out-of-sequence messages */
	*eof=1;
	kmd->br_free=0;
	kmd->br_read=0;	
	}
*start=page;
return count;
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


int add_km_device(KM_FIELD *kmfl, void *priv)
{
long i,num;
char temp[32];
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
devices[num].br_size=PAGE_SIZE;
devices[num].br_free=0;
devices[num].br_read=0;
devices[num].buffer_read=kmalloc(devices[num].br_size, GFP_KERNEL);
sprintf(temp, "control%d", num);
devices[num].control=create_proc_entry(temp, S_IFREG | S_IRUGO | S_IWUSR, km_root);
sprintf(temp, "data%d", num);
devices[num].data=create_proc_entry(temp, S_IFREG | S_IRUGO | S_IWUSR, km_root);
devices[num].control->read_proc=km_control_read;
devices[num].control->write_proc=km_control_write;
/* devices[num].control->proc_fops=&km_file_operations; */
devices[num].control->data=&(devices[num]);
devices[num].data->data=&(devices[num]);
MOD_INC_USE_COUNT;
return num;
}

int remove_km_device(int num)
{
char temp[32];
if(num<0)return -EINVAL;
if(num>devices_free)return -EINVAL;
if(devices[num].number<0)return -EINVAL;
sprintf(temp, "control%d", num);
remove_proc_entry(temp, km_root);
sprintf(temp, "data%d", num);
remove_proc_entry(temp, km_root);
kfree(devices[num].buffer_read);
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

memcpy(&km_file_operations, km_root->proc_fops, sizeof(struct file_operations));
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

