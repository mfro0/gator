/*     km preliminary version

       (C) Vladimir Dergachev 2001-2002
       
       GNU Public License
       
*/

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
#include "mach64_reg.h"

void mach64_wait_for_fifo(KM_STRUCT *kms, int entries)
{
long count;
u32 a;
count=1000;
while(((a=readl(kms->reg_aperture+MACH64_FIFO_STAT))&0xFFFF)>((u32)(0x8000>>entries))){
	udelay(1);
	count--;
	if(count<0){
		printk(KERN_ERR "km: mach64 FIFO locked up\n");
		return;
		}
	}
}

void mach64_wait_for_idle(KM_STRUCT *kms)
{
u32 a;
long count;
mach64_wait_for_fifo(kms,16);
count=1000;
while(((a=readl(kms->reg_aperture+MACH64_GUI_STAT)) & 0x1)!=0){
	udelay(1);
	count--;
	if(count<0){
		printk(KERN_ERR "km: mach64 engine lock up\n");
		return;
		}
	}
}

int mach64_is_capture_active(KM_STRUCT *kms)
{
return (readl(kms->reg_aperture+MACH64_CAP0_CONFIG) & 0x1);
}

void mach64_get_window_parameters(KM_STRUCT *kms, struct video_window *vwin)
{
u32 a;
vwin->x=0;
vwin->y=0;
mach64_wait_for_idle(kms);
a=readl(kms->reg_aperture+MACH64_CAP0_X_WIDTH);
vwin->width=(a>>16)/2;
a=readl(kms->reg_aperture+MACH64_CAP0_START_END);
vwin->height=(((a>>16)& 0xffff)-(a & 0xffff))+1;
printk("mach64_get_window_parameters: width=%d height=%d\n", vwin->width, vwin->height);
}

void mach64_start_transfer(KM_STRUCT *kms)
{
u32 a;
a=readl(kms->reg_aperture+MACH64_BUS_CNTL);
writel((a | (3<<1) )&(~(1<<6)), kms->reg_aperture+MACH64_BUS_CNTL);
a=readl(kms->reg_aperture+MACH64_CRTC_INT_CNTL);
KM_DEBUG("CRTC_INT_CNTL=0x%08x\n", a);
writel(a|MACH64_CAPBUF0_INT_ACK|MACH64_CAPBUF1_INT_ACK|MACH64_BUSMASTER_INT_ACK, kms->reg_aperture+MACH64_CRTC_INT_CNTL);
writel(a|MACH64_CAPBUF0_INT_EN|MACH64_CAPBUF1_INT_EN|MACH64_BUSMASTER_INT_EN, kms->reg_aperture+MACH64_CRTC_INT_CNTL);
}

void mach64_stop_transfer(KM_STRUCT *kms)
{
u32 a;

a=readl(kms->reg_aperture+MACH64_CRTC_INT_CNTL);
writel(a& ~(MACH64_CAPBUF0_INT_EN|MACH64_CAPBUF1_INT_EN), kms->reg_aperture+MACH64_CRTC_INT_CNTL);
}

static int mach64_setup_single_frame_buffer(KM_STRUCT *kms, SINGLE_FRAME *frame, long offset)
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
		frame->dma_table[i].command=count | MACH64_DMA_GUI_COMMAND__EOL;
		}
	}
return 0;
}

static void mach64_start_frame_transfer_buf0(KM_STRUCT *kms)
{
long offset, status;
if(kms->frame_info[FRAME_ODD].buffer==NULL)return;
kms->frame_info[FRAME_ODD].timestamp=jiffies;
mach64_wait_for_idle(kms);
offset=readl(kms->reg_aperture+MACH64_CAP0_BUF0_OFFSET);
mach64_setup_single_frame_buffer(kms, &(kms->frame_info[FRAME_ODD]), offset);
#if 0 
/* no analog for mach64.. yet ? */
/* wait for at least one available queue */
do {
	status=readl(kms->reg_aperture+MACH64_DMA_GUI_STATUS);
	KM_DEBUG("status=0x%08x\n", status);
	} while (!(status & 0x1f));
#endif
/* start transfer */
if(kms->frame_info[FRAME_ODD].dma_active)KM_DEBUG("DMA overrun\n");
if(kms->frame_info[FRAME_ODD].buf_ptr!=kms->frame_info[FRAME_ODD].buf_free){
	kms->overrun++;
	KM_DEBUG("Data overrun\n");
	}
kms->total_frames++;
kms->frame_info[FRAME_ODD].dma_active=1;
writel(kvirt_to_pa(kms->frame_info[FRAME_ODD].dma_table)|MACH64_SYSTEM_TRIGGER_VIDEO_TO_SYSTEM, 
	kms->reg_aperture+MACH64_BM_SYSTEM_TABLE);
KM_DEBUG("start_frame_transfer_buf0\n");
}

static void mach64_start_frame_transfer_buf0_even(KM_STRUCT *kms)
{
long offset, status;
if(kms->frame_info[FRAME_EVEN].buffer==NULL)return;
kms->frame_info[FRAME_EVEN].timestamp=jiffies;
mach64_wait_for_idle(kms);
offset=readl(kms->reg_aperture+MACH64_CAP0_BUF0_EVEN_OFFSET);
mach64_setup_single_frame_buffer(kms, &(kms->frame_info[FRAME_EVEN]), offset);
#if 0 
/* no analog for mach64.. yet ? */
/* wait for at least one available queue */
do {
	status=readl(kms->reg_aperture+MACH64_DMA_GUI_STATUS);
	KM_DEBUG("status=0x%08x\n", status);
	} while (!(status & 0x1f));
#endif
/* start transfer */
if(kms->frame_info[FRAME_EVEN].dma_active)KM_DEBUG("DMA overrun\n");
if(kms->frame_info[FRAME_EVEN].buf_ptr!=kms->frame_info[FRAME_EVEN].buf_free){
	kms->overrun++;
	KM_DEBUG("Data overrun\n");
	}
kms->total_frames++;
kms->frame_info[FRAME_EVEN].dma_active=1;
writel(kvirt_to_pa(kms->frame_info[FRAME_EVEN].dma_table)|MACH64_SYSTEM_TRIGGER_VIDEO_TO_SYSTEM, 
	kms->reg_aperture+MACH64_BM_SYSTEM_TABLE);
KM_DEBUG("start_frame_transfer_buf0_even\n");
}

static int mach64_is_capture_irq_active(KM_STRUCT *kms)
{
long status;
status=readl(kms->reg_aperture+MACH64_CRTC_INT_CNTL);
if(!(status & (MACH64_CAPBUF0_INT_ACK|MACH64_CAPBUF1_INT_ACK)))return 0;
mach64_wait_for_idle(kms);
writel(ACK_INTERRUPT(status, MACH64_CAPBUF0_INT_ACK|MACH64_CAPBUF1_INT_ACK), kms->reg_aperture+MACH64_CRTC_INT_CNTL);
KM_DEBUG("CRTC_INT_CNTL=0x%08x\n", status);
/* do not start dma transfer if capture is not active anymore */
if(!mach64_is_capture_active(kms))return 1;
if(status & MACH64_CAPBUF0_INT_ACK)mach64_start_frame_transfer_buf0(kms);
if(status & MACH64_CAPBUF1_INT_ACK)mach64_start_frame_transfer_buf0_even(kms); 
return 1;
}

void mach64_km_irq(int irq, void *dev_id, struct pt_regs *regs)
{
KM_STRUCT *kms;
long status, mask;
int count;

kms=dev_id;
kms->interrupt_count++;

/* we should only get tens per second, no more */
count=10000;


while(1){
	/* a little imprecise.. should work for now */
/*	KM_DEBUG("beep %ld\n", kms->interrupt_count); */
	count--;
	if(count<0){
		KM_DEBUG(KERN_ERR "Kmultimedia: IRQ %d locked up, disabling interrupts in the hardware\n", irq);
		mach64_wait_for_idle(kms);
		writel(0, kms->reg_aperture+MACH64_CRTC_INT_CNTL);
		}
	if(!mach64_is_capture_irq_active(kms)){
		status=readl(kms->reg_aperture+MACH64_CRTC_INT_CNTL);
/*		KM_DEBUG("mach64: status=0x%08x\n", status); */
		mach64_wait_for_idle(kms);
		if((status & MACH64_BUSMASTER_INT_ACK))acknowledge_dma(kms);
/*		writel((status|MACH64_BUSMASTER_INT_ACK) & ~(MACH64_ACKS_MASK & ~MACH64_BUSMASTER_INT_ACK), kms->reg_aperture+MACH64_CRTC_INT_CNTL); */
		/* hack admittedly.. but so what ? */
		writel(status, kms->reg_aperture+MACH64_CRTC_INT_CNTL);
		return;
		}
	}
}

int mach64_allocate_v4l_dvb(KM_STRUCT *kms, long size)
{
int i,k;
if(size>(4096*4096/sizeof(bm_list_descriptor))){
	printk("Too large buffer allocation requested: %ld bytes\n", size);
	return -1;
	}
/* allocate data unit to hold video data */
kms->v4l_dvb.size=((size+PAGE_SIZE-1)/PAGE_SIZE)*PAGE_SIZE;
kms->v4l_dvb.n=kms->num_buffers;
kms->v4l_dvb.free=kms->v4l_free;
kms->v4l_dvb.ptr=kms->v4l_ptr;
kms->v4l_du=km_allocate_data_virtual_block(&(kms->v4l_dvb), S_IFREG | S_IRUGO);
/* allocate data unit to hold field info */
kms->v4l_dvb_info.size=kms->num_buffers*sizeof(FIELD_INFO);
kms->v4l_dvb_info.n=1;
kms->v4l_dvb_info.free=&(kms->info_free);
kms->info_free=kms->num_buffers*sizeof(FIELD_INFO);
kms->v4l_dvb_info.ptr=&(kms->info_ptr);
kms->v4l_info_du=km_allocate_data_virtual_block(&(kms->v4l_dvb_info), S_IFREG | S_IRUGO);
if(kms->v4l_du<0)return -1;
for(k=0;k<kms->num_buffers;k++){
	kms->v4l_dvb.free[k]=size;
	kms->frame_info[k].buf_free=size;
	kms->frame_info[k].buf_ptr=size;
	kms->frame_info[k].buf_size=kms->v4l_dvb.size;
	kms->frame_info[k].buffer=kms->v4l_dvb.ptr[k];
	kms->frame_info[k].dma_active=0;
	kms->frame_info[k].dma_table=rvmalloc(4096);
	memset(kms->frame_info[k].dma_table, 0, 4096);
	/* create DMA table */
	for(i=0;i<(kms->frame_info[k].buf_size/PAGE_SIZE);i++){
		kms->frame_info[k].dma_table[i].to_addr=kvirt_to_pa(kms->frame_info[k].buffer+i*PAGE_SIZE);
		#if 0
		printk("entry virt %p phys %p %s\n", kms->frame_info[k].buffer+i*PAGE_SIZE, kms->frame_info[k].dma_table[i].to_addr,
		((unsigned long)kms->frame_info[k].dma_table[i].to_addr)<64*1024*1024?"*":"");
		#endif
		if(kvirt_to_pa(kms->frame_info[k].buffer+i*PAGE_SIZE)!=kvirt_to_bus(kms->frame_info[k].buffer+i*PAGE_SIZE)){
			printk(KERN_ERR "pa!=bus for entry %d frame %d\n", i, k);
			}
		}
	}
return 0;
}

int mach64_deallocate_v4l_dvb(KM_STRUCT *kms)
{
int k;
for(k=0;k<kms->num_buffers;k++){
	kms->frame_info[k].buf_free=0;
	kms->frame_info[k].buf_ptr=0;
	kms->frame_info[k].buf_size=0;
	kms->frame_info[k].buffer=NULL;
	kms->frame_info[k].dma_active=0;
	rvfree(kms->frame_info[k].dma_table, 4096);
	kms->frame_info[k].dma_table=NULL;
	}
km_deallocate_data(kms->v4l_info_du);
kms->v4l_info_du=-1;
km_deallocate_data(kms->v4l_du);
kms->v4l_du=-1;
return 0;
}

int mach64_allocate_single_frame_buffer(KM_STRUCT *kms, SINGLE_FRAME *frame, long size)
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
	printk(KERN_ERR "km: failed to allocate buffer of %ld bytes\n", frame->buf_size);
	return -1;
	}
printk("Allocated %ld bytes for a single frame buffer\n", frame->buf_size);
/*data1.dma_table=__get_dma_pages(GFP_KERNEL | GFP_DMA, 1);*/
frame->dma_table=rvmalloc(4096);
printk("frame table virtual address 0x%p, physical address: 0x%08lx, bus address: 0x%08lx\n",
	frame->dma_table, kvirt_to_pa(frame->dma_table), kvirt_to_bus(frame->dma_table));
if(frame->dma_table==NULL){
	printk(KERN_ERR "km: failed to allocate DMA SYSTEM table\n");
	rvfree(frame->buffer, frame->buf_size);
	return -1;
	}
/* create DMA table */
for(i=0;i<(frame->buf_size/PAGE_SIZE);i++){
	frame->dma_table[i].to_addr=kvirt_to_pa(frame->buffer+i*PAGE_SIZE);
	frame->dma_table[i].reserved=0;
	}
return 0;
}

