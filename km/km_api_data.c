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
#include <linux/pci.h>

#include "km_api_data.h"
#include "km_memory.h"
#include "km_api.h"

extern struct proc_dir_entry *km_root;

spinlock_t data_units_lock;
KM_DATA_UNIT *data_units=NULL;
long du_size=0;
long du_free=0;

KDU_FILE_PRIVATE_DATA* km_data_create_kdufpd(int data_unit)
{
KM_DATA_UNIT *kdu;
KDU_FILE_PRIVATE_DATA *kdufpd=NULL;
spin_lock(&data_units_lock);
if(data_unit<0)return NULL;
if(data_unit>=du_free)return NULL;
kdu=&(data_units[data_unit]);
MOD_INC_USE_COUNT;
spin_lock(&(kdu->lock));
spin_unlock(&data_units_lock);
if(kdu->use_count<=0){
	MOD_DEC_USE_COUNT;
	spin_unlock(&(kdu->lock));
	return NULL;
	}
kdu->use_count++;
kdufpd=kmalloc(sizeof(KDU_FILE_PRIVATE_DATA), GFP_KERNEL);
if(kdufpd==NULL){
	kdu->use_count--;
	MOD_DEC_USE_COUNT;
	spin_unlock(&(kdu->lock));
	return NULL;
	}
memset(kdufpd, 0, sizeof(KDU_FILE_PRIVATE_DATA));

kdufpd->kdu=kdu;
kdufpd->buffer=0;
kdufpd->bytes_read=0;

spin_unlock(&(kdu->lock));
return kdufpd;
}

void km_data_destroy_kdufpd(KDU_FILE_PRIVATE_DATA *kdufpd)
{
KM_DATA_UNIT *kdu;
kdu=kdufpd->kdu;
if(kdu==NULL)printk(KERN_ERR "BUG in %s", __FUNCTION__);
spin_lock(&(kdu->lock));
kdu->use_count--;
spin_unlock(&(kdu->lock));
MOD_DEC_USE_COUNT;
kfree(kdufpd);
}

long km_data_generic_stream_read(KDU_FILE_PRIVATE_DATA *kdufpd, KM_STREAM_BUFFER_INFO *kmsbi, KM_DATA_VIRTUAL_BLOCK *dvb,
	 char *buf, unsigned long count, int nonblock,
	 int user_flag, int user_flag_mask)
{
int q,todo;
KM_DATA_UNIT *kdu=kdufpd->kdu;
DECLARE_WAITQUEUE(wait, current);
spin_lock(&(kdu->lock));
if(kdufpd->buffer<0){
	printk("Internal error in km_v4l:km_read()\n");
	spin_unlock(&(kdu->lock));
	return -EIO;
	}
q=kmsbi[kdufpd->buffer].next;
while((kdufpd->bytes_read==dvb->free[kdufpd->buffer])||(q<0)||((kmsbi[q].flag & user_flag_mask)!=user_flag)){
	q=kmsbi[kdufpd->buffer].next;
	if((q>=0) && (kdufpd->age<kmsbi[q].age)){
		kdufpd->buffer=q;
		kdufpd->age=kmsbi[q].age;
		if((kmsbi[q].user_flag & user_flag_mask)!=user_flag){
			kdufpd->bytes_read=dvb->free[q];
			KM_DEBUG("Skipping buf %d flag=%d age=%d\n", q, kmsbi[q].flag, kdufpd->age);
			continue;
			} else {
			kdufpd->bytes_read=0;
			KM_DEBUG("Reading buf %d flag=%d age=%d\n", q, kmsbi[q].flag, kdufpd->age);
			break;
			}
		}
	if(nonblock){
		spin_unlock(&(kdu->lock));
		return -EWOULDBLOCK;
		}
	add_wait_queue(&(kdu->dataq), &wait);
	current->state=TASK_INTERRUPTIBLE;
	spin_unlock(&(kdu->lock));
	schedule();
	if(signal_pending(current)){
		spin_lock(&(kdu->lock));
		remove_wait_queue(&(kdu->dataq), &wait);
		current->state=TASK_RUNNING;
		spin_unlock(&(kdu->lock));
		return -EINTR;
		}
	spin_lock(&(kdu->lock));
	remove_wait_queue(&(kdu->dataq), &wait);
	current->state=TASK_RUNNING;
	}
todo=count;
while(todo>0){	
	q=todo;
	if((kdufpd->bytes_read+q)>=dvb->free[kdufpd->buffer])q=dvb->free[kdufpd->buffer]-kdufpd->bytes_read;   
	if(copy_to_user((void *) buf, (void *) (dvb->ptr[kdufpd->buffer]+kdufpd->bytes_read), q)){
		spin_unlock(&(kdu->lock));
		return -EFAULT;
		}
	todo-=q;
	kdufpd->bytes_read+=q;
	buf+=q;
	#if 0
	if(kdufpd->bytes_read>=dvb->free[kdufpd->buffer]){
		kms->v4l_buf_parity=!kms->v4l_buf_parity;
		break;
		}
	#endif
	}
spin_unlock(&(kdu->lock));
return (count-todo);
}

unsigned int km_data_generic_stream_poll(KDU_FILE_PRIVATE_DATA *kdufpd, KM_STREAM_BUFFER_INFO *kmsbi, KM_DATA_VIRTUAL_BLOCK *dvb,
	 struct file *file, poll_table *wait)
{
KM_DATA_UNIT *kdu=kdufpd->kdu;
unsigned int mask=0;

spin_lock(&(kdu->lock));
if(kdufpd->buffer<0){
	printk("km: internal error in kmv4l:km_poll\n");
	spin_unlock(&(kdu->lock));
	return 0;
	}
if(kdufpd->bytes_read==dvb->free[kdufpd->buffer]){
	spin_unlock(&(kdu->lock));
	poll_wait(file, &(kdu->dataq), wait);
	spin_lock(&(kdu->lock));
	}

if(kdufpd->bytes_read<dvb->free[kdufpd->buffer])
	/* Now we have more data.. */
	mask |= (POLLIN | POLLRDNORM);

spin_unlock(&(kdu->lock));
return mask;
}

static int km_fo_data_open(struct inode * inode, struct file * file)
{
char *filename;
KM_DATA_UNIT *kdu;
KDU_FILE_PRIVATE_DATA *kdufpd=NULL;
int i;
filename=file->f_dentry->d_iname;
if(strncmp(filename, "data", 4)){
	return -EINVAL;
	}
i=simple_strtol(filename+4, NULL, 10);

kdufpd=km_data_create_kdufpd(i);
if(kdufpd==NULL){
	return -ENOMEM;
	}
file->private_data=kdufpd;

return 0;
}

static int km_fo_data_release(struct inode * inode, struct file * file)
{
KDU_FILE_PRIVATE_DATA *kdufpd=file->private_data;
KM_DATA_UNIT *kdu=kdufpd->kdu;

KM_CHECKPOINT
file->private_data=NULL;
km_data_destroy_kdufpd(kdufpd);
return 0;
}

static int km_fo_data_mmap(struct file * file, struct vm_area_struct * vma)
{
KDU_FILE_PRIVATE_DATA *kdufpd=file->private_data;
KM_DATA_UNIT *kdu=kdufpd->kdu;
KM_CHECKPOINT
if(kdu->mmap==NULL)return -ENOTSUPP;
return kdu->mmap(file, vma);
}

static int km_dvb_mmap(struct file * file, struct vm_area_struct * vma)
{
KDU_FILE_PRIVATE_DATA *kdufpd=file->private_data;
KM_DATA_UNIT *kdu=kdufpd->kdu;
KM_DATA_VIRTUAL_BLOCK *dvb=(KM_DATA_VIRTUAL_BLOCK *)kdu->data_private;
unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
unsigned long size = vma->vm_end-vma->vm_start;
unsigned long chunk_size;
unsigned long page, start;
int i,j;

KM_CHECKPOINT
if(kdu->type!=KDU_TYPE_VIRTUAL_BLOCK){
	printk(KERN_ERR "km: internal error %s %s %d\n", __FILE__, __FUNCTION__, __LINE__);
	return -ENOTSUPP;
	}
KM_CHECKPOINT
chunk_size=((dvb->size+PAGE_SIZE-1)&~(PAGE_SIZE-1));
if(offset+size>chunk_size*dvb->n)return -EINVAL;
KM_CHECKPOINT
for(i=0;i<dvb->n;i++)
	for(j=0;j<dvb->size;j+=PAGE_SIZE){
		if(chunk_size*i+PAGE_SIZE*j<offset)continue;
		if(chunk_size*i+PAGE_SIZE*j>offset+size)return 0;
		page=kvirt_to_pa(dvb->ptr[i]+j*PAGE_SIZE);
		start=vma->vm_start+chunk_size*i+PAGE_SIZE*j-offset;
		if(remap_page_range(start, page, PAGE_SIZE, PAGE_SHARED))
			return -EAGAIN;
		}
KM_CHECKPOINT
return 0;
}

void km_free_private_data_virtual_block(KM_DATA_UNIT *kdu)
{
int i;
unsigned long chunk_size;
KM_DATA_VIRTUAL_BLOCK *dvb;
if(kdu->type!=KDU_TYPE_VIRTUAL_BLOCK){
	printk(KERN_ERR "km_api_data.km_free_private_data_virtual_block: attempt to free non virtual_block kdu\n");
	}
dvb=(KM_DATA_VIRTUAL_BLOCK *)kdu->data_private;
kdu->data_private=NULL;
kdu->free_private=NULL;
kdu->type=KDU_TYPE_GENERIC;
chunk_size=((dvb->size+PAGE_SIZE-1)&~(PAGE_SIZE-1));
for(i=0;i<dvb->n;i++)rvfree(dvb->ptr[i],chunk_size);
}

struct file_operations km_data_file_operations={
	owner: 		THIS_MODULE,
	open:		km_fo_data_open,
	release: 	km_fo_data_release,
	mmap:		km_fo_data_mmap
/*	read: 		km_fo_data_read,
	write: 		km_fo_data_write,
	poll: 		km_fo_data_poll */
	} ;


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
			return -ENOMEM;
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
init_waitqueue_head(&(kdu->dataq));
kdu->use_count=1;
kdu->type=KDU_TYPE_GENERIC;
kdu->mode=mode;
kdu->number=k;
sprintf(temp, "data%ld", k);
kdu->data=NULL;
if(mode!=0){
	kdu->data=create_proc_entry(temp, mode, km_root);
	if(kdu->data==NULL){
		printk(KERN_ERR "Could not create proc entry %s\n", temp);
		spin_unlock(&(kdu->lock));
		MOD_DEC_USE_COUNT;
		return -1;
		}
	kdu->data->data=kdu;
	kdu->data->proc_fops=&km_data_file_operations;
	}
kdu->data_private=NULL;
kdu->free_private=NULL;
spin_unlock(&(kdu->lock));
return k;
}

int km_allocate_data_virtual_block(KM_DATA_VIRTUAL_BLOCK *dvb, mode_t mode)
{
long i,k;
unsigned long chunk_size;
KM_DATA_UNIT *kdu;
k=km_allocate_data_unit(mode);
if(k<0){
	return k;
	}
kdu=&(data_units[k]);
kdu->type=KDU_TYPE_VIRTUAL_BLOCK;
kdu->data_private=dvb;
kdu->free_private=km_free_private_data_virtual_block;
kdu->mmap=km_dvb_mmap;
chunk_size=((dvb->size+PAGE_SIZE-1)&~(PAGE_SIZE-1));
if(kdu->data!=NULL){
	kdu->data->size=chunk_size*dvb->n;
	}
for(i=0;i<dvb->n;i++){
	dvb->ptr[i]=rvmalloc(chunk_size);
	memset(dvb->ptr[i], 0 , chunk_size);
	}
dvb->dataq=&(kdu->dataq);
return k;
}


void km_deallocate_data(int data_unit)
{
char temp[32];
KM_DATA_UNIT *kdu;
if(data_unit<0)return;
spin_lock(&data_units_lock);
if(data_unit>=du_free)return;
kdu=&(data_units[data_unit]);
spin_lock(&(kdu->lock));
spin_unlock(&data_units_lock);
kdu->use_count--;
KM_CHECKPOINT
if(kdu->use_count>0){
	MOD_DEC_USE_COUNT;
	spin_unlock(&(kdu->lock));
	return; /* something is still using it */
	}
KM_CHECKPOINT
/* free the data */
sprintf(temp, "data%d", data_unit);
if(kdu->data!=NULL){
	remove_proc_entry(temp, km_root);
	kdu->data=NULL;
	}
if(kdu->free_private!=NULL)kdu->free_private(kdu);
spin_unlock(&(kdu->lock));
MOD_DEC_USE_COUNT;
}

int __init init_km_data_units(void)
{
du_size=10;
du_free=0;
data_units=kmalloc(du_size*sizeof(KM_DATA_UNIT), GFP_KERNEL);
spin_lock_init(&(data_units_lock));
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
