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
#include <linux/delay.h>


#include "km.h"
#include "km_memory.h"
#include "radeon_reg.h"

void radeon_wait_for_fifo(KM_STRUCT *kms, int entries)
{
long count;
u32 a;
count=10000;
while(((a=readl(kms->reg_aperture+RADEON_RBBM_STATUS))&0x7F)<entries){
	udelay(1);
	count--;
	if(count<0){
		printk(KERN_ERR "km: radeon FIFO locked up\n");
		return;
		}
	}
}

void radeon_wait_for_idle(KM_STRUCT *kms)
{
u32 a;
long count;
a=readl(kms->reg_aperture+RADEON_RBBM_STATUS);
KM_DEBUG("RADEON_RBBM_STATUS=0x%08x\n", a);
radeon_wait_for_fifo(kms,64);
wmb();
count=1000;
while(((a=readl(kms->reg_aperture+RADEON_RBBM_STATUS)) & RADEON_ENGINE_ACTIVE)!=0){
	udelay(1);
	count--;
	if(count<0){
		printk(KERN_ERR "km: radeon engine lock up\n");
		return;
		}
	}
/* according to docs we should do FlushPixelCache here. However we don't really
read back framebuffer data, so no need */
}

void verify_dma_table(KM_STRUCT *kms, SINGLE_FRAME *frame)
{
long i;
for(i=0;i<(frame->buf_size/PAGE_SIZE);i++){
	if(frame->dma_table[i].to_addr!=kvirt_to_pa(frame->buffer+i*PAGE_SIZE)){
		printk(KERN_ERR "Corrupt entry %ld in dma_table %p\n", i, frame->dma_table);
		}
	}
}

int radeon_is_capture_active(KM_STRUCT *kms)
{
return (readl(kms->reg_aperture+RADEON_CAP0_CONFIG) & 0x1);
}

void radeon_get_window_parameters(KM_STRUCT *kms, struct video_window *vwin)
{
u32 a;
vwin->x=0;
vwin->y=0;
radeon_wait_for_idle(kms);
wmb();
a=readl(kms->reg_aperture+RADEON_CAP0_BUF_PITCH);
vwin->width=a/2;
a=readl(kms->reg_aperture+RADEON_CAP0_V_WINDOW);
vwin->height=(((a>>16)& 0xffff)-(a & 0xffff))+1;
printk("radeon_get_window_parameters: width=%d height=%d\n", vwin->width, vwin->height);
}

void radeon_start_transfer(KM_STRUCT *kms)
{
u32 a;

a=readl(kms->reg_aperture+RADEON_BUS_CNTL);
printk("RADEON_BUS_CNTL=0x%08x\n", a);
/* enable bus mastering */
if(a & (1<<6)){ 
	printk("Enabling bus mastering\n");
	writel(a | (3<<1) | (1<<6), kms->reg_aperture+RADEON_BUS_CNTL);
	}
wmb();
writel(3, kms->reg_aperture+RADEON_CAP_INT_STATUS);
writel(1<<30, kms->reg_aperture+RADEON_GEN_INT_STATUS);
a=readl(kms->reg_aperture+RADEON_CAP_INT_CNTL);
writel(a|3, kms->reg_aperture+RADEON_CAP_INT_CNTL);
a=readl(kms->reg_aperture+RADEON_GEN_INT_CNTL);
writel(a|(1<<30), kms->reg_aperture+RADEON_GEN_INT_CNTL);
}

void radeon_stop_transfer(KM_STRUCT *kms)
{
u32 a;

a=readl(kms->reg_aperture+RADEON_CAP_INT_CNTL);
writel(a & ~3, kms->reg_aperture+RADEON_CAP_INT_CNTL);
a=readl(kms->reg_aperture+RADEON_GEN_INT_CNTL);
writel(a & ~(1<<30), kms->reg_aperture+RADEON_GEN_INT_CNTL);
}

static int radeon_setup_single_frame_buffer(KM_STRUCT *kms, SINGLE_FRAME *frame, long offset)
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
		frame->dma_table[i].command=count | RADEON_DMA_GUI_COMMAND__EOL;
		}
	}
/* verify_dma_table(kms, frame); */
return 0;
}

static void radeon_start_frame_transfer_buf0(KM_STRUCT *kms)
{
long offset, status;
if(kms->frame.buffer==NULL)return;
kms->frame.timestamp=jiffies;
offset=readl(kms->reg_aperture+RADEON_CAP0_BUF0_OFFSET);
radeon_setup_single_frame_buffer(kms, &(kms->frame), offset);
wmb();
/* wait for at least one available queue */
do {
	status=readl(kms->reg_aperture+RADEON_DMA_GUI_STATUS);
	KM_DEBUG("status=0x%08lx\n", status);
	} while (!(status & 0x1f));
/* start transfer */
if(kms->frame.dma_active)KM_DEBUG("DMA overrun\n");
if(kms->frame.buf_ptr!=kms->frame.buf_free){
	kms->overrun++;
	KM_DEBUG("Data overrun\n");
	}
kms->total_frames++;
kms->frame.dma_active=1;
wmb();
writel(kvirt_to_pa(kms->frame.dma_table), (u32)(kms->reg_aperture+RADEON_DMA_GUI_TABLE_ADDR)| (0));
KM_DEBUG("start_frame_transfer_buf0\n");
}

static void radeon_start_frame_transfer_buf0_even(KM_STRUCT *kms)
{
long offset, status;
if(kms->frame_even.buffer==NULL)return;
kms->frame_even.timestamp=jiffies;
offset=readl(kms->reg_aperture+RADEON_CAP0_BUF0_EVEN_OFFSET);
radeon_setup_single_frame_buffer(kms, &(kms->frame_even), offset);
wmb();
/* wait for at least one available queue */
do {
	status=readl(kms->reg_aperture+RADEON_DMA_GUI_STATUS);
	KM_DEBUG("status=0x%08lx\n", status);
	} while (!(status & 0x1f));
/* start transfer */
if(kms->frame_even.dma_active)KM_DEBUG("DMA overrun\n");
if(kms->frame_even.buf_ptr!=kms->frame_even.buf_free){
	kms->overrun++;
	KM_DEBUG("Data overrun\n");
	}
kms->total_frames++;
kms->frame_even.dma_active=1;
wmb();
writel(kvirt_to_pa(kms->frame_even.dma_table), (u32)(kms->reg_aperture+RADEON_DMA_GUI_TABLE_ADDR) | (0));
KM_DEBUG("start_frame_transfer_buf0_even\n");
}

static int radeon_is_capture_irq_active(KM_STRUCT *kms)
{
long status, mask;
status=readl(kms->reg_aperture+RADEON_GEN_INT_STATUS);
if(!(status & (1<<8)))return 0;
status=readl(kms->reg_aperture+RADEON_CAP_INT_STATUS);
mask=readl(kms->reg_aperture+RADEON_CAP_INT_CNTL);
if(!(status & mask))return 0;
radeon_wait_for_idle(kms);
wmb();
writel(status & mask, kms->reg_aperture+RADEON_CAP_INT_STATUS);
KM_DEBUG("CAP_INT_STATUS=0x%08x\n", status);
/* do not start dma transfer if capture is not active anymore */
if(!radeon_is_capture_active(kms))return 1;
if(status & 1)radeon_start_frame_transfer_buf0(kms);
if(status & 2)radeon_start_frame_transfer_buf0_even(kms); 
return 1;
}

void radeon_km_irq(int irq, void *dev_id, struct pt_regs *regs)
{
KM_STRUCT *kms;
long status, mask;
int count;

kms=dev_id;
kms->interrupt_count++;

/* we should only get tens per second, no more */
count=10000;

while(1){
/*	KM_DEBUG("beep %ld\n", kms->interrupt_count); */
	if(!radeon_is_capture_irq_active(kms)){
		status=readl(kms->reg_aperture+RADEON_GEN_INT_STATUS);
		mask=readl(kms->reg_aperture+RADEON_GEN_INT_CNTL);
		if(!(status & mask))return;
		radeon_wait_for_idle(kms);
		wmb();
		if(status & (1<<30))acknowledge_dma(kms);
		writel(status & mask, kms->reg_aperture+RADEON_GEN_INT_STATUS);
		count--;
		if(count<0){
			KM_DEBUG(KERN_ERR "Kmultimedia: IRQ %d locked up, disabling interrupts in the hardware\n", irq);
			writel(0, kms->reg_aperture+RADEON_GEN_INT_STATUS);
			}
		}
	}
}


int radeon_allocate_single_frame_buffer(KM_STRUCT *kms, SINGLE_FRAME *frame, long size)
{
int i;
if(size>(4096*4096/sizeof(bm_list_descriptor))){
	printk("Too large buffer allocation requested: %ld bytes\n", size);
	return -1;
	}
frame->buf_free=size;
frame->buf_size=((size+PAGE_SIZE-1)/PAGE_SIZE)*PAGE_SIZE;
frame->buf_ptr=frame->buf_free; /* no data is available */
frame->buffer=rvmalloc(frame->buf_size);
frame->dma_active=0;
if(frame->buffer==NULL){
	printk(KERN_ERR "km: failed to allocate buffer of %d bytes\n", frame->buf_size);
	return -1;
	}
printk("Allocated %ld bytes for a single frame buffer\n", frame->buf_size);
frame->dma_table=__get_dma_pages(GFP_KERNEL | GFP_DMA, 1);
/*frame->dma_table=rvmalloc(4096);*/
printk("frame table virtual address 0x%p, physical address: 0x%08lx, bus address: 0x%08lx\n",
	frame->dma_table, kvirt_to_pa(frame->dma_table), kvirt_to_bus(frame->dma_table));
if(frame->dma_table==NULL){
	printk(KERN_ERR "km: failed to allocate DMA SYSTEM table\n");
	rvfree(frame->buffer, frame->buf_size);
	return -1;
	}
memset(frame->dma_table, 0, 4096);
/* create DMA table */
printk("Frame %p\n", frame);
for(i=0;i<(frame->buf_size/PAGE_SIZE);i++){
	frame->dma_table[i].to_addr=kvirt_to_pa(frame->buffer+i*PAGE_SIZE);
	printk("entry virt %p phys %p %s\n", frame->buffer+i*PAGE_SIZE, frame->dma_table[i].to_addr,
		((unsigned long)frame->dma_table[i].to_addr)<64*1024*1024?"*":"");
	if(kvirt_to_pa(frame->buffer+i*PAGE_SIZE)!=kvirt_to_bus(frame->buffer+i*PAGE_SIZE)){
		printk(KERN_ERR "pa!=bus for entry %ld frame %p\n", i, frame);
		}
	}
return 0;
}

