/*     km preliminary version

(C) Vladimir Dergachev 2001-2003
       
GNU Public License
       
*/

#include <linux/autoconf.h>
#if defined(MODULE) && defined(CONFIG_MODVERSIONS)
#define MODVERSIONS
#ifdef LINUX_2_6
#include <config/modversions.h>
#else
#include <linux/modversions.h>
#endif
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
#include "km_api_data.h"

struct proc_dir_entry  *km_root=NULL;

static struct file_operations km_file_operations;

KM_DEVICE **devices=NULL;
long devices_size=0;
long devices_free=0;

#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

MODULE_DESCRIPTION("km_api");
EXPORT_SYMBOL(add_km_device);
EXPORT_SYMBOL(remove_km_device);
EXPORT_SYMBOL(kmd_signal_state_change);
EXPORT_SYMBOL(km_allocate_data_virtual_block);
EXPORT_SYMBOL(km_deallocate_data);
EXPORT_SYMBOL(km_data_create_kdufpd);
EXPORT_SYMBOL(km_data_destroy_kdufpd);
EXPORT_SYMBOL(km_data_generic_stream_read);
EXPORT_SYMBOL(km_data_generic_stream_poll);
EXPORT_SYMBOL(km_fo_control_perform_command);
EXPORT_SYMBOL(num_km_devices);
EXPORT_SYMBOL(open_km_device);
EXPORT_SYMBOL(close_km_device);

#define KM_MODULUS	255
#define KM_MULTIPLE	23

unsigned  km_command_hash(const char *s, int length)
{
	int i;
	unsigned r;
	r=0;
	for(i=0;(i<length)&& s[i] && (s[i]!=' ') && (s[i]!='\n') && (s[i]!='=');i++){
		r=(r*KM_MULTIPLE+s[i]) % KM_MODULUS;
	}
	if((i>=length)&& s[i]){
		printk("%s function %s: length limit reached for string %s\n",__FILE__, __FUNCTION__, s);
	}
	return r;
}


static void expand_buffer(KM_FILE_PRIVATE_DATA *kmfpd, long increment)
{
	char *b;
	kmfpd->br_size+=kmfpd->br_size+increment;
	b=kmalloc(kmfpd->br_size, GFP_KERNEL);
	if(kmfpd->br_free>0)memcpy(b, kmfpd->buffer_read, kmfpd->br_free);
	kfree(kmfpd->buffer_read);
	kmfpd->buffer_read=b;
}

static void perform_status_cmd(KM_DEVICE *kmd, KM_FILE_PRIVATE_DATA *kmfpd)
{
	long field_length;
	KM_FIELD *f;
	int i;

	if(kmfpd->br_free+10>=kmfpd->br_size)expand_buffer(kmfpd, 10);
	kmfpd->br_free+=sprintf(kmfpd->buffer_read+kmfpd->br_free, "+STATUS\n");

	for(i=0;kmd->fields[i].type!=KM_FIELD_TYPE_EOL;i++){
		f=&(kmd->fields[i]);
		switch(f->type){
		case KM_FIELD_TYPE_STATIC:
			field_length=strlen(f->name)+strlen(f->data.c.string)+2;
			if(kmfpd->br_free+field_length+10>=kmfpd->br_size)expand_buffer(kmfpd, field_length+10);
			kmfpd->br_free+=sprintf(kmfpd->buffer_read+kmfpd->br_free, "%s=%s\n", f->name, f->data.c.string);
			break;
		case KM_FIELD_TYPE_DYNAMIC_INT:
			kmfpd->kfd[i].i.old_value=*(f->data.i.field);
			field_length=strlen(f->name)+20;
			if(kmfpd->br_free+field_length>=kmfpd->br_size)expand_buffer(kmfpd, field_length);
			kmfpd->br_free+=sprintf(kmfpd->buffer_read+kmfpd->br_free, "%s=%u\n", f->name, kmfpd->kfd[i].i.old_value);
			break;
		case KM_FIELD_TYPE_DYNAMIC_STRING:
			kmfpd->kfd[i].s.old_string=*(f->data.s.string);
			field_length=strlen(f->name)+strlen(*(f->data.s.string))+4;
			if(kmfpd->br_free+field_length>=kmfpd->br_size)expand_buffer(kmfpd, field_length);
			kmfpd->br_free+=sprintf(kmfpd->buffer_read+kmfpd->br_free, "%s=%s\n", f->name, kmfpd->kfd[i].s.old_string);
			break;
		case KM_FIELD_TYPE_LEVEL_TRIGGER:
			kmfpd->kfd[i].t.old_count=f->data.t.count;
			field_length=strlen(f->name)+20;
			if(kmfpd->br_free+field_length>=kmfpd->br_size)expand_buffer(kmfpd, field_length);
			kmfpd->br_free+=sprintf(kmfpd->buffer_read+kmfpd->br_free, "%s=%u\n", f->name, kmfpd->kfd[i].t.old_count);
			break;
		}
	}
	if(kmfpd->br_free+10>=kmfpd->br_size)expand_buffer(kmfpd, 14);
	kmfpd->br_free+=sprintf(kmfpd->buffer_read+kmfpd->br_free, "+END_STATUS\n");
	kmfpd->request_flags &= ~KM_STATUS_REQUESTED; 
	kmd_signal_state_change(kmd->number);
}

static void dump_changed_fields(KM_DEVICE *kmd, KM_FILE_PRIVATE_DATA *kmfpd)
{
	int i;
	long field_length;
	u32 a;
	char *b;
	KM_FIELD *f;
	for(i=0;kmd->fields[i].type!=KM_FIELD_TYPE_EOL;i++){
		if(!(kmfpd->field_flags[i] & KM_FIELD_UPDATE_REQUESTED))continue;
		f=&(kmd->fields[i]);
		/*	if(!(f->changed))continue; */
		switch(f->type){
		case KM_FIELD_TYPE_DYNAMIC_INT:
			a=*(f->data.i.field);
			if(a==kmfpd->kfd[i].i.old_value)continue;
			kmfpd->kfd[i].i.old_value=a;
			field_length=strlen(f->name)+22;
			if(kmfpd->br_free+field_length>=kmfpd->br_size)expand_buffer(kmfpd, field_length);
			kmfpd->buffer_read[kmfpd->br_free]=':';
			kmfpd->br_free++;
			kmfpd->br_free+=sprintf(kmfpd->buffer_read+kmfpd->br_free, "%s=%u\n", f->name, a);
			break;
		case KM_FIELD_TYPE_DYNAMIC_STRING:
			b=*(f->data.s.string);
			if(b==kmfpd->kfd[i].s.old_string)continue;
			kmfpd->kfd[i].s.old_string=b;
			field_length=strlen(f->name)+6+strlen(b);
			if(kmfpd->br_free+field_length>=kmfpd->br_size)expand_buffer(kmfpd, field_length);
			kmfpd->buffer_read[kmfpd->br_free]=':';
			kmfpd->br_free++;
			kmfpd->br_free+=sprintf(kmfpd->buffer_read+kmfpd->br_free, "%s=%s\n", f->name, b);
			break;
		case KM_FIELD_TYPE_LEVEL_TRIGGER:
			a=f->data.t.count;
			if(a==kmfpd->kfd[i].t.old_count)continue;
			kmfpd->kfd[i].t.old_count=a;
			field_length=strlen(f->name)+22;
			if(kmfpd->br_free+field_length>=kmfpd->br_size)expand_buffer(kmfpd, field_length);
			kmfpd->buffer_read[kmfpd->br_free]=':';
			kmfpd->br_free++;
			kmfpd->br_free+=sprintf(kmfpd->buffer_read+kmfpd->br_free, "%s=%u\n", f->name, a);
			break;
		}
	}
}

int num_km_devices(void)
{
	return devices_free;
}

KM_FILE_PRIVATE_DATA *open_km_device(int number)
{
	KM_FILE_PRIVATE_DATA *kmfpd=NULL;
	KM_DEVICE *kmd=NULL;
	int i;

	if(number<0)return NULL;
	if(number>=devices_free)return NULL;
	kmd=(devices[number]);
	if((kmd==NULL)||(kmd->fields==NULL))return NULL;

#ifndef LINUX_2_6
	MOD_INC_USE_COUNT;
#endif

	kmfpd=kmalloc(sizeof(KM_FILE_PRIVATE_DATA), GFP_KERNEL);
	memset(kmfpd, 0, sizeof(KM_FILE_PRIVATE_DATA));

	kmfpd->br_size=PAGE_SIZE;
	kmfpd->br_free=0;
	kmfpd->br_read=0;
	kmfpd->buffer_read=kmalloc(kmfpd->br_size, GFP_KERNEL);

	spin_lock(&(kmd->lock));
	kmfpd->kmd=kmd;
	kmfpd->field_flags=kmalloc(sizeof(*(kmfpd->field_flags))*kmd->num_fields, GFP_ATOMIC);
	memset(kmfpd->field_flags, 0, sizeof(*(kmfpd->field_flags))*kmd->num_fields);

	kmfpd->kfd=kmalloc(sizeof(*(kmfpd->kfd))*kmd->num_fields, GFP_ATOMIC);
	memset(kmfpd->kfd, 0, sizeof(*(kmfpd->kfd))*kmd->num_fields);

	/* initialize field values */
	for(i=0;i<kmd->num_fields;i++)
		switch(kmd->fields[i].type){
		case KM_FIELD_TYPE_LEVEL_TRIGGER:
			kmfpd->kfd[i].t.requested=0;
			kmfpd->kfd[i].t.old_count=0;
			break;
		}

	spin_lock_init(&(kmfpd->lock));

	kmd->use_count++;
	spin_unlock(&(kmd->lock));
	return kmfpd;
}

static int km_fo_control_open(struct inode * inode, struct file * file)
{
	char *filename;
	KM_FILE_PRIVATE_DATA *kmfpd=NULL;
	int i;
	filename=file->f_dentry->d_iname;
	if(strncmp(filename, "control", 7)){
		return -EINVAL;
	}
	i=simple_strtol(filename+7, NULL, 10);
	kmfpd=open_km_device(i);
	if(kmfpd==NULL)return -EINVAL;
	file->private_data=kmfpd;
	return 0;
}

void close_km_device(KM_FILE_PRIVATE_DATA *kmfpd)
{
	char temp[32];
	KM_DEVICE *kmd=kmfpd->kmd;
	int i;

	spin_lock(&(kmd->lock));
	/* cleanup first, as some fields need to relinquish their state */
	for(i=0;i<kmd->num_fields;i++){
		switch(kmd->fields[i].type){
		case KM_FIELD_TYPE_LEVEL_TRIGGER:
			if(kmfpd->kfd[i].t.requested){
				kmd->fields[i].data.t.count--;
				if(!kmd->fields[i].data.t.count)kmd->fields[i].data.t.one2zero(kmd->fields[i].data.t.priv);				
			}
			break;
		}
	}

	kfree(kmfpd->field_flags);
	kmfpd->field_flags=NULL;
	kfree(kmfpd->kfd);
	kmfpd->kfd=NULL;
	kfree(kmfpd->buffer_read);
	kmfpd->buffer_read=NULL;
	kfree(kmfpd);
	kmfpd=NULL;
	kmd->use_count--;
	if(kmd->use_count<=0){
		devices[kmd->number]=NULL;
		sprintf(temp, "control%ld", kmd->number);
		remove_proc_entry(temp, km_root);
		kmd->control=NULL;
		kfree(kmd);
	}
	spin_unlock(&(kmd->lock));

#ifndef LINUX_2_6
	MOD_DEC_USE_COUNT;
#endif
}

static int km_fo_control_release(struct inode * inode, struct file * file)
{
	KM_FILE_PRIVATE_DATA *kmfpd=file->private_data;

	close_km_device(kmfpd);
	file->private_data=NULL;
	return 0;
}

ssize_t km_fo_control_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	int retval=0;
	KM_FILE_PRIVATE_DATA *kmfpd=file->private_data;
	KM_DEVICE *kmd=kmfpd->kmd;
	DECLARE_WAITQUEUE(wait, current);
	spin_lock(&(kmfpd->lock));
	if(kmfpd->br_free==0){	
		dump_changed_fields(kmd, kmfpd);
	}
	spin_unlock(&(kmfpd->lock));
	add_wait_queue(&(kmd->wait), &wait);
	current->state=TASK_INTERRUPTIBLE;
	while((kmfpd->br_free==0)){
		if(file->f_flags & O_NONBLOCK){
			retval=-EAGAIN;
			break;
		}
		if(signal_pending(current)){
			retval=-ERESTARTSYS;
			break;
		}
		if(!kmfpd->request_flags)
			schedule();
		/* check that the devices has not been removed from under us */
		if(kmd->fields!=NULL){
			spin_lock(&(kmfpd->lock));
			if(kmfpd->request_flags & KM_STATUS_REQUESTED)perform_status_cmd(kmd, kmfpd);
			dump_changed_fields(kmd, kmfpd);
			spin_unlock(&(kmfpd->lock));
		}
	}
	current->state=TASK_RUNNING;
	remove_wait_queue(&(kmd->wait), &wait);
	if(retval<0)return retval;
	if(count>(kmfpd->br_free-kmfpd->br_read))count=kmfpd->br_free-kmfpd->br_read;
	if(count>0)memcpy(buf, kmfpd->buffer_read+kmfpd->br_read, count); 
	kmfpd->br_read+=count;
	if(kmfpd->br_read==kmfpd->br_free){
		kmfpd->br_free=0;
		kmfpd->br_read=0;	
	}
	return count;
}

static unsigned int km_fo_control_poll(struct file *file, poll_table *wait)
{
	KM_FILE_PRIVATE_DATA *kmfpd=file->private_data;
	KM_DEVICE *kmd=kmfpd->kmd;
	poll_wait(file, &(kmd->wait), wait);
	if (kmfpd->br_free<kmfpd->br_read)
		return POLLIN | POLLRDNORM;
	return 0;
}

/* for now only process one command per string */

int km_fo_control_perform_command(KM_FILE_PRIVATE_DATA *kmfpd, const char *command, size_t count)
{
	KM_DEVICE *kmd=kmfpd->kmd;
	int i,j;
	int field_length;
	unsigned hash;
	const char *value;
	KM_FIELD *kf;
	KM_FIELD_DATA *kfd;
	u32 int_value;
	int result=0;

	hash=km_command_hash(command, count);
	spin_lock(&(kmd->lock));
	if((hash==kmd->status_hash) && !strncmp("STATUS\n", command, count)){
		spin_lock(&(kmfpd->lock));
		kmfpd->request_flags|=KM_STATUS_REQUESTED;
		spin_unlock(&(kmfpd->lock));
		kmd_signal_state_change(kmd->number);
	} else
		if((hash==kmd->report_hash)&& (count>8) && !strncmp("REPORT ", command, 7)){
			field_length=count-8; /* exclude trailing \n */
			if(field_length<=0)goto exit; /* bogus command */
			for(i=0;i<kmd->num_fields;i++){
				if(!strncmp(command+7, kmd->fields[i].name, field_length)&&
				   !kmd->fields[i].name[field_length]){
					spin_lock(&(kmfpd->lock));
					kmfpd->field_flags[i]|=KM_FIELD_UPDATE_REQUESTED;
					spin_unlock(&(kmfpd->lock));
					kmd_signal_state_change(kmd->number);
					printk("Reporting field %d = %s\n", i, kmd->fields[i].name);
					goto exit;
				}
			}
		} else {
			i=kmd->command_hash[hash];
			while((i>=0) && (count>=kmd->fields[i].length) && strncmp(kmd->fields[i].name, command, kmd->fields[i].length))i=kmd->fields[i].next_command;
			if(i<0)goto exit; /* nothing matched */
	
			kf=&(kmd->fields[i]);
			kfd=&(kmfpd->kfd[i]);
			spin_unlock(&(kmd->lock));

			value=&(command[kf->length]);
			j=count-kf->length;
			while((*value=='=')||(*value==' ')||(*value=='\t')){
				j--;
				value++;
				if(!j){
					break;
				}
			}

			int_value=simple_strtol(value,NULL,j);
			printk("Processing \"%s\"=\"%s\" int_value=%d\n", kf->name, value, int_value);
			switch(kf->type){
			case KM_FIELD_TYPE_PROGRAMMABLE:
				printk("Performing KM_FIELD_PROGRAMMABLE action \"%s\" is not implemented [yet]\n", kf->name);
				break;
			case KM_FIELD_TYPE_LEVEL_TRIGGER:
				if(!j || (int_value==0)){
					/* trigger lowered */
					if(!kfd->t.requested){ return result; /* redundant */ }
					kfd->t.requested=0;
					kf->data.t.count--;
					if(!kf->data.t.count)kf->data.t.one2zero(kf->data.t.priv);
				} else {
					/* trigger raised */
					if(kfd->t.requested){ return result; /* redundant */ }
					/* the zero2one transition checks for success before
					   increasing count first */
					if(!kf->data.t.count && (result=kf->data.t.zero2one(kf->data.t.priv))){
						return result;
					}
					kfd->t.requested=1;
					kf->data.t.count++;
				}
				break;
			}
			return result;
		}

 exit:
	spin_unlock(&(kmd->lock));
	return result;
}

static ssize_t km_fo_control_write(struct file * file, const char * buffer, size_t count, loff_t *ppos)
{
	KM_FILE_PRIVATE_DATA *kmfpd=file->private_data;
	km_fo_control_perform_command(kmfpd, buffer, count);
	return count;
}

static void expand_devices(void)
{
	KM_DEVICE **d;
	devices_size+=devices_size+10;
	d=kmalloc(devices_size*sizeof(*devices), GFP_KERNEL);
	if(d==NULL){
		devices_size=(devices_size-10)/2;
		return;
	}
	if(devices_free>0)memcpy(d, devices, devices_free*sizeof(*devices));
	kfree(devices);
	devices=d;
}

struct file_operations km_control_file_operations={
	owner: 		THIS_MODULE,
	open:		km_fo_control_open,
	read: 		km_fo_control_read,
	release: 	km_fo_control_release,
	write: 		km_fo_control_write,
	poll: 		km_fo_control_poll
} ;

int add_km_device(KM_FIELD *kmfl, void *priv)
{
	long i,h,num;
	char temp[32];
	KM_DEVICE * kmd=NULL;
	KM_FIELD *kf=NULL;
	num=-1;
#ifndef LINUX_2_6
	MOD_INC_USE_COUNT;
#endif
	for(i=0;(num<0)&&(i<devices_free);i++){
		if(devices[i]==NULL)num=i;
	}
	if(num<0){
		if(devices_free>=devices_size)expand_devices();
		if(devices_free>=devices_size){
#ifndef LINUX_2_6
			MOD_DEC_USE_COUNT;
#endif
			return -ENOMEM;
		}
		num=devices_free;
		devices_free++;
	}
	kmd=kmalloc(sizeof(KM_DEVICE), GFP_KERNEL);
	if(kmd==NULL){
#ifndef LINUX_2_6
		MOD_DEC_USE_COUNT;
#endif
		return -ENOMEM;
	}
	devices[num]=kmd;
	memset(kmd, 0, sizeof(KM_DEVICE));
	kmd->number=num;
	kmd->fields=kmfl;
	kmd->num_fields=0;
	kmd->status_hash=km_command_hash("STATUS", 6);
	kmd->report_hash=km_command_hash("REPORT", 6);
	kmd->command_hash=kmalloc(sizeof(*(kmd->command_hash))*KM_MODULUS, GFP_KERNEL);
	for(i=0;i<KM_MODULUS;i++)kmd->command_hash[i]=-1;
	while((kf=&(kmd->fields[kmd->num_fields]))->type!=KM_FIELD_TYPE_EOL){
		kf->next_command=-1;
		kf->length=strlen(kf->name);
		switch(kf->type){
		case KM_FIELD_TYPE_PROGRAMMABLE:
		case KM_FIELD_TYPE_LEVEL_TRIGGER:
			h=km_command_hash(kf->name, kf->length);
			i=kmd->command_hash[h];
			if(i<0)kmd->command_hash[h]=kmd->num_fields;
			else {
				while(kmd->fields[i].next_command>=0)i=kmd->fields[i].next_command;
				kmd->fields[i].next_command=kmd->num_fields;
			}
			break;
		}
		kmd->num_fields++;
	}
	kmd->priv=priv;
	spin_lock_init(&(kmd->lock));
	init_waitqueue_head(&(kmd->wait));

	sprintf(temp, "control%ld", num);
	/*kmd->control=create_proc_entry(temp, S_IFREG | S_IRUGO | S_IWUSR, km_root); */
	kmd->control=create_proc_entry(temp, S_IFREG | S_IRUGO | S_IWUGO, km_root);
#if 0
	sprintf(temp, "data%ld", num);
	kmd->data=create_proc_entry(temp, S_IFREG | S_IRUGO | S_IWUSR, km_root);
#endif
	kmd->control->data=kmd;
	kmd->control->proc_fops=&km_control_file_operations;

	kmd->use_count=1;
	return num;
}

void kmd_signal_state_change(int num)
{
	if(num<0)return;
	if(num>=devices_free)return;
	if(devices[num]!=NULL)wake_up_interruptible(&(devices[num]->wait));
}

int remove_km_device(int num)
{
	char temp[32];
	KM_DEVICE *kmd;
	if(num<0)return -EINVAL;
	if(num>devices_free)return -EINVAL;
	kmd=devices[num];
	if(kmd==NULL)return -EINVAL;
	spin_lock(&(kmd->lock));
	kfree(kmd->command_hash);
	kmd->command_hash=NULL;
	kmd->fields=NULL;
	kmd->priv=NULL;
	kmd->use_count--;
	if(kmd->use_count<=0){
		devices[num]=NULL;
		sprintf(temp, "control%d", num);
		remove_proc_entry(temp, km_root);
		kmd->control=NULL;
		kfree(kmd);
	}
	spin_unlock(&(kmd->lock));
#ifndef LINUX_2_6
	MOD_DEC_USE_COUNT;
#endif
	return 0;
}


#ifdef MODULE

void km_api_cleanup_module(void);

static int __init km_api_init_module(void)
{
	int result;

	km_root=create_proc_entry("km", S_IFDIR, &proc_root);
	if(km_root==NULL){
		printk(KERN_ERR "km_api: unable to initialize /proc/km\n");
		return -EACCES;
	}
	memcpy(&km_file_operations, km_root->proc_fops, sizeof(struct file_operations));
	if((result=init_km_data_units())<0){
		km_api_cleanup_module();
		return result;
	}
	devices_size=10;
	devices_free=0;
	devices=kmalloc(devices_size*sizeof(KM_DEVICE), GFP_KERNEL);
	if(devices==NULL){
		printk(KERN_ERR "Could not allocate memory for devices array\n");
		km_api_cleanup_module();
		return -ENOMEM;
	}

	printk("Kmultimedia API module version %s loaded\n", KM_API_VERSION);

	return 0;
}

void __exit km_api_cleanup_module(void)
{
	if(devices!=NULL)kfree(devices);
	if(km_root!=NULL)remove_proc_entry("km", &proc_root);
	cleanup_km_data_units();
	return;
}

module_init(km_api_init_module);
module_exit(km_api_cleanup_module);

#endif

