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
a=readl(kms->reg_aperture+MACH64_CAP0_BUF0_EVEN_OFFSET);
kms->even_offset=a;
a=readl(kms->reg_aperture+MACH64_CAP0_BUF0_OFFSET);
kms->odd_offset=a;
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

static int mach64_setup_single_frame_buffer(KM_STRUCT *kms, bm_list_descriptor *dma_table, long offset, long free)
{
int i;
long count;
count=free;
for(i=0;i<(kms->v4l_dvb.size/PAGE_SIZE);i++){
	dma_table[i].from_addr=offset+i*PAGE_SIZE;
	if(count>PAGE_SIZE){
		dma_table[i].command=PAGE_SIZE;
		count-=PAGE_SIZE;
		} else {
		dma_table[i].command=count | MACH64_DMA_GUI_COMMAND__EOL;
		}
	}
return 0;
}

static void mach64_start_frame_transfer(KM_STRUCT *kms, int buffer, int field)
{
long offset, status;
if(buffer<0){
	KM_DEBUG("start_frame_transfer buffer=%d field=%d\n",buffer, field);
	return;
	}
if(field){
	offset=kms->odd_offset;
	kms->fi[buffer].flag|=KM_FI_ODD;
	} else {
	offset=kms->even_offset;
	kms->fi[buffer].flag&=~KM_FI_ODD;
	}
KM_DEBUG("buf=%d field=%d\n", buffer, field);
kms->fi[buffer].timestamp_start=jiffies;
mach64_wait_for_idle(kms);
mach64_setup_single_frame_buffer(kms, (kms->dma_table[buffer]), offset, kms->v4l_free[buffer]);
#if 0 
/* no analog for mach64.. yet ? */
/* wait for at least one available queue */
do {
	status=readl(kms->reg_aperture+MACH64_DMA_GUI_STATUS);
	KM_DEBUG("status=0x%08x\n", status);
	} while (!(status & 0x1f));
#endif
/* start transfer */
kms->total_frames++;
kms->fi[buffer].flag|=KM_FI_DMA_ACTIVE;
kms->fi[buffer].age=kms->total_frames;
wmb();
writel(kvirt_to_pa(kms->dma_table[buffer])|MACH64_SYSTEM_TRIGGER_VIDEO_TO_SYSTEM, 
	kms->reg_aperture+MACH64_BM_SYSTEM_TABLE);
KM_DEBUG("start_frame_transfer_buf0\n");
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
if(status & MACH64_CAPBUF0_INT_ACK)mach64_start_frame_transfer(kms, find_free_buffer(kms), 1);
if(status & MACH64_CAPBUF1_INT_ACK)mach64_start_frame_transfer(kms, find_free_buffer(kms), 0); 
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
