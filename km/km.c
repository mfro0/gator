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
MODULE_PARM_DESC(km_debug, "kmultimedia debugging level");

int km_debug=1;

void generic_deallocate_single_frame_buffer(KM_STRUCT *kms, SINGLE_FRAME *frame)
{
if(frame->buffer!=NULL)rvfree(frame->buffer, frame->buf_size);
if(frame->dma_table!=NULL)rvfree(frame->dma_table, 4096);
frame->buffer=NULL;
frame->dma_table=NULL;
}


int acknowledge_dma(KM_STRUCT *kms)
{
if(kms->frame.dma_active){
	kms->frame.dma_active=0;
	if(kms->frame.buf_ptr==kms->frame.buf_free){
		kms->frame.buf_ptr=0;
		if(kms->buf_read_from==0)wake_up_interruptible(&(kms->frameq));
		}
	}
if(kms->frame_even.dma_active){
	kms->frame_even.dma_active=0;
	if(kms->frame_even.buf_ptr==kms->frame.buf_free){
		kms->frame_even.buf_ptr=0;
		if(kms->buf_read_from==1)wake_up_interruptible(&(kms->frameq));
		}
	}
return 0;
}

int install_irq_handler(KM_STRUCT *kms, void *handler)
{
int result;
result=request_irq(kms->irq, handler, SA_SHIRQ, "km", (void *)kms);
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
	  read_complete: NULL
	}, 
	{ type: KM_FIELD_TYPE_EOL
	}
	};

static int __devinit km_probe(struct pci_dev *dev, const struct pci_device_id *pci_id)
{
KM_STRUCT *kms;
int result;
void *irq_handler=NULL;
/* too many */
if(num_devices>=MAX_DEVICES)return -1;

kms=&(km_devices[num_devices]);
kms->dev=dev;
kms->irq=dev->irq;
kms->frame.buffer=NULL;
kms->frame.dma_table=NULL;
kms->frame_even.buffer=NULL;
kms->frame_even.dma_table=NULL;
kms->interrupt_count=0;

printk("km: using irq %ld\n", kms->irq);
init_waitqueue_head(&(kms->frameq));
if (pci_enable_device(dev))
	return -EIO;	
printk("Register aperture is 0x%08lx 0x%08lx\n", pci_resource_start(dev, 2), pci_resource_len(dev, 2));
if (!request_mem_region(pci_resource_start(dev,2),
			pci_resource_len(dev,2),
			"km")) {
	return -EBUSY;
	}

kms->reg_aperture=ioremap(pci_resource_start(dev, 2), pci_resource_len(dev, 2));
printk("kms variables: reg_aperture=0x%p\n",
	kms->reg_aperture);

switch(pci_id->driver_data){
	case HARDWARE_RADEON:
		kms->is_capture_active=radeon_is_capture_active;
		kms->get_window_parameters=radeon_get_window_parameters;
		kms->start_transfer=radeon_start_transfer;
		kms->stop_transfer=radeon_stop_transfer;
		kms->allocate_single_frame_buffer=radeon_allocate_single_frame_buffer;
		kms->deallocate_single_frame_buffer=generic_deallocate_single_frame_buffer;
		irq_handler=radeon_km_irq;
		break;
	case HARDWARE_MACH64:
		kms->is_capture_active=mach64_is_capture_active;
		kms->get_window_parameters=mach64_get_window_parameters;
		kms->start_transfer=mach64_start_transfer;
		kms->stop_transfer=mach64_stop_transfer;
		kms->allocate_single_frame_buffer=mach64_allocate_single_frame_buffer;
		kms->deallocate_single_frame_buffer=generic_deallocate_single_frame_buffer;
		irq_handler=mach64_km_irq;
		break;
	case HARDWARE_RAGE128:
		kms->is_capture_active=rage128_is_capture_active;
		kms->get_window_parameters=rage128_get_window_parameters;
		kms->start_transfer=rage128_start_transfer;
		kms->stop_transfer=rage128_stop_transfer;
		kms->allocate_single_frame_buffer=rage128_allocate_single_frame_buffer;
		kms->deallocate_single_frame_buffer=generic_deallocate_single_frame_buffer;
		irq_handler=rage128_km_irq;
		break;
	default:
		printk("Unknown hardware type %ld\n", pci_id->driver_data);
		}
if((irq_handler==NULL) || (install_irq_handler(kms, irq_handler)<0)){
	iounmap(kms->reg_aperture);
	release_mem_region(pci_resource_start(dev,2),
        	pci_resource_len(dev,2));
	pci_set_drvdata(dev, NULL);
	return -EIO;
	}
init_km_v4l(kms);
printk("sizeof(kmfl_template)=%d sizeof(KM_FIELD)=%d\n", sizeof(kmfl_template), sizeof(KM_FIELD));
kms->kmfl=kmalloc(sizeof(kmfl_template), GFP_KERNEL);
memcpy(kms->kmfl, kmfl_template, sizeof(kmfl_template));
kms->kmfl[0].data.c.string=kmalloc(strlen(dev->name)+1, GFP_KERNEL);
memcpy(kms->kmfl[0].data.c.string, dev->name, strlen(dev->name)+1);
kms->kmd=add_km_device(kms->kmfl, kms);
printk("Device %s %s (0x%04x:0x%04x) corresponds to /dev/video%d\n",
	dev->name, dev->slot_name, dev->vendor, dev->device, kms->vd.minor);
pci_set_master(dev);
pci_set_drvdata(dev, kms);
printk("kms variables: reg_aperture=0x%08x\n",
	kms->reg_aperture);
num_devices++;
return 0;

fail:
	return -1;
}


static void __devexit km_remove(struct pci_dev *pci_dev)
{
KM_STRUCT *kms;
kms=pci_get_drvdata(pci_dev);
printk("Removing Kmultimedia supported device. interrupt_count=%ld\n", kms->interrupt_count);
remove_km_device(kms->kmd);
cleanup_km_v4l(kms);
free_irq(kms->irq, kms);
kms->deallocate_single_frame_buffer(kms, &(kms->frame));
kms->deallocate_single_frame_buffer(kms, &(kms->frame_even));
iounmap(kms->reg_aperture);
release_mem_region(pci_resource_start(pci_dev,2),
                           pci_resource_len(pci_dev,2));
pci_set_drvdata(pci_dev, NULL);
}

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
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_RB,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RADEON},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_RC,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RADEON},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_RD,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, HARDWARE_RADEON},
        {0,}
};

MODULE_DEVICE_TABLE(pci, km_pci_tbl);

static struct pci_driver radeon_km_pci_driver = {
        name:     "km",
        id_table: km_pci_tbl,
        probe:    km_probe,
        remove:   km_remove,
};


#ifdef MODULE
static int __init init_module(void)
{
int result;
long i;
printk("Kmultimedia module version %s loaded\n", KM_VERSION);
printk("Page size is %ld sizeof(bm_list_descriptor)=%d sizeof(KM_STRUCT)=%d\n", PAGE_SIZE, sizeof(bm_list_descriptor), sizeof(KM_STRUCT));
num_devices=0;

result=pci_module_init(&radeon_km_pci_driver);

return result;
}

void cleanup_module(void)
{
pci_unregister_driver(&radeon_km_pci_driver);
return;
}


#endif
