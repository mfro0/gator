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
#include "radeon_reg.h"



/*
MODULE_PARM(irq_num, "i");
MODULE_PARM_DESC(irq_num, "interrupt request number");
*/

static int allocate_single_frame_buffer(KM_STRUCT *kms, SINGLE_FRAME *frame, long size)
{
int i;
frame->buf_free=size;
frame->buf_size=((size+PAGE_SIZE-1)/PAGE_SIZE)*PAGE_SIZE;
frame->buf_ptr=0;
frame->buffer=rvmalloc(frame->buf_size);
if(frame->buffer==NULL){
	printk(KERN_ERR "km: failed to allocate buffer of %d bytes\n", frame->buf_size);
	return -1;
	}
printk("Allocated %ld bytes for a single frame buffer\n", frame->buf_size);
/*data1.dma_table=__get_dma_pages(GFP_KERNEL | GFP_DMA, 1);*/
frame->dma_table=rvmalloc(4096);
printk("frame table virtual address 0x%08x, physical address: 0x%08x, bus address: 0x%08x\n",
	frame->dma_table, kvirt_to_pa(frame->dma_table), kvirt_to_bus(frame->dma_table));
if(frame->dma_table==NULL){
	printk(KERN_ERR "km: failed to allocate DMA SYSTEM table\n");
	rvfree(frame->buffer, frame->buf_size);
	return -1;
	}
/* create DMA table */
for(i=0;i<(frame->buf_size/PAGE_SIZE);i++){
	frame->dma_table[i].to_addr=kvirt_to_pa(frame->buffer+i*PAGE_SIZE);
	}
return 0;
}

void deallocate_single_frame_buffer(KM_STRUCT *kms, SINGLE_FRAME *frame)
{
if(frame->buffer!=NULL)rvfree(frame->buffer, frame->buf_size);
if(frame->dma_table!=NULL)rvfree(frame->dma_table, 4096);
frame->buffer=NULL;
frame->dma_table=NULL;
}

static int setup_single_frame_buffer(KM_STRUCT *kms, SINGLE_FRAME *frame, long offset)
{
int i;
long count;
count=frame->buf_free;
for(i=0;i<(frame->buf_size/PAGE_SIZE);i++){
	frame->dma_table[i].from_addr=offset+i*PAGE_SIZE;
	if(count>PAGE_SIZE){
		frame->dma_table[i].command=PAGE_SIZE;
		count-=PAGE_SIZE;
		} else {
		frame->dma_table[i].command=count | DMA_GUI_COMMAND__EOL;
		}
	}
return 0;
}

static void start_frame_transfer(KM_STRUCT *kms)
{
long offset, status;
offset=readl(kms->reg_aperture+CAP0_BUF0_OFFSET);
setup_single_frame_buffer(kms, &(kms->frame), offset);
/* wait for at least one available queue */
do {
	status=readl(kms->reg_aperture+DMA_GUI_STATUS);
	} while (!(status & 0x1f));
/* start transfer */
writel(kvirt_to_pa(kms->frame.dma_table), kms->reg_aperture+DMA_GUI_TABLE_ADDR);
printk("start_frame_transfer\n");
}

static int is_capture_irq_active(int irq, KM_STRUCT *kms)
{
long status, mask;
status=readl(kms->reg_aperture+GEN_INT_STATUS);
if(!(status & (1<<8)))return 0;
status=readl(kms->reg_aperture+CAP_INT_STATUS);
mask=readl(kms->reg_aperture+CAP_INT_CNTL);
if(!(status & mask))return 0;
writel(status & mask, kms->reg_aperture+CAP_INT_STATUS);
start_frame_transfer(kms);
return 1;
}

static void km_irq(int irq, void *dev_id, struct pt_regs *regs)
{
KM_STRUCT *kms;
long status, mask;
int count;

kms=dev_id;
kms->interrupt_count++;
printk("beep %ld\n", kms->interrupt_count);

count=100;

while(1){
	if(!is_capture_irq_active(irq, kms)){
		status=readl(kms->reg_aperture+kms->reg_status);
		mask=readl(kms->reg_aperture+kms->reg_mask);
		if(!(status & mask))return;
		printk("beep %ld\n", kms->interrupt_count);
		writel(status & mask, kms->reg_aperture+kms->reg_status);
		count--;
		if(count<0){
			printk(KERN_ERR "Kmultimedia: IRQ %d locked up, disabling interrupts in the hardware\n", irq);
			writel(0, kms->reg_aperture+kms->reg_mask);
			}
		}
	}
}

/* you shouldn't be having more than 2 actually.. */
#define MAX_RADEONS 1
KM_STRUCT radeon[10];
int num_radeons=0;

static int __devinit km_probe(struct pci_dev *dev, const struct pci_device_id *pci_id)
{
KM_STRUCT *kms;
int result;
/* too many */
if(num_radeons>=MAX_RADEONS)return -1;

kms=&(radeon[num_radeons]);
num_radeons++;
kms->dev=dev;
kms->irq=dev->irq;
printk("km: using irq %d\n", kms->irq);
kms->interrupt_count=0;
if (pci_enable_device(dev))
	return -EIO;
if (!request_mem_region(pci_resource_start(dev,2),
			pci_resource_len(dev,2),
			"km")) {
	return -EBUSY;
	}

kms->reg_status=0x44;
kms->reg_mask=0x40;
kms->reg_aperture=ioremap(pci_resource_start(dev, 2), 0x1000);
printk("Register aperture is 0x%08x 0x%08x\n", pci_resource_start(dev, 2), pci_resource_len(dev, 2));
printk("kms variables: reg_aperture=0x%08x reg_mask=0x%08x reg_status=0x%08x\n",
	kms->reg_aperture, kms->reg_mask, kms->reg_status);
if(allocate_single_frame_buffer(kms, &(kms->frame), 640*240*2)<0){
	result=-1;
	goto fail;
	}

result=request_irq(kms->irq, km_irq, SA_SHIRQ, "km", (void *)kms);
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
printk("kms variables: reg_aperture=0x%08x reg_mask=0x%08x reg_status=0x%08x\n",
	kms->reg_aperture, kms->reg_mask, kms->reg_status);
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
deallocate_single_frame_buffer(kms, &(kms->frame));
iounmap(kms->reg_aperture);
release_mem_region(pci_resource_start(pci_dev,2),
                           pci_resource_len(pci_dev,2));
pci_set_drvdata(pci_dev, NULL);
}

static struct pci_device_id km_pci_tbl[] __devinitdata = {
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

MODULE_DEVICE_TABLE(pci, km_pci_tbl);

static struct pci_driver km_pci_driver = {
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
printk("Page size is %ld sizeof(bm_list_descriptor)=%ld sizeof(KM_STRUCT)=%ld\n", PAGE_SIZE, sizeof(bm_list_descriptor), sizeof(KM_STRUCT));
num_radeons=0;

result=pci_module_init(&km_pci_driver);

return result;
}

void cleanup_module(void)
{
pci_unregister_driver(&km_pci_driver);
return;
}


#endif
