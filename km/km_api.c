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
EXPORT_SYMBOL(kmd_signal_state_change);

static void expand_buffer(KM_DEVICE *kmd, long increment)
{
char *b;
kmd->br_size+=kmd->br_size+increment;
b=kmalloc(kmd->br_size, GFP_KERNEL);
if(kmd->br_free>0)memcpy(b, kmd->buffer_read, kmd->br_free);
kfree(kmd->buffer_read);
kmd->buffer_read=b;
}

static void perform_status_cmd(KM_DEVICE *kmd)
{
long field_length;
KM_FIELD *f;
int i;

for(i=0;kmd->fields[i].type!=KM_FIELD_TYPE_EOL;i++){
	f=&(kmd->fields[i]);
	switch(f->type){
		case KM_FIELD_TYPE_STATIC:
			field_length=strlen(f->name)+strlen(f->data.c.string)+2;
			if(kmd->br_free+field_length+10>=kmd->br_size)expand_buffer(kmd, field_length+10);
			sprintf(kmd->buffer_read+kmd->br_free, "%s=%s\n", f->name, f->data.c.string);
			kmd->br_free+=field_length;
			break;
		case KM_FIELD_TYPE_DYNAMIC_INT:
			f->data.i.old_value=*(f->data.i.field);
			field_length=strlen(f->name)+20;
			if(kmd->br_free+field_length>=kmd->br_size)expand_buffer(kmd, field_length);
			kmd->br_free+=sprintf(kmd->buffer_read+kmd->br_free, "%s=%d\n", f->name, (f->data.i.old_value));
			break;
		}
	}
kmd_signal_state_change(kmd->number);
}

static void dump_changed_fields(KM_DEVICE *kmd)
{
int i;
long field_length;
u32 a;
KM_FIELD *f;
for(i=0;kmd->fields[i].type!=KM_FIELD_TYPE_EOL;i++){
	f=&(kmd->fields[i]);
/*	if(!(f->changed))continue; */
	switch(f->type){
		case KM_FIELD_TYPE_DYNAMIC_INT:
			a=*(f->data.i.field);
			if(a==f->data.i.old_value)continue;
			f->data.i.old_value=a;
			field_length=strlen(f->name)+20;
			if(kmd->br_free+field_length>=kmd->br_size)expand_buffer(kmd, field_length);
			kmd->br_free+=sprintf(kmd->buffer_read+kmd->br_free, "%s=%d\n", f->name, (f->data.i.old_value));
			break;
		}
	}
}

static int km_fo_control_open(struct inode * inode, struct file * file)
{
char *filename;
KM_DEVICE *kmd=NULL;
int i;
filename=file->f_dentry->d_iname;
if(strncmp(filename, "control", 7)){
	return -EINVAL;
	}
i=simple_strtol(filename+7, NULL, 10);
if(i<0)return -EINVAL;
if(i>=devices_free)return -EINVAL;
kmd=&(devices[i]);
file->private_data=kmd;
kmd->use_count++;
return 0;
}

static int km_fo_control_release(struct inode * inode, struct file * file)
{
KM_DEVICE *kmd=file->private_data;
kmd->use_count--;
return 0;
}

ssize_t km_fo_control_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
int retval=0;
KM_DEVICE *kmd=file->private_data;
DECLARE_WAITQUEUE(wait, current);
if(kmd->br_free==0){
	dump_changed_fields(kmd);
	}
add_wait_queue(&(kmd->wait), &wait);
current->state=TASK_INTERRUPTIBLE;
while(kmd->br_free==0){
	if(file->f_flags & O_NONBLOCK){
		retval=-EAGAIN;
		break;
		}
	if(signal_pending(current)){
		retval=-ERESTARTSYS;
		break;
		}
	schedule();
	dump_changed_fields(kmd);
	}
current->state=TASK_RUNNING;
remove_wait_queue(&(kmd->wait), &wait);
if(retval<0)return retval;
if(count>(kmd->br_free-kmd->br_read))count=kmd->br_free-kmd->br_read;
if(count>0)memcpy(buf, kmd->buffer_read+kmd->br_read, count); 
kmd->br_read+=count;
if(kmd->br_read==kmd->br_free){
	kmd->br_free=0;
	kmd->br_read=0;	
	}
return count;
}

static unsigned int km_fo_control_poll(struct file *file, poll_table *wait)
{
KM_DEVICE *kmd=file->private_data;
poll_wait(file, &(kmd->wait), wait);
if (kmd->br_free<kmd->br_read)
	return POLLIN | POLLRDNORM;
return 0;
}

static ssize_t km_fo_control_write(struct file * file, const char * buffer, size_t count, loff_t *ppos)
{
KM_DEVICE *kmd=file->private_data;
printk("km_control_write: count=%ld\n", count);
perform_status_cmd(kmd);
return count;
}

static int km_control_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
KM_DEVICE *kmd=data;
if(kmd->br_free==0){
	dump_changed_fields(kmd);
	}
if(count>(kmd->br_free-kmd->br_read))count=kmd->br_free-kmd->br_read;
if(count>0)memcpy(page, kmd->buffer_read+kmd->br_read, count); 
kmd->br_read+=count;
if(kmd->br_read==kmd->br_free){
	kmd->br_free=0;
	kmd->br_read=0;	
	/* put here code to handle out-of-sequence messages */
/*	*eof=1; */
	}
*start=page;
return count;
}


static int km_control_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
KM_DEVICE *kmd=data;
printk("km_control_write: count=%ld\n", count);
perform_status_cmd(kmd);
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

struct file_operations km_control_file_operations={
	owner: 	THIS_MODULE,
	open:	km_fo_control_open,
	read: 	km_fo_control_read,
	release: km_fo_control_release,
	write: km_fo_control_write,
	poll: km_fo_control_poll

	} ;

int add_km_device(KM_FIELD *kmfl, void *priv)
{
long i,num;
char temp[32];
KM_DEVICE * kmd=NULL;
num=-1;
for(i=0;(num<0)&&(i<devices_free);i++){
	if(devices[i].number<0)num=i;
	}
if(num<0){
	num=devices_free;
	if(devices_free>=devices_size)expand_devices();
	}
kmd=&(devices[num]);
memset(kmd, sizeof(KM_DEVICE), 0);
kmd->number=num;
kmd->fields=kmfl;
kmd->priv=priv;
kmd->br_size=PAGE_SIZE;
kmd->br_free=0;
kmd->br_read=0;
kmd->buffer_read=kmalloc(kmd->br_size, GFP_KERNEL);
sprintf(temp, "control%d", num);
kmd->control=create_proc_entry(temp, S_IFREG | S_IRUGO | S_IWUSR, km_root);
sprintf(temp, "data%d", num);
kmd->data=create_proc_entry(temp, S_IFREG | S_IRUGO | S_IWUSR, km_root);
kmd->control->read_proc=km_control_read;
kmd->control->write_proc=km_control_write;
/* kmd->control->proc_fops=&km_file_operations; */
kmd->control->data=&(devices[num]);
kmd->control->proc_fops=&km_control_file_operations;
kmd->data->data=kmd;
init_waitqueue_head(&(kmd->wait));
devices_free++;
MOD_INC_USE_COUNT;
return num;
}

void kmd_signal_state_change(int num)
{
if(num<0)return;
if(num>=devices_free)return;
wake_up_interruptible(&(devices[num].wait));
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

void cleanup_module(void);

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

