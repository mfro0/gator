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

#include "km.h"
#include "km_memory.h"
#include "km_v4l.h"
#include "radeon.h"
#include "mach64.h"

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




/* you shouldn't be having more than 3 actually.. - 1 agp and 2 pci - 
    number of slots + bandwidth issues */
#define MAX_RADEONS 10
KM_STRUCT radeon[10];
int num_radeons=0;

static int __devinit radeon_km_probe(struct pci_dev *dev, const struct pci_device_id *pci_id)
{
KM_STRUCT *kms;
int result;
/* too many */
if(num_radeons>=MAX_RADEONS)return -1;

kms=&(radeon[num_radeons]);
num_radeons++;
kms->dev=dev;
kms->irq=dev->irq;
kms->is_capture_active=radeon_is_capture_active;
kms->get_window_parameters=radeon_get_window_parameters;
kms->start_transfer=radeon_start_transfer;
kms->stop_transfer=radeon_stop_transfer;
kms->allocate_single_frame_buffer=radeon_allocate_single_frame_buffer;
kms->deallocate_single_frame_buffer=generic_deallocate_single_frame_buffer;
printk("km: using irq %d\n", kms->irq);
kms->interrupt_count=0;
init_waitqueue_head(&(kms->frameq));
if (pci_enable_device(dev))
	return -EIO;
if (!request_mem_region(pci_resource_start(dev,2),
			pci_resource_len(dev,2),
			"km")) {
	return -EBUSY;
	}

kms->reg_aperture=ioremap(pci_resource_start(dev, 2), 0x1000);
printk("Register aperture is 0x%08x 0x%08x\n", pci_resource_start(dev, 2), pci_resource_len(dev, 2));
printk("kms variables: reg_aperture=0x%08x\n",
	kms->reg_aperture);

kms->frame.buffer=NULL;
kms->frame.dma_table=NULL;
kms->frame_even.buffer=NULL;
kms->frame_even.dma_table=NULL;


result=request_irq(kms->irq, radeon_km_irq, SA_SHIRQ, "km", (void *)kms);
if(result==-EINVAL){
	printk(KERN_ERR "km: bad irq number or handler\n");
	goto fail;
	}
if(result==-EBUSY){
	printk(KERN_ERR "km: IRQ %d busy\n", kms->irq);
	goto fail;
	}
if(result<0){
	printk(KERN_ERR "km: could not install irq handler\n");
	goto fail;
	}
init_km_v4l(kms);
pci_set_master(dev);
pci_set_drvdata(dev, kms);
printk("kms variables: reg_aperture=0x%08x\n",
	kms->reg_aperture);
return 0;

fail:
	return -1;
}

static void __devexit km_remove(struct pci_dev *pci_dev)
{
KM_STRUCT *kms;
kms=pci_get_drvdata(pci_dev);
printk("Removing Kmultimedia supported device. interrupt_count=%ld\n", kms->interrupt_count);
cleanup_km_v4l(kms);
free_irq(kms->irq, kms);
kms->deallocate_single_frame_buffer(kms, &(kms->frame));
kms->deallocate_single_frame_buffer(kms, &(kms->frame_even));
iounmap(kms->reg_aperture);
release_mem_region(pci_resource_start(pci_dev,2),
                           pci_resource_len(pci_dev,2));
pci_set_drvdata(pci_dev, NULL);
}

static struct pci_device_id radeon_km_pci_tbl[] __devinitdata = {
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_LE,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_LF,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_N1,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_N2,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_RA,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_RB,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_RC,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
        {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_RD,
         PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
        {0,}
};

MODULE_DEVICE_TABLE(pci, radeon_km_pci_tbl);

static struct pci_driver radeon_km_pci_driver = {
        name:     "km",
        id_table: radeon_km_pci_tbl,
        probe:    radeon_km_probe,
        remove:   km_remove,
};


#ifdef MODULE
static int __init init_module(void)
{
int result;
long i;
printk("Kmultimedia module version %s loaded\n", KM_VERSION);
printk("Page size is %ld sizeof(bm_list_descriptor)=%ld sizeof(KM_STRUCT)=%ld\n", PAGE_SIZE, sizeof(bm_list_descriptor), sizeof(KM_STRUCT));
num_radeons=0;

result=pci_module_init(&radeon_km_pci_driver);

return result;
}

void cleanup_module(void)
{
pci_unregister_driver(&radeon_km_pci_driver);
return;
}


#endif
