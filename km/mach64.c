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

#include <linux/types.h>
#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#ifndef LINUX_2_6
#include <linux/wrapper.h>
#endif
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
a=readl(kms->reg_aperture+MACH64_CAP0_BUF0_EVEN_OFFSET);
kms->buf0_even_offset=a;
a=readl(kms->reg_aperture+MACH64_CAP0_BUF0_OFFSET);
kms->buf0_odd_offset=a;
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

/* stop interrupts */
a=readl(kms->reg_aperture+MACH64_CRTC_INT_CNTL);
writel(a& ~(MACH64_CAPBUF0_INT_EN|MACH64_CAPBUF1_INT_EN), kms->reg_aperture+MACH64_CRTC_INT_CNTL);
wmb();
/* stop outstanding DMA transfers */
a=readl(kms->reg_aperture+MACH64_BUS_CNTL);
writel(a|MACH64_BUS_CNTL__BM_RESET, kms->reg_aperture+MACH64_BUS_CNTL);
}

static int mach64_setup_dma_table(KM_STRUCT *kms, bm_list_descriptor *dma_table, long offset, long free)
{
int i;
long count;
count=free;
for(i=0;;i++){
	dma_table[i].from_addr=offset+i*PAGE_SIZE;
	if(count>PAGE_SIZE){
		dma_table[i].command=PAGE_SIZE;
		count-=PAGE_SIZE;
		} else {
		dma_table[i].command=count | MACH64_DMA_GUI_COMMAND__EOL;
		break;
		}
	}
return 0;
}

static void mach64_start_request_transfer(KM_TRANSFER_REQUEST *kmtr)
{
KM_STRUCT *kms=kmtr->user_data;
mach64_wait_for_idle(kms);
wmb();
writel(kms->capture.dma_table_physical[kmtr->buffer]|MACH64_SYSTEM_TRIGGER_VIDEO_TO_SYSTEM, 
	kms->reg_aperture+MACH64_BM_SYSTEM_TABLE);
}

static void mach64_schedule_request(KM_STRUCT *kms, int buffer, int field)
{
long offset;
KM_STREAM *stream=&(kms->capture);
if(stream->num_buffers<=0){
	return;
	}
if(buffer<0){
	KM_DEBUG("mach64_schedule_request buffer=%d field=%d\n",buffer, field);
	return;
	}
if(field){
	offset=kms->buf0_even_offset;
	stream->dvb.kmsbi[buffer].user_flag&=~KM_FI_ODD;
	} else {
	offset=kms->buf0_odd_offset;
	stream->dvb.kmsbi[buffer].user_flag|=KM_FI_ODD;
	}
KM_DEBUG("buf=%d field=%d\n", buffer, field);
stream->dvb.kmsbi[buffer].timestamp=jiffies;
mach64_setup_dma_table(kms, (stream->dma_table[buffer]), offset, stream->free[buffer]);
/* start transfer */
stream->total_frames++;
stream->dvb.kmsbi[buffer].age=stream->total_frames;
wmb();
km_add_transfer_request(&(kms->gui_dma_queue),
	stream, buffer, KM_TRANSFER_TO_SYSTEM_RAM, mach64_start_request_transfer, kms);
}

irqreturn_t mach64_km_irq(int irq, void *dev_id, struct pt_regs *regs)
{
KM_STRUCT *kms;
long status;
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

	status=readl(kms->reg_aperture+MACH64_CRTC_INT_CNTL);
	KM_DEBUG("CRTC_INT_CNTL=0x%08lx\n", status);
	writel(ACK_INTERRUPT(status, status & (MACH64_CAPBUF0_INT_ACK|
		MACH64_CAPBUF1_INT_ACK|
		MACH64_BUSMASTER_INT_ACK)), kms->reg_aperture+MACH64_CRTC_INT_CNTL);
	if((status & (MACH64_CAPBUF0_INT_ACK|MACH64_CAPBUF1_INT_ACK))){
		/* do not start dma transfer if capture is not active anymore */
		if(mach64_is_capture_active(kms)&& spin_trylock(&(kms->gui_dma_queue.lock))){
			mach64_wait_for_idle(kms);
			if(status & MACH64_CAPBUF0_INT_ACK)mach64_schedule_request(kms, find_free_buffer(&(kms->capture)), 0);
			if(status & MACH64_CAPBUF1_INT_ACK)mach64_schedule_request(kms, find_free_buffer(&(kms->capture)), 1); 
			spin_unlock(&(kms->gui_dma_queue.lock));
			}
		}
	
	if((status & MACH64_BUSMASTER_INT_ACK)){
		mach64_wait_for_idle(kms);
		acknowledge_dma(kms);
		}
	if(!(status & (MACH64_CAPBUF0_INT_ACK|MACH64_CAPBUF1_INT_ACK|MACH64_BUSMASTER_INT_ACK)))
		return IRQ_NONE;
	}
 return IRQ_HANDLED; 
}
