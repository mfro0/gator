/* -*- linux-c -*- */
/*     km preliminary version

(C) Vladimir Dergachev 2001-2003
       
GNU Public License
       
*/

#define __KM_C__

#include <linux/autoconf.h>
#if defined(MODULE) && defined(CONFIG_MODVERSIONS)
#define MODVERSIONS
#ifdef LINUX_2_6
#include <config/modversions.h>
#else
#include <linux/modversions.h>
#endif
#endif

#include <linux/types.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kmod.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#ifndef LINUX_2_6
#include <linux/wrapper.h>
#endif
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17)
#include <media/v4l2-dev.h>
#else
#include <linux/videodev.h>
#endif

#include "km_api.h"
#include "km.h"
#include "km_memory.h"
#include "km_v4l.h"
#include "radeon.h"
#include "mach64.h"
#include "rage128.h"

#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

int km_debug=0;
int km_buffers=5;

MODULE_DESCRIPTION("km_drv");
module_param(km_debug, int, 0);
module_param(km_buffers, int, 0);
MODULE_PARM_DESC(km_debug, "kmultimedia debugging level");
MODULE_PARM_DESC(km_buffers, "how many buffers to use for video capture per each device");

#define POINTER_TO_INT( x ) ((unsigned long)(x))

int find_free_buffer(KM_STREAM *stream)
{
	int next;
	KM_STREAM_BUFFER_INFO *kmsbi=stream->dvb.kmsbi;
	next=stream->next_buf;
	while(kmsbi[next].flag & KM_STREAM_BUF_PINNED)next=kmsbi[next].next;
	stream->next_buf=kmsbi[next].next;
	if((next>stream->num_buffers) || (next<0)){
		printk(KERN_DEBUG "km: Invalid next buffer %d, forcing to 0\n", next);
		next=0;
	}
	return next;
}

int generic_allocate_dvb(KM_STREAM *stream, int num_buffers,  long size)
{
	int i,k;
	if(size>(4096*4096/sizeof(bm_list_descriptor))){
		printk(KERN_ERR "km.c: Too large buffer allocation requested: %ld bytes\n", size);
		return -1;
	}
	spin_lock_init(&(stream->lock));
	stream->total_frames=0;
/* allocate data unit to hold stream meta info */
	stream->dvb_info.size=num_buffers*sizeof(KM_STREAM_BUFFER_INFO);
	stream->dvb_info.n=1;
	stream->dvb_info.free=&(stream->info_free);
	stream->info_free=num_buffers*sizeof(KM_STREAM_BUFFER_INFO);
	stream->dvb_info.ptr=(void **) &(stream->dvb.kmsbi);
	stream->dvb_info.kmsbi=NULL; /* no meta-metainfo */
	stream->info_du=km_allocate_data_virtual_block(&(stream->dvb_info), S_IFREG | S_IRUGO);
	if(stream->info_du<0)return -1;
/* allocate data unit to hold video data */
	stream->num_buffers=num_buffers;
	stream->dvb.size=((size+PAGE_SIZE-1)/PAGE_SIZE)*PAGE_SIZE;
	stream->dvb.n=num_buffers;
	stream->dvb.free=stream->free;
	stream->dvb.ptr=stream->buffer;
	stream->du=km_allocate_data_virtual_block(&(stream->dvb), S_IFREG | S_IRUGO);
	if(stream->du<0){
		km_deallocate_data(stream->info_du);
		stream->info_du=-1;
		stream->num_buffers=-1;
		return -1;
	}
	memset(stream->dvb.kmsbi, 0 , stream->dvb_info.size);
/*
  dma_table allocation and initialization is the only hardware dependent part of this function

  However it is common for all ATI chipsets and should be very similar for other PCI cards.

  Note: only the buffer offsets (which are computed here) are common, flags do differ between mach64, rage128 and 
  radeon cards.
*/


	stream->dma_table=kmalloc(sizeof(*(stream->dma_table))*num_buffers, GFP_KERNEL);
	stream->dma_table_physical=kmalloc(sizeof(u32)*num_buffers, GFP_KERNEL);
	for(k=0;k<num_buffers;k++){
		stream->dvb.free[k]=size;
		stream->dvb.kmsbi[k].flag=0;
		stream->dvb.kmsbi[k].user_flag=0;
		stream->dvb.kmsbi[k].next=k+1;
		stream->dvb.kmsbi[k].prev=k-1;
		stream->dvb.kmsbi[k].age=-1;
		stream->dma_table[k]=rvmalloc(4096);
		stream->dma_table_physical[k]=kvirt_to_bus(POINTER_TO_INT(stream->dma_table[k]));
		stream->free[k]=size;
		memset(stream->dma_table[k], 0, 4096);
		KM_DEBUG("dma_table[%d]=%p\n", k, stream->dma_table[k]);
		/* create DMA table */
		for(i=0;i<(stream->dvb.size/PAGE_SIZE);i++){
			stream->dma_table[k][i].to_addr=kvirt_to_bus(POINTER_TO_INT(stream->buffer[k]+i*PAGE_SIZE));
			KM_DEBUG("dma_table[%d][%d].to_addr=0x%08x\n", k, i,stream->dma_table[k][i].to_addr);
		}
	}
	stream->dvb.kmsbi[0].prev=num_buffers-1;
	stream->dvb.kmsbi[num_buffers-1].next=0;
	stream->next_buf=0;
	return 0;
}

int verify_dvb(KM_STRUCT *kms, KM_STREAM *stream)
{
	int i,k;
#define VERIFY_PAGE(addr)    ((kms->verify_page==NULL)||kms->verify_page(kms, (addr)))
	for(k=0;k<stream->num_buffers;k++){
		if(!VERIFY_PAGE(POINTER_TO_INT(kvirt_to_bus(POINTER_TO_INT(stream->dma_table[k]))))) return 0;
		for(i=0;i<(stream->dvb.size/PAGE_SIZE);i++){
			if(!VERIFY_PAGE(stream->dma_table[k][i].to_addr))return 0;
		}
	}
	return 1;
}

int generic_deallocate_dvb(KM_STREAM *stream)
{
	int k;
	spin_lock(&(stream->lock));
	for(k=0;k<stream->num_buffers;k++){
		rvfree(stream->dma_table[k], 4096);
	}
	kfree(stream->dma_table_physical);	
	kfree(stream->dma_table);
	stream->dma_table=NULL;
	km_deallocate_data(stream->du);
	stream->du=-1;
	km_deallocate_data(stream->info_du);
	stream->info_du=-1;
	stream->num_buffers=-1;
	spin_unlock(&(stream->lock));
	printk("km: closed stream, %ld buffers captured\n", stream->total_frames);
	return 0;
}

KM_TRANSFER_QUEUE * km_make_transfer_queue(int size)
{
	int i;
	KM_TRANSFER_QUEUE *kmtq;
	kmtq=kmalloc(sizeof(KM_TRANSFER_QUEUE)+size*sizeof(KM_TRANSFER_REQUEST), GFP_KERNEL);
	if(kmtq==NULL)return NULL;
	kmtq->request=(KM_TRANSFER_REQUEST *)((u8 *)kmtq)+sizeof(KM_TRANSFER_QUEUE);
	kmtq->size=size;
	kmtq->first=0;
	kmtq->last=0;
	spin_lock_init(&(kmtq->lock));
	for(i=0;i<kmtq->size;i++)
		kmtq->request[i].flag=KM_TRANSFER_NOP;
	return kmtq;
}

static int km_fire_transfer_request(KM_TRANSFER_QUEUE *kmtq)
{
	if(kmtq->request[kmtq->first].flag & KM_TRANSFER_IN_PROGRESS){
		return 0;
	}
	while(kmtq->first!=kmtq->last){
		if(kmtq->request[kmtq->first].flag!=KM_TRANSFER_NOP){
			kmtq->request[kmtq->first].flag|=KM_TRANSFER_IN_PROGRESS;
			return 1;		
		}
		kmtq->first++;
		if(kmtq->first>=kmtq->size)kmtq->first=0;
	}
	return 0;
}

int km_add_transfer_request(KM_TRANSFER_QUEUE *kmtq, 
			    KM_STREAM *stream, int buffer, int flag,
			    void (*start_transfer)(KM_TRANSFER_REQUEST *kmtr), 
			    void *user_data)
{
	int last;
	last=kmtq->last;
	last++;
	if(last>=kmtq->size)last=0;
	if(kmtq->request[last].flag!=KM_TRANSFER_NOP){
		printk("km: GUI_DMA queue (size=%d) is full first=%d last=%d flag=0x%08x\n", 
		       kmtq->size,
		       kmtq->first, kmtq->last,
		       kmtq->request[last].flag);
		return -1;
	}

	kmtq->request[last].stream=stream;
	kmtq->request[last].buffer=buffer;
	kmtq->request[last].flag=flag;
	kmtq->request[last].start_transfer=start_transfer;
	kmtq->request[last].user_data=user_data;
	kmtq->last=last;
/* check if any transfer has been scheduled */
	if(km_fire_transfer_request(kmtq)){
		kmtq->request[kmtq->first].start_transfer(&(kmtq->request[kmtq->first]));
		return 0;
	}
	return 0;
}

/* this function could be called from an interrupt handler.
   Be careful with spin_locks */
void km_signal_transfer_completion(KM_TRANSFER_QUEUE *kmtq)
{
	KM_TRANSFER_REQUEST *request;
	request=&(kmtq->request[kmtq->first]);
	if(request->stream->dvb.kmsbi!=NULL)
		request->stream->dvb.kmsbi[request->buffer].flag&=~KM_STREAM_BUF_BUSY;
	wake_up_interruptible(request->stream->dvb.dataq);
/* this operation is atomic as the value written is 32 bits */
	request->flag=KM_TRANSFER_NOP;
	wmb();
	if(km_fire_transfer_request(kmtq)){
		request=&(kmtq->request[kmtq->first]);	
		request->start_transfer(request);
		return;
	}
}

void km_purge_queue(KM_TRANSFER_QUEUE *kmtq)
{
	printk("Purging transfer queue\n");
	spin_lock(&(kmtq->lock));
	memset(kmtq->request, 0, kmtq->size*sizeof(*(kmtq->request)));
	kmtq->first=0;
	kmtq->last=0;
	spin_unlock(&(kmtq->lock));
}

int acknowledge_dma(KM_STRUCT *kms)
{
	km_signal_transfer_completion(&(kms->gui_dma_queue));
	return 0;
}

int install_irq_handler(KM_STRUCT *kms, void *handler, char *tag)
{
	int result;
	result=request_irq(kms->irq, handler, SA_SHIRQ, tag, (void *)kms);
	if(result==-EINVAL){
		printk(KERN_ERR "km: bad irq number or handler\n");
		goto fail;
	}
	if(result==-EBUSY){
		printk(KERN_ERR "km: IRQ %ld busy\n", kms->irq);
		goto fail;
	}
	if(result<0){
		printk(KERN_ERR "km: could not install irq handler\n");
		goto fail;
	}
	return 0;
 fail:
	printk(KERN_ERR " Could not install irq handler...\n");
	printk(KERN_ERR " Perhaps you need to let your BIOS assign an IRQ to your video card\n");
	return -1;
}


int start_video_capture(KM_STRUCT *kms)
{
	int result;
	u32 buf_size;
	spin_lock(&(kms->kms_lock));
	if(kms->gdq_usage==0){
		km_purge_queue(&(kms->gui_dma_queue));
	}
	kms->gdq_usage++;
	if((kms->is_capture_active==NULL)||(kms->get_window_parameters==NULL)||(kms->allocate_dvb==NULL)){
		result=-ENOTSUPP;
		goto fail;
	}
	if(!kms->is_capture_active(kms)){
		printk(KERN_ERR "km: no data is available until AVview or xawtv is started\n");
		result=-ENODATA;
		goto fail;
	}

	kms->get_window_parameters(kms, &(kms->vwin));
	buf_size=kms->vwin.width*kms->vwin.height*2;
	printk("Capture buf size=%d\n", buf_size);
	spin_unlock(&(kms->kms_lock));

	/* Spin lock has to be released before calling allocate_dvb
	 * which may sleep!
	 */

	if(kms->allocate_dvb(&(kms->capture), km_buffers, buf_size)<0){
		spin_lock(&(kms->kms_lock));
		result=-ENOMEM;
		goto fail;
	}
	if(!verify_dvb(kms, &(kms->capture))){
		if(kms->deallocate_dvb!=NULL)kms->deallocate_dvb(&(kms->capture));
		spin_lock(&(kms->kms_lock));
		result=-ENOTSUPP;
		goto fail;
	}
	kmd_signal_state_change(kms->kmd);
	spin_lock(&(kms->kms_lock));
	if(kms->start_transfer!=NULL){
		kms->start_transfer(kms);
		spin_unlock(&(kms->kms_lock));
		return 0;
	}
	result=0;

 fail:
	kms->gdq_usage--;
	spin_unlock(&(kms->kms_lock));
	return result;
}

void stop_video_capture(KM_STRUCT *kms)
{
	spin_lock(&(kms->kms_lock));
	if(kms->stop_transfer!=NULL)kms->stop_transfer(kms);
	if(kms->deallocate_dvb!=NULL){
		kms->deallocate_dvb(&(kms->capture));
		kmd_signal_state_change(kms->kmd);
	}
	kms->gdq_usage--;
	if(kms->gdq_usage==0){
		km_purge_queue(&(kms->gui_dma_queue));
	}
	spin_unlock(&(kms->kms_lock));
}

int start_vbi_capture(KM_STRUCT *kms)
{
	int result;
	long buf_size;
	spin_lock(&(kms->kms_lock));
	if(kms->gdq_usage==0){
		km_purge_queue(&(kms->gui_dma_queue));
	}
	kms->gdq_usage++;
	if((kms->is_vbi_active==NULL)||(kms->get_vbi_buf_size==NULL)||(kms->allocate_dvb==NULL)){
		result=-ENOTSUPP;
		goto fail;
	}
	if(!kms->is_vbi_active(kms)){
		printk("km: no data is available until AVview or xawtv is started\n");
		result=-ENODATA;
		goto fail;
	}

	buf_size=kms->get_vbi_buf_size(kms);
	printk("vbi_buf_size=%ld\n", buf_size);
	spin_unlock(&(kms->kms_lock));

	if(kms->allocate_dvb(&(kms->vbi), km_buffers, buf_size)<0){
		spin_lock(&(kms->kms_lock));
		result=-ENOMEM;
		goto fail;
	}
	if(!verify_dvb(kms, &(kms->vbi))){
		if(kms->deallocate_dvb!=NULL)kms->deallocate_dvb(&(kms->vbi));
		spin_lock(&(kms->kms_lock));
		result=-ENOTSUPP;
		goto fail;
	}

	kmd_signal_state_change(kms->kmd);
	spin_lock(&(kms->kms_lock));
	if(kms->start_vbi_transfer!=NULL){
		kms->start_vbi_transfer(kms);
		spin_unlock(&(kms->kms_lock));
		return 0;
	}
	result=0;

 fail:
	kms->gdq_usage--;
	spin_unlock(&(kms->kms_lock));
	return result;
}

void stop_vbi_capture(KM_STRUCT *kms)
{
	spin_lock(&(kms->kms_lock));
	if(kms->stop_vbi_transfer!=NULL)kms->stop_vbi_transfer(kms);
	if(kms->deallocate_dvb!=NULL){
		kms->deallocate_dvb(&(kms->vbi));
		kmd_signal_state_change(kms->kmd);
	}
	kms->gdq_usage--;
	if(kms->gdq_usage==0){
		km_purge_queue(&(kms->gui_dma_queue));
	}
	spin_unlock(&(kms->kms_lock));
}

/* you shouldn't be having more than 3 actually.. - 1 agp and 2 pci - 
   number of slots + bandwidth issues */
#define MAX_DEVICES 10
KM_STRUCT km_devices[MAX_DEVICES];
int num_devices=0;

int find_kmfl(KM_FIELD *kmfl, char *id)
{
	int i;
	for(i=0;kmfl[i].type!=KM_FIELD_TYPE_EOL;i++){
		if(!strcmp(kmfl[i].name, id))return i;
	}
	printk(KERN_ERR "km: internal error: could not find field %s\n", id);
	return 0;
}

KM_FIELD kmfl_template[]={
	{ type: KM_FIELD_TYPE_STATIC,
	  name: "DEVICE_ID",
	  changed: 0,
	  lock: NULL,
	  priv: NULL,
	  read_complete: NULL,
	}, 
	{ type: KM_FIELD_TYPE_STATIC,
	  name: "LOCATION_ID",
	  changed: 0,
	  lock: NULL,
	  priv: NULL,
	  read_complete: NULL,
	}, 
	{ type: KM_FIELD_TYPE_STATIC,
	  name: "INSTANCE_ID",
	  changed: 0,
	  lock: NULL,
	  priv: NULL,
	  read_complete: NULL,
	}, 
	{ type: KM_FIELD_TYPE_DYNAMIC_INT,
	  name: "VSYNC_COUNT",
	  changed: 0,
	  lock: NULL,
	  priv: NULL,
	  read_complete: NULL,
	}, 
	{ type: KM_FIELD_TYPE_DYNAMIC_INT,
	  name: "VBLANK_COUNT",
	  changed: 0,
	  lock: NULL,
	  priv: NULL,
	  read_complete: NULL,
	}, 
	{ type: KM_FIELD_TYPE_DYNAMIC_INT,
	  name: "VLINE_COUNT",
	  changed: 0,
	  lock: NULL,
	  priv: NULL,
	  read_complete: NULL,
	}, 
	{ type: KM_FIELD_TYPE_DYNAMIC_INT,
	  name: "V4L_DEVICE",
	  changed: 0,
	  lock: NULL,
	  priv: NULL,
	  read_complete: NULL,
	},
	{ type: KM_FIELD_TYPE_LEVEL_TRIGGER,
	  name: "VIDEO_STREAM_ACTIVE",
	  changed: 0,
	  lock: NULL,
	  priv: NULL,
	  read_complete: NULL,
	},
	{ type: KM_FIELD_TYPE_DYNAMIC_INT,
	  name: "VIDEO_STREAM_DATA_UNIT",
	  changed: 0,
	  lock: NULL,
	  priv: NULL,
	  read_complete: NULL,
	},
	{ type: KM_FIELD_TYPE_DYNAMIC_INT,
	  name: "VIDEO_STREAM_INFO_DATA_UNIT",
	  changed: 0,
	  lock: NULL,
	  priv: NULL,
	  read_complete: NULL,
	},
	{ type: KM_FIELD_TYPE_DYNAMIC_INT,
	  name: "VBI_DEVICE",
	  changed: 0,
	  lock: NULL,
	  priv: NULL,
	  read_complete: NULL,
	},
	{ type: KM_FIELD_TYPE_DYNAMIC_INT,
	  name: "VBI_STREAM_DATA_UNIT",
	  changed: 0,
	  lock: NULL,
	  priv: NULL,
	  read_complete: NULL,
	},
	{ type: KM_FIELD_TYPE_DYNAMIC_INT,
	  name: "VBI_STREAM_INFO_DATA_UNIT",
	  changed: 0,
	  lock: NULL,
	  priv: NULL,
	  read_complete: NULL,
	},
	{ type: KM_FIELD_TYPE_LEVEL_TRIGGER,
	  name: "VBI_STREAM_ACTIVE",
	  changed: 0,
	  lock: NULL,
	  priv: NULL,
	  read_complete: NULL,
	},
	{ type: KM_FIELD_TYPE_EOL
	}
};

int video_stream_on(KM_STRUCT *kms)
{
	printk("Video stream on\n");
	return 0;
}

int video_stream_off(KM_STRUCT *kms)
{
	printk("Video stream off\n");
	return 0;
}

int vbi_stream_on(KM_STRUCT *kms)
{
	printk("VBI stream on\n");
	return 0;
}

int vbi_stream_off(KM_STRUCT *kms)
{
	printk("VBI stream off\n");
	return 0;
}

static int __devinit km_probe(struct pci_dev *dev, const struct pci_device_id *pci_id)
{
	KM_STRUCT *kms;

	char *tag;

	/* too many */
	if(num_devices>=MAX_DEVICES)return -1;

#ifdef LINUX_2_6
#ifdef CONFIG_PCI_NAMES
	printk(KERN_INFO "km: probing %s\n",dev->pretty_name);
#else
	printk(KERN_INFO "km: probing %s\n",pci_name(dev));
#endif
#else
	printk(KERN_INFO "km: probing %s\n",dev->name);
#endif

	switch(pci_id->driver_data){
	case HARDWARE_RADEON:
		tag="km_ati (Radeon)";
		break;
	case HARDWARE_MACH64:
		tag="km_ati (Mach64)";
		break;
	case HARDWARE_RAGE128:
		tag="km_ati (Rage128)";
		break;
	default:
		tag="km_ati (unknown)";
	}

	kms=&(km_devices[num_devices]);
	memset(kms, 0, sizeof(KM_STRUCT));
	if (pci_enable_device(dev))
		return -EIO;	
	kms->dev=dev;
	kms->irq=dev->irq;
	kms->gui_dma_queue.request=kms->gui_dma_request;
	kms->gui_dma_queue.size=10;
	kms->gui_dma_queue.last=0;
	kms->gui_dma_queue.first=0;
	spin_lock_init(&(kms->gui_dma_queue.lock));
	memset(kms->gui_dma_request, 0, kms->gui_dma_queue.size*sizeof(KM_TRANSFER_REQUEST));

	atomic_set(&(kms->recursion_count),0);
	kms->interrupt_count=0;
	kms->irq_handler=NULL;
	if(km_buffers<2)km_buffers=2;
	if(km_buffers>=MAX_FRAME_BUFF_NUM)km_buffers=MAX_FRAME_BUFF_NUM-1;
	kms->capture.du=-1;
	kms->capture.info_du=-1;
	kms->vbi.du=-1;
	kms->vbi.info_du=-1;
	spin_lock_init(&(kms->kms_lock));
	printk(KERN_DEBUG "km: using irq %ld\n", kms->irq);
	printk(KERN_DEBUG "Register aperture is 0x%08llx 0x%08llx\n", (long long)pci_resource_start(dev, 2), (long long)pci_resource_len(dev, 2));
/*
  if (!request_mem_region(pci_resource_start(dev,2),
  pci_resource_len(dev,2),
  tag)) {
  return -EBUSY;
  }
*/
	kms->reg_aperture=ioremap(pci_resource_start(dev, 2), pci_resource_len(dev, 2));
	printk(KERN_DEBUG "kms variables: reg_aperture=0x%p\n",
	       kms->reg_aperture);
	switch(pci_id->driver_data){
#ifndef OMIT_RADEON_DRIVER
	case HARDWARE_RADEON:
		kms->is_capture_active=radeon_is_capture_active;
		kms->is_vbi_active=radeon_is_vbi_active;
		kms->get_window_parameters=radeon_get_window_parameters;
		kms->get_vbi_buf_size=radeon_get_vbi_buf_size;
		kms->start_transfer=radeon_start_transfer;
		kms->stop_transfer=radeon_stop_transfer;
		kms->start_vbi_transfer=radeon_start_vbi_transfer;
		kms->stop_vbi_transfer=radeon_stop_vbi_transfer;
		kms->allocate_dvb=generic_allocate_dvb;
		kms->deallocate_dvb=generic_deallocate_dvb;
		kms->irq_handler=radeon_km_irq;
		kms->init_hardware=radeon_init_hardware;
		kms->uninit_hardware=radeon_uninit_hardware;
		kms->verify_page=radeon_verify_page;
		break;
#endif
#ifndef OMIT_MACH64_DRIVER
	case HARDWARE_MACH64:
		kms->is_capture_active=mach64_is_capture_active;
		kms->get_window_parameters=mach64_get_window_parameters;
		kms->start_transfer=mach64_start_transfer;
		kms->stop_transfer=mach64_stop_transfer;
		kms->allocate_dvb=generic_allocate_dvb;
		kms->deallocate_dvb=generic_deallocate_dvb;
		kms->irq_handler=mach64_km_irq;
		break;
#endif
#ifndef OMIT_RAGE128_DRIVER
	case HARDWARE_RAGE128:
		kms->is_capture_active=rage128_is_capture_active;
		kms->get_window_parameters=rage128_get_window_parameters;
		kms->start_transfer=rage128_start_transfer;
		kms->stop_transfer=rage128_stop_transfer;
		kms->allocate_dvb=generic_allocate_dvb;
		kms->deallocate_dvb=generic_deallocate_dvb;
		kms->irq_handler=rage128_km_irq;
		break;
#endif
	default:
		printk("Unknown hardware type %ld\n", pci_id->driver_data);
	}
	if((kms->irq_handler==NULL) || (install_irq_handler(kms, kms->irq_handler, tag)<0)){
		iounmap(kms->reg_aperture);
		release_mem_region(pci_resource_start(dev,2),
				   pci_resource_len(dev,2));
		pci_set_drvdata(dev, NULL);
		return -EIO;
	}
	if(kms->init_hardware!=NULL){
		if(kms->init_hardware(kms)<0){
			free_irq(kms->irq, kms);
			iounmap(kms->reg_aperture);
			release_mem_region(pci_resource_start(dev,2),
					   pci_resource_len(dev,2));
			pci_set_drvdata(dev, NULL);
			return -EIO;		
		}
	}
	if (init_km_v4l(kms)){
		free_irq(kms->irq, kms);
		iounmap(kms->reg_aperture);
		release_mem_region(pci_resource_start(dev,2),
				   pci_resource_len(dev,2));
		pci_set_drvdata(dev, NULL);
		return -EIO;		
	}
	printk("sizeof(kmfl_template)=%ld sizeof(KM_FIELD)=%ld\n", (unsigned long)sizeof(kmfl_template), (unsigned long)sizeof(KM_FIELD));

	kms->kmfl=kmalloc(sizeof(kmfl_template), GFP_KERNEL);
	memcpy(kms->kmfl, kmfl_template, sizeof(kmfl_template));

#define FIELD(name)   kms->kmfl[find_kmfl(kms->kmfl, name)]

#ifdef LINUX_2_6
#ifdef CONFIG_PCI_NAMES
	FIELD("DEVICE_ID").data.c.string=kmalloc(strlen(dev->pretty_name)+1, GFP_KERNEL);
	memcpy(FIELD("DEVICE_ID").data.c.string, dev->pretty_name, strlen(dev->pretty_name)+1);
#else
	FIELD("DEVICE_ID").data.c.string=kmalloc(strlen(pci_name(dev))+1, GFP_KERNEL);
	memcpy(FIELD("DEVICE_ID").data.c.string, pci_name(dev), strlen(pci_name(dev))+1);
#endif
#else
	FIELD("DEVICE_ID").data.c.string=kmalloc(strlen(dev->name)+1, GFP_KERNEL);
	memcpy(FIELD("DEVICE_ID").data.c.string, dev->name, strlen(dev->name)+1);
#endif

	FIELD("LOCATION_ID").data.c.string=kmalloc(strlen(pci_name(dev))+10, GFP_KERNEL);
	sprintf(FIELD("LOCATION_ID").data.c.string, "PCI:%s", pci_name(dev));

	FIELD("INSTANCE_ID").data.c.string=kmalloc(20, GFP_KERNEL);
	sprintf(FIELD("INSTANCE_ID").data.c.string, "KM_DEVICE:%d", num_devices);

	FIELD("VSYNC_COUNT").data.i.field=(u32 *)&(kms->vsync_count);

	FIELD("VBLANK_COUNT").data.i.field=(u32 *)&(kms->vblank_count);

	FIELD("VLINE_COUNT").data.i.field=(u32 *)&(kms->vline_count);

	FIELD("V4L_DEVICE").data.i.field=(u32 *)&(kms->vd->minor);

	FIELD("VIDEO_STREAM_ACTIVE").data.t.zero2one=
		(int (*)(void *))start_video_capture;
	FIELD("VIDEO_STREAM_ACTIVE").data.t.one2zero=
		(void (*)(void *))stop_video_capture;
	FIELD("VIDEO_STREAM_ACTIVE").data.t.priv=kms;

	FIELD("VIDEO_STREAM_DATA_UNIT").data.i.field=&(kms->capture.du);

	FIELD("VIDEO_STREAM_INFO_DATA_UNIT").data.i.field=&(kms->capture.info_du);

	FIELD("VBI_DEVICE").data.i.field=&(kms->vbi_vd->minor);

	FIELD("VBI_STREAM_DATA_UNIT").data.i.field=&(kms->vbi.du);

	FIELD("VBI_STREAM_INFO_DATA_UNIT").data.i.field=&(kms->vbi.info_du);

	FIELD("VBI_STREAM_ACTIVE").data.t.zero2one=
		(int (*)(void *))start_video_capture;
	FIELD("VBI_STREAM_ACTIVE").data.t.one2zero=
		(void (*)(void *))stop_video_capture;
	FIELD("VBI_STREAM_ACTIVE").data.t.priv=kms;

	kms->kmd=add_km_device(kms->kmfl, kms);
#ifdef LINUX_2_6
#ifdef CONFIG_PCI_NAMES
	printk("Device %s %s (0x%04x:0x%04x) corresponds to /dev/video%d\n",
	       dev->pretty_name, pci_name(dev), dev->vendor, dev->device, kms->vd->minor);
#else
	printk("Device %s (0x%04x:0x%04x) corresponds to /dev/video%d\n",
	       pci_name(dev), dev->vendor, dev->device, kms->vd->minor);
#endif
#else
	printk("Device %s %s (0x%04x:0x%04x) corresponds to /dev/video%d\n",
	       dev->name, pci_name(dev), dev->vendor, dev->device, kms->vd->minor);
#endif
	pci_set_master(dev);
	printk("kms variables: reg_aperture=%p\n",kms->reg_aperture);
	num_devices++;
	return 0;
}

static void __devexit km_remove(struct pci_dev *pci_dev)
{
	KM_STRUCT *kms = 0;
	int i;
	for (i=0;i<num_devices;++i)
		if (km_devices[i].dev==pci_dev) kms = &(km_devices[i]);
	if (!kms){
		printk(KERN_ERR "km_remove: couldn't find km_device corresponding to pci_dev at %p\n",pci_dev);
		return;
	}
	printk("Removing Kmultimedia supported device /dev/video%d. Interrupt_count=%ld\n", kms->vd->minor,  kms->interrupt_count);
	if(kms->uninit_hardware!=NULL)kms->uninit_hardware(kms);
	remove_km_device(kms->kmd);
	cleanup_km_v4l(kms);
	free_irq(kms->irq, kms);

	iounmap(kms->reg_aperture);
	kfree(kms->kmfl[0].data.c.string);
	kfree(kms->kmfl[1].data.c.string);
	kfree(kms->kmfl[2].data.c.string);
	kfree(kms->kmfl);
	kms->kmfl=NULL;
/* 
   release_mem_region(pci_resource_start(pci_dev,2),
   pci_resource_len(pci_dev,2));
*/
}

#ifndef PCI_DEVICE_ID_ATI_RADEON_BB 
#define PCI_DEVICE_ID_ATI_RADEON_BB 0x4242
#endif

#ifndef PCI_DEVICE_ID_ATI_RADEON_IF
#define PCI_DEVICE_ID_ATI_RADEON_IF    0x4966
#endif

#ifndef PCI_DEVICE_ID_ATI_RADEON_QD
#define PCI_DEVICE_ID_ATI_RADEON_QD      0x5144
#endif

#ifndef PCI_DEVICE_ID_ATI_RADEON_QF
#define PCI_DEVICE_ID_ATI_RADEON_QF      0x5146
#endif

#ifndef PCI_DEVICE_ID_ATI_RADEON_QG
#define PCI_DEVICE_ID_ATI_RADEON_QG      0x5147
#endif

#ifndef PCI_DEVICE_ID_ATI_RADEON_QM	 
#define PCI_DEVICE_ID_ATI_RADEON_QM      0x514c
#endif

#ifndef PCI_DEVICE_ID_ATI_RADEON_QN	 
#define PCI_DEVICE_ID_ATI_RADEON_QN      0x514d
#endif

#ifndef PCI_DEVICE_ID_ATI_RADEON_QW
#define PCI_DEVICE_ID_ATI_RADEON_QW	 0x5157
#endif                                             

#ifndef PCI_DEVICE_ID_ATI_RADEON_N1
#define PCI_DEVICE_ID_ATI_RADEON_N1    0x5159
#endif

#ifndef PCI_DEVICE_ID_ATI_RADEON_N2
#define PCI_DEVICE_ID_ATI_RADEON_N2    0x515a
#endif

#ifndef PCI_DEVICE_ID_ATI_RADEON_RA
#define PCI_DEVICE_ID_ATI_RADEON_RA    0x5144
#endif

#ifndef PCI_DEVICE_ID_ATI_RADEON_RB
#define PCI_DEVICE_ID_ATI_RADEON_RB    0x5145
#endif

#ifndef PCI_DEVICE_ID_ATI_RADEON_RC
#define PCI_DEVICE_ID_ATI_RADEON_RC    0x5146
#endif

#ifndef PCI_DEVICE_ID_ATI_RADEON_RD
#define PCI_DEVICE_ID_ATI_RADEON_RD    0x5147
#endif


/*-------------------------------------------------------------------------*/

static struct pci_device_id km_pci_tbl [] = {
	/* mach64 cards */
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215GB,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_MACH64},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215GD,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_MACH64},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215GI,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_MACH64},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215GP,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_MACH64},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215GQ,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_MACH64},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215XL,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_MACH64},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215GT,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_MACH64},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215GTB,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_MACH64},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215_LB,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_MACH64},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215_LD,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_MACH64},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215_LG,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_MACH64},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215_LI,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_MACH64},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215_LM,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_MACH64},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215_LN,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_MACH64},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215_LR,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_MACH64},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215_LS,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_MACH64},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_264_LT,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_MACH64},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_264VT,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_MACH64},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_264VU,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_MACH64},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_264VV,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_MACH64},
	/* Rage 128 */
	/* kernels 2.6.3 and up changed the spelling here */
#ifdef PCI_DEVICE_ID_ATI_Rage128_PA
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_Rage128_PA,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_Rage128_PB,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_Rage128_PC,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_Rage128_PD,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_Rage128_PE,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},
#else
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PA,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PB,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PC,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PD,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PE,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},
#endif
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PF,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PG,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PH,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PI,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PJ,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PK,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PL,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PM,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PN,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PO,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PP,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PQ,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PR,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PS,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PT,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PU,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PV,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PW,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PX,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_TR,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RE,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RF,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RG,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},
	/* kernel 2.6.3 does not have the following defined */
#ifdef PCI_DEVICE_ID_ATI_RAGE128_RH
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RH,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RI,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},
#endif
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RK,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RL,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},
#ifdef PCI_DEVICE_ID_ATI_RAGE128_RM
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RM,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RN,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RO,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},
#endif
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_LE,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_LF,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},
#ifdef PCI_DEVICE_ID_ATI_RAGE128_TF
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_TF,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},
#endif
#ifdef PCI_DEVICE_ID_ATI_RAGE128_U1
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_U1,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_U2,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_U3,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},
#endif
	/* Radeons */
	/* Kernel 2.6.15 does not define these */
#ifdef PCI_DEVICE_ID_ATI_RADEON_LE
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_LE,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RADEON},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_LF,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RADEON},
#endif
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_N1,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RADEON},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_N2,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RADEON},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_RA,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RADEON},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_QD,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RADEON},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_QF,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RADEON},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_QG,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RADEON},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_QM,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RADEON},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_QN,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RADEON},
	{PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_QW,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RADEON},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_RB,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RADEON},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_RC,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RADEON},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_RD,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RADEON},
	/* Radeon200 i.e. 8500 */
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_BB,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RADEON},	
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_IF,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RADEON},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_QL,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RADEON},
	{PCI_DEVICE( 0x0000, 0x0000 )}
};

MODULE_DEVICE_TABLE(pci, km_pci_tbl);


static struct pci_driver km_pci_driver = {
	.name		= "Km",
	.id_table	= km_pci_tbl,
	.probe		= km_probe,
	.remove		= __devexit_p(km_remove)
};


#ifdef MODULE
static int __init km_init_module(void)
{

	/* this does not do anything useful at the moment */
	request_module("km_api_drv");
	request_module("videodev");

	printk(KERN_INFO "Kmultimedia module version %s loaded\n", KM_VERSION);
	printk(KERN_DEBUG "Page size is %ld sizeof(bm_list_descriptor)=%ld sizeof(KM_STRUCT)=%ld\n", PAGE_SIZE, (unsigned long)sizeof(bm_list_descriptor), (unsigned long)sizeof(KM_STRUCT));
	num_devices=0;
	return pci_module_init( &km_pci_driver );
}

void __exit km_cleanup_module(void)
{
	pci_unregister_driver( &km_pci_driver );
}

module_init(km_init_module);
module_exit(km_cleanup_module);

#endif
