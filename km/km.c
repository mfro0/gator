/*     km preliminary version

       (C) Vladimir Dergachev 2001-2002
       
       GNU Public License
       
*/

#define __KM_C__

#include <linux/autoconf.h>
#if defined(MODULE) && defined(CONFIG_MODVERSIONS)
#define MODVERSIONS
#include <linux/modversions.h>
#endif

#include <linux/types.h>
#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/wrapper.h>
#include <linux/videodev.h>


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

MODULE_DESCRIPTION("km_ati");
MODULE_PARM(km_debug, "i");
MODULE_PARM(km_debug_overruns, "i");
MODULE_PARM(km_buffers, "i");
MODULE_PARM_DESC(km_debug, "kmultimedia debugging level");
MODULE_PARM_DESC(km_debug_overruns, "kmultimedia debug overruns");
MODULE_PARM_DESC(km_buffers, "how many buffers to use for video capture per each device");

int km_debug=0;
int km_debug_overruns=0;
int km_buffers=5;

int find_free_buffer(KM_STRUCT *kms)
{
int i;
int age;
int k;
if(kms->fi==NULL)return -1;
while(kms->fi[kms->next_cap_buf].flag & KM_FI_PINNED)kms->next_cap_buf=kms->fi[kms->next_cap_buf].next;
i=kms->next_cap_buf;
kms->next_cap_buf=kms->fi[kms->next_cap_buf].next;
return i;
}

int find_next_buffer(KM_STRUCT *kms, int buf)
{

}

int generic_allocate_dvb(KM_STRUCT *kms, long size)
{
int i,k;
KM_CHECKPOINT
if(size>(4096*4096/sizeof(bm_list_descriptor))){
	printk("Too large buffer allocation requested: %ld bytes\n", size);
	return -1;
	}
/* allocate data unit to hold video data */
kms->dvb.size=((size+PAGE_SIZE-1)/PAGE_SIZE)*PAGE_SIZE;
kms->dvb.n=kms->num_buffers;
kms->dvb.free=kms->v4l_free;
kms->dvb.ptr=kms->buffer;
kms->v4l_du=km_allocate_data_virtual_block(&(kms->dvb), S_IFREG | S_IRUGO);
if(kms->v4l_du<0)return -1;
KM_CHECKPOINT
/* allocate data unit to hold field info */
kms->dvb_info.size=kms->num_buffers*sizeof(FIELD_INFO);
kms->dvb_info.n=1;
kms->dvb_info.free=&(kms->info_free);
kms->info_free=kms->num_buffers*sizeof(FIELD_INFO);
kms->dvb_info.ptr=&(kms->fi);
kms->v4l_info_du=km_allocate_data_virtual_block(&(kms->dvb_info), S_IFREG | S_IRUGO);
if(kms->v4l_info_du<0){
	km_deallocate_data(kms->v4l_du);
	kms->v4l_du=-1;
	return -1;
	}
/*
  dma_table allocation and initialization is the only hardware dependent part of this function

  However it is common for all ATI chipsets and should be very similar for other PCI cards.
  
  Note: only the buffer offsets (which are computed here) are common, flags do differ between mach64, rage128 and 
  radeon cards.
*/

kms->dma_table=kmalloc(sizeof(*(kms->dma_table))*kms->num_buffers, GFP_KERNEL);
memset(kms->fi, 0 , sizeof(*(kms->fi))*kms->num_buffers);
for(k=0;k<kms->num_buffers;k++){
	kms->dvb.free[k]=size;
	kms->fi[k].flag=0;
	kms->fi[k].next=k+1;
	kms->fi[k].prev=k-1;
	kms->fi[k].age=-1;
	kms->dma_table[k]=rvmalloc(4096);
	kms->v4l_free[k]=size;
	memset(kms->dma_table[k], 0, 4096);
	/* create DMA table */
	for(i=0;i<(kms->dvb.size/PAGE_SIZE);i++){
		kms->dma_table[k][i].to_addr=kvirt_to_pa(kms->buffer[k]+i*PAGE_SIZE);
		if(kvirt_to_pa(kms->buffer[k]+i*PAGE_SIZE)!=kvirt_to_bus(kms->buffer[k]+i*PAGE_SIZE)){
			printk(KERN_ERR "pa!=bus for entry %d frame %d\n", i, k);
			}
		}
	}
kms->fi[0].prev=kms->num_buffers-1;
kms->fi[kms->num_buffers-1].next=0;
kms->next_cap_buf=0;
KM_CHECKPOINT
return 0;
}


int generic_deallocate_dvb(KM_STRUCT *kms)
{
int k;
for(k=0;k<kms->num_buffers;k++){
	rvfree(kms->dma_table[k], 4096);
	}
kfree(kms->dma_table);
kms->dma_table=NULL;
km_deallocate_data(kms->v4l_info_du);
kms->v4l_info_du=-1;
km_deallocate_data(kms->v4l_du);
kms->v4l_du=-1;
return 0;
}

int acknowledge_dma(KM_STRUCT *kms)
{
int i;
for(i=0;i<kms->num_buffers;i++)
if(kms->fi[i].flag & KM_FI_DMA_ACTIVE){
	kms->fi[i].flag&=~KM_FI_DMA_ACTIVE;
	kms->fi[i].timestamp_end=jiffies;
	wake_up_interruptible(&(kms->frameq));
	}
#if 0
if(kms->frame_info[FRAME_EVEN].dma_active){
	kms->frame_info[FRAME_EVEN].dma_active=0;
	if(kms->frame_info[FRAME_EVEN].buf_ptr==kms->frame_info[FRAME_EVEN].buf_free){
		kms->frame_info[FRAME_EVEN].buf_ptr=0;
		if(kms->buf_read_from==1)wake_up_interruptible(&(kms->frameq));
		} else 
	if(km_debug_overruns)printk("overrun buf_ptr=%d buf_free=%d total=%d\n", 
			kms->frame_info[FRAME_ODD].buf_ptr,
			kms->frame_info[FRAME_ODD].buf_free, kms->total_frames);
	}
#endif
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




/* you shouldn't be having more than 3 actually.. - 1 agp and 2 pci - 
    number of slots + bandwidth issues */
#define MAX_DEVICES 10
KM_STRUCT km_devices[10];
int num_devices=0;

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
	{ type: KM_FIELD_TYPE_EOL
	}
	};

static int __devinit km_probe(struct pci_dev *dev, const struct pci_device_id *pci_id)
{
KM_STRUCT *kms;
int result;
char *tag;
/* too many */
if(num_devices>=MAX_DEVICES)return -1;

KM_CHECKPOINT

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
	}
		
kms=&(km_devices[num_devices]);
memset(kms, 0, sizeof(KM_STRUCT));
kms->dev=dev;
kms->irq=dev->irq;
#if 0
kms->frame_info[FRAME_ODD].buffer=NULL;
kms->frame_info[FRAME_ODD].dma_table=NULL;
kms->frame_info[FRAME_EVEN].buffer=NULL;
kms->frame_info[FRAME_EVEN].dma_table=NULL;
#endif
kms->interrupt_count=0;
kms->irq_handler=NULL;
if(km_buffers<2)km_buffers=2;
if(km_buffers>=MAX_FRAME_BUFF_NUM)km_buffers=MAX_FRAME_BUFF_NUM-1;
kms->num_buffers=km_buffers;
kms->v4l_du=-1;
kms->v4l_info_du=-1;
spin_lock_init(&(kms->kms_lock));
printk("km: using irq %ld\n", kms->irq);
init_waitqueue_head(&(kms->frameq));
if (pci_enable_device(dev))
	return -EIO;	
printk("Register aperture is 0x%08lx 0x%08lx\n", pci_resource_start(dev, 2), pci_resource_len(dev, 2));
/*
if (!request_mem_region(pci_resource_start(dev,2),
			pci_resource_len(dev,2),
			tag)) {
	return -EBUSY;
	}
*/
kms->reg_aperture=ioremap(pci_resource_start(dev, 2), pci_resource_len(dev, 2));
printk("kms variables: reg_aperture=0x%p\n",
	kms->reg_aperture);
switch(pci_id->driver_data){
#ifndef OMIT_RADEON_DRIVER
	case HARDWARE_RADEON:
		kms->is_capture_active=radeon_is_capture_active;
		kms->get_window_parameters=radeon_get_window_parameters;
		kms->start_transfer=radeon_start_transfer;
		kms->stop_transfer=radeon_stop_transfer;
		kms->allocate_dvb=generic_allocate_dvb;
		kms->deallocate_dvb=generic_deallocate_dvb;
		kms->irq_handler=radeon_km_irq;
		kms->init_hardware=radeon_init_hardware;
		kms->uninit_hardware=radeon_uninit_hardware;
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
init_km_v4l(kms);
printk("sizeof(kmfl_template)=%d sizeof(KM_FIELD)=%d\n", sizeof(kmfl_template), sizeof(KM_FIELD));

kms->kmfl=kmalloc(sizeof(kmfl_template), GFP_KERNEL);
memcpy(kms->kmfl, kmfl_template, sizeof(kmfl_template));

kms->kmfl[0].data.c.string=kmalloc(strlen(dev->name)+1, GFP_KERNEL);
memcpy(kms->kmfl[0].data.c.string, dev->name, strlen(dev->name)+1);

kms->kmfl[1].data.c.string=kmalloc(strlen(dev->slot_name)+10, GFP_KERNEL);
sprintf(kms->kmfl[1].data.c.string, "PCI:%s", dev->slot_name);

kms->kmfl[2].data.c.string=kmalloc(20, GFP_KERNEL);
sprintf(kms->kmfl[2].data.c.string, "KM_DEVICE:%d", num_devices);

kms->kmfl[3].data.i.field=&(kms->vsync_count);

kms->kmfl[4].data.i.field=&(kms->vblank_count);

kms->kmfl[5].data.i.field=&(kms->vline_count);

kms->kmfl[6].data.i.field=&(kms->vd.minor);

kms->kmfl[7].data.i.field=&(kms->v4l_du);

kms->kmfl[8].data.i.field=&(kms->v4l_info_du);

kms->kmd=add_km_device(kms->kmfl, kms);
printk("Device %s %s (0x%04x:0x%04x) corresponds to /dev/video%d\n",
	dev->name, dev->slot_name, dev->vendor, dev->device, kms->vd.minor);
pci_set_master(dev);
printk("kms variables: reg_aperture=0x%08x\n",
	kms->reg_aperture);
num_devices++;
return 0;

fail:
	return -1;
}


static void __devexit km_remove(struct pci_dev *pci_dev, KM_STRUCT *kms)
{
printk("Removing Kmultimedia supported device /dev/video%d. Interrupt_count=%ld\n", kms->vd.minor,  kms->interrupt_count);
if(kms->uninit_hardware!=NULL)kms->uninit_hardware(kms);
remove_km_device(kms->kmd);
cleanup_km_v4l(kms);
free_irq(kms->irq, kms);
#if 0
kms->deallocate_single_frame_buffer(kms, &(kms->frame_info[FRAME_ODD]));
kms->deallocate_single_frame_buffer(kms, &(kms->frame_info[FRAME_EVEN]));
#endif
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

#ifndef PCI_DEVICE_ID_ATI_RADEON_QD
#define PCI_DEVICE_ID_ATI_RADEON_QD      0x5144
#endif

#ifndef PCI_DEVICE_ID_ATI_RADEON_QF
#define PCI_DEVICE_ID_ATI_RADEON_QF      0x5146
#endif

#ifndef PCI_DEVICE_ID_ATI_RADEON_QG
#define PCI_DEVICE_ID_ATI_RADEON_QG      0x5147
#endif

#ifndef PCI_DEVICE_ID_ATI_RADEON_QW
#define PCI_DEVICE_ID_ATI_RADEON_QW	 0x5157
#endif                                             

static struct pci_device_id km_pci_tbl[] __devinitdata = {
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
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RH,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RI,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RK,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RL,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RM,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RN,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RO,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_LE,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_LF,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_U1,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_U2,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_U3,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RAGE128},	 
	/* Radeons */
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_LE,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RADEON},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_LF,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RADEON},
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
        {0,}
};

MODULE_DEVICE_TABLE(pci, km_pci_tbl);

/*
static struct pci_driver radeon_km_pci_driver = {
        name:     "km",
        id_table: km_pci_tbl,
        probe:    km_probe,
        remove:   km_remove,
};
*/

#ifdef MODULE
static int __init init_module(void)
{
int result;
struct pci_dev *pdev;
const struct pci_device_id *pdid;
printk("Kmultimedia module version %s loaded\n", KM_VERSION);
printk("Page size is %ld sizeof(bm_list_descriptor)=%d sizeof(KM_STRUCT)=%d\n", PAGE_SIZE, sizeof(bm_list_descriptor), sizeof(KM_STRUCT));
num_devices=0;
result=-1;
pci_for_each_dev(pdev){
	if((pdid=pci_match_device(km_pci_tbl, pdev))!=NULL){
		if(km_probe(pdev, pdid)>=0)result=0;
		}
	}
if(result<0)printk("km: **** no supported devices found ****\n");
return result;
}

void cleanup_module(void)
{
int i;
for(i=0;i<num_devices;i++)
	km_remove(km_devices[i].dev, &(km_devices[i]));
return;
}


#endif
