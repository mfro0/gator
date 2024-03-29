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
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#ifndef LINUX_2_6
#include <linux/config.h>
#include <linux/wrapper.h>
#endif
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

int radeon_check_mc_settings(KM_STRUCT *kms)
{
u32 aperture, aperture_size;
u32 mc_fb_location;
u32 mc_agp_location;
u32 new_mc_fb_location;

aperture=pci_resource_start(kms->dev,0);
radeon_wait_for_idle(kms);
aperture_size=readl(kms->reg_aperture+RADEON_CONFIG_APER_SIZE);
mc_fb_location=readl(kms->reg_aperture+RADEON_MC_FB_LOCATION);
mc_agp_location=readl(kms->reg_aperture+RADEON_MC_AGP_LOCATION);


new_mc_fb_location=((aperture>>16)&0xffff)|
        ((aperture+aperture_size-1)&0xffff0000);

if((new_mc_fb_location!=mc_fb_location) ||
   ((mc_agp_location & 0xffff)!=(((aperture+aperture_size)>>16)&0xffff))){
        printk("WARNING !   Radeon memory controller is misconfigured, disabling capture\n");
        printk("WARNING !   upgrade your Xserver and DRM driver\n");
	return 0;
	}
return 1;
}

int radeon_is_capture_active(KM_STRUCT *kms)
{
return (radeon_check_mc_settings(kms) && (readl(kms->reg_aperture+RADEON_CAP0_CONFIG) & 0x1));
}

int radeon_is_vbi_active(KM_STRUCT *kms)
{
return (radeon_check_mc_settings(kms) && (readl(kms->reg_aperture+RADEON_CAP0_CONFIG) & (1<<13)));
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
vwin->height=(((a>>16)& 0xffff)-(a & 0xffff));
a=readl(kms->reg_aperture+RADEON_CAP0_BUF0_EVEN_OFFSET);
kms->buf0_even_offset=a;
a=readl(kms->reg_aperture+RADEON_CAP0_BUF0_OFFSET);
kms->buf0_odd_offset=a;
a=readl(kms->reg_aperture+RADEON_CAP0_BUF1_EVEN_OFFSET);
kms->buf1_even_offset=a;
a=readl(kms->reg_aperture+RADEON_CAP0_BUF1_OFFSET);
kms->buf1_odd_offset=a;
printk("radeon_get_window_parameters: width=%d height=%d\n", vwin->width, vwin->height);
}

long radeon_get_vbi_buf_size(KM_STRUCT *kms)
{
u32 h_window, v_window;
radeon_wait_for_idle(kms);
wmb();
h_window=readl(kms->reg_aperture+RADEON_CAP0_VBI_H_WINDOW);
v_window=readl(kms->reg_aperture+RADEON_CAP0_VBI_V_WINDOW);
kms->vbi0_offset=readl(kms->reg_aperture+RADEON_CAP0_VBI0_OFFSET);
kms->vbi1_offset=readl(kms->reg_aperture+RADEON_CAP0_VBI1_OFFSET);
kms->vbi_width=(h_window>>16);
kms->vbi_height=(v_window>>16)-(v_window & 0xffff)+1;
kms->vbi_start=(v_window & 0xffff)+2; /* Rage Theatre has an off by 2 count */
return (kms->vbi_width*kms->vbi_height);
}

void radeon_purge_gui_dma_queue(KM_STRUCT *kms)
{
u32 a;
int count;

a=readl(kms->reg_aperture+RADEON_DMA_GUI_STATUS);
writel(a | RADEON_DMA_GUI_STATUS__ABORT, kms->reg_aperture+RADEON_DMA_GUI_STATUS);
wmb();
count=1000;
while((a=readl(kms->reg_aperture+RADEON_DMA_GUI_STATUS))&RADEON_DMA_GUI_STATUS__ACTIVE){
	udelay(1);
	count--;
	if(count<0){
		printk("km: GUI_DMA_QUEUE abort stalled\n");
		return;
		}
	}
wmb();
writel(a & ~ RADEON_DMA_GUI_STATUS__ABORT, kms->reg_aperture+RADEON_DMA_GUI_STATUS);
wmb();
a=readl(kms->reg_aperture+RADEON_DMA_GUI_STATUS);
printk("km: DMA_GUI_STATUS=0x%08x entries=%d\n", a, a & 0x1f);
}

int radeon_start_gui_dma_queue(KM_STRUCT *kms)
{
u32 a;
int count;

a=readl(kms->reg_aperture+RADEON_DMA_GUI_STATUS);
a=a& ~(3<<22); /* set swap control to no swap */
if(a & RADEON_DMA_GUI_STATUS__ABORT){
	if(a & RADEON_DMA_GUI_STATUS__ACTIVE){
		count=20;
		while(count){
			writel(a, kms->reg_aperture+RADEON_DMA_GUI_STATUS);	
			udelay(1);
			a=readl(kms->reg_aperture+RADEON_DMA_GUI_STATUS);
			a=a& ~(3<<22); /* set swap control to no swap */
			if(!(a & RADEON_DMA_GUI_STATUS__ACTIVE)){
				break;
				}
			count--;
			}
		if(!count){
			printk("km: DMA_GUI_QUEUE stalled after abort\n");
			return 0;
			}
		}
	wmb();
	
	writel(a & ~ RADEON_DMA_GUI_STATUS__ABORT, kms->reg_aperture+RADEON_DMA_GUI_STATUS);	
	}
return 1;
}



void radeon_start_transfer(KM_STRUCT *kms)
{
u32 a;

if(!radeon_check_mc_settings(kms))return;
wmb();
a=readl(kms->reg_aperture+RADEON_BUS_CNTL);
printk("RADEON_BUS_CNTL=0x%08x\n", a);
/* enable bus mastering */
if(a & (1<<6)){ 
	printk("Enabling bus mastering\n");
	writel(a | (3<<1) | (1<<6), kms->reg_aperture+RADEON_BUS_CNTL);
	}
wmb();
if(kms->gdq_usage==1){
	printk("Starting GUIDMA queue\n");
	if(!radeon_start_gui_dma_queue(kms))return;
	writel(INT_BIT_GUIDMA, kms->reg_aperture+RADEON_GEN_INT_STATUS);
	wmb();
	a=readl(kms->reg_aperture+RADEON_GEN_INT_CNTL);
	writel(a|INT_BIT_GUIDMA, kms->reg_aperture+RADEON_GEN_INT_CNTL);
	}
wmb();
writel((CAP_INT_BIT_BUF0|CAP_INT_BIT_BUF0_EVEN|
	CAP_INT_BIT_BUF1|CAP_INT_BIT_BUF1_EVEN), kms->reg_aperture+RADEON_CAP_INT_STATUS);
wmb();
a=readl(kms->reg_aperture+RADEON_CAP_INT_CNTL);
writel(a|(CAP_INT_BIT_BUF0|CAP_INT_BIT_BUF0_EVEN|
	CAP_INT_BIT_BUF1|CAP_INT_BIT_BUF1_EVEN), kms->reg_aperture+RADEON_CAP_INT_CNTL);
}

void radeon_stop_transfer(KM_STRUCT *kms)
{
u32 a;

/* stop interrupts */
a=readl(kms->reg_aperture+RADEON_CAP_INT_CNTL);
writel(a & ~(CAP_INT_BIT_BUF0|CAP_INT_BIT_BUF0_EVEN|
	     CAP_INT_BIT_BUF1|CAP_INT_BIT_BUF1_EVEN), kms->reg_aperture+RADEON_CAP_INT_CNTL);
wmb();
writel((CAP_INT_BIT_BUF0|CAP_INT_BIT_BUF0_EVEN|
	CAP_INT_BIT_BUF1|CAP_INT_BIT_BUF1_EVEN), kms->reg_aperture+RADEON_CAP_INT_STATUS);
wmb();
if(kms->gdq_usage==1){
	printk("Stopping GUIDMA queue\n");
	a=readl(kms->reg_aperture+RADEON_GEN_INT_CNTL);
	writel(a & ~INT_BIT_GUIDMA, kms->reg_aperture+RADEON_GEN_INT_CNTL);
	wmb();
	writel(INT_BIT_GUIDMA, kms->reg_aperture+RADEON_GEN_INT_STATUS);
	/* stop outstanding DMA transfers */
	a=readl(kms->reg_aperture+RADEON_DMA_GUI_STATUS);
	if(a & RADEON_DMA_GUI_STATUS__ACTIVE){
		radeon_purge_gui_dma_queue(kms);
		}
	}
}

void radeon_start_vbi_transfer(KM_STRUCT *kms)
{
u32 a;

if(!radeon_check_mc_settings(kms))return;
wmb();
a=readl(kms->reg_aperture+RADEON_BUS_CNTL);
printk("RADEON_BUS_CNTL=0x%08x\n", a);
/* enable bus mastering */
if(a & (1<<6)){ 
	printk("Enabling bus mastering\n");
	writel(a | (3<<1) | (1<<6), kms->reg_aperture+RADEON_BUS_CNTL);
	}
wmb();
if(kms->gdq_usage==1){
	printk("Starting GUIDMA queue\n");
	if(!radeon_start_gui_dma_queue(kms))return;
	writel(INT_BIT_GUIDMA, kms->reg_aperture+RADEON_GEN_INT_STATUS);
	wmb();
	a=readl(kms->reg_aperture+RADEON_GEN_INT_CNTL);
	writel(a|INT_BIT_GUIDMA, kms->reg_aperture+RADEON_GEN_INT_CNTL);
	}
wmb();
writel((CAP_INT_BIT_VBI0|CAP_INT_BIT_VBI1), kms->reg_aperture+RADEON_CAP_INT_STATUS);
wmb();
a=readl(kms->reg_aperture+RADEON_CAP_INT_CNTL);
writel(a|(CAP_INT_BIT_VBI0|CAP_INT_BIT_VBI1), kms->reg_aperture+RADEON_CAP_INT_CNTL);
}

void radeon_stop_vbi_transfer(KM_STRUCT *kms)
{
u32 a;
/* stop interrupts */
a=readl(kms->reg_aperture+RADEON_CAP_INT_CNTL);
writel(a & ~(CAP_INT_BIT_VBI0|CAP_INT_BIT_VBI1), kms->reg_aperture+RADEON_CAP_INT_CNTL);
wmb();
writel((CAP_INT_BIT_VBI0|CAP_INT_BIT_VBI1), kms->reg_aperture+RADEON_CAP_INT_STATUS);
wmb();
if(kms->gdq_usage==1){
	printk("Stopping GUIDMA queue\n");
	a=readl(kms->reg_aperture+RADEON_GEN_INT_CNTL);
	writel(a & ~INT_BIT_GUIDMA, kms->reg_aperture+RADEON_GEN_INT_CNTL);
	wmb();
	writel(INT_BIT_GUIDMA, kms->reg_aperture+RADEON_GEN_INT_STATUS);
	/* stop outstanding DMA transfers */
	a=readl(kms->reg_aperture+RADEON_DMA_GUI_STATUS);
	if(a & RADEON_DMA_GUI_STATUS__ACTIVE){
		radeon_purge_gui_dma_queue(kms);
		}
	}
}

static int radeon_setup_dma_table(KM_STRUCT *kms, bm_list_descriptor *dma_table, long offset, long free)
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
		dma_table[i].command=count | RADEON_DMA_GUI_COMMAND__EOL ;
		break;
		}
	}
/* verify_dma_table(kms, frame); */
return 0;
}

static void radeon_start_request_transfer(KM_TRANSFER_REQUEST *kmtr)
{
u32 status;
int count;
KM_STRUCT *kms=kmtr->user_data;
wmb();
/* wait for at least one available queue */
count=0;
do {
	status=readl(kms->reg_aperture+RADEON_DMA_GUI_STATUS);
	if((status & RADEON_DMA_GUI_STATUS__ACTIVE))printk("DMA_GUI_QUEUE is active\n");
	count++;
	if(count==10){
		printk(KERN_ERR "radeon DMA_GUI queue full DMA_GUI_STATUS=0x%08x\n", status);
		}
	KM_DEBUG("status=0x%08x\n", status);
	} while (!(status & 0x1f));
wmb();
writel(kmtr->stream->dma_table_physical[kmtr->buffer]&~1, kms->reg_aperture+RADEON_DMA_GUI_TABLE_ADDR);
}

static void radeon_schedule_request(KM_STRUCT *kms, KM_STREAM *stream, u32 offset, int odd)
{
int buffer;
/* do not start dma transfer if stream is not being used anymore */
if(!spin_trylock(&(stream->lock))){
	printk("km: Stream is locked\n");
	return;
	}
if(stream->num_buffers<=0){
	spin_unlock(&(stream->lock));
	return;
	}
buffer=find_free_buffer(stream);
if(buffer<0){
	printk(KERN_ERR "radeon_schedule_request: could not find free buffer buffer=%d offset=0x%08x odd=%d\n", 
		buffer, offset, odd);
	spin_unlock(&(stream->lock));
	return;
	}
if(odd)
	stream->dvb.kmsbi[buffer].user_flag|=KM_FI_ODD;
	else
	stream->dvb.kmsbi[buffer].user_flag&=~KM_FI_ODD;
	
KM_DEBUG("buf=%d offset=0x%08x odd=%d\n", buffer, offset, odd);
stream->dvb.kmsbi[buffer].timestamp=jiffies;
/* start transfer */
stream->total_frames++;
stream->dvb.kmsbi[buffer].age=stream->total_frames;
stream->dvb.kmsbi[buffer].flag|=KM_STREAM_BUF_BUSY;
spin_unlock(&(stream->lock));
radeon_setup_dma_table(kms, (stream->dma_table[buffer]), offset, stream->free[buffer]);
wmb();
km_add_transfer_request(&(kms->gui_dma_queue),
	stream, buffer, KM_TRANSFER_TO_SYSTEM_RAM, radeon_start_request_transfer, kms);
}

irqreturn_t radeon_km_irq(int irq, void *dev_id, struct pt_regs *regs)
{
KM_STRUCT *kms;
u32 status, status_cap, mask;
int count;

kms=dev_id;
kms->interrupt_count++;

/* we should only get tens per second, no more */
count=10000;

while(1){
/*	KM_DEBUG("beep %ld\n", kms->interrupt_count); */

		/* Check interrupt status */
	status=readl(kms->reg_aperture+RADEON_GEN_INT_STATUS);
	#if 0
	mask=readl(kms->reg_aperture+RADEON_GEN_INT_CNTL) & ((1<<30)|7);
	#endif
	KM_DEBUG_LEVEL(3)("GEN_INT_STATUS=0x%08x\n", status);
	status &=INT_BIT_GUIDMA|
		INT_BIT_VLINE|
		INT_BIT_VSYNC|
		INT_BIT_VBLANK|
		INT_BIT_CAP0;
	if(!status){
		/* INT_BIT_CAP0 will tell us if any interrupt bits are high */
		return IRQ_HANDLED;
		}
	atomic_inc(&(kms->recursion_count));
	/* bug ?? For some reason I think that changing recursion count in
	   between these instructions is not possible as interrupt has not
	   been acknowledged yet - and we know it was our interrupt
	   as status is non-zero */
	if(atomic_read(&(kms->recursion_count))>1){
		printk("km: irq handler double entry\n");
		atomic_dec(&(kms->recursion_count));
		return IRQ_NONE; /* another interrupt handler is active */
		}
	status_cap=0;
	if(status & INT_BIT_CAP0){
	             /* Check capture registers */
		status_cap=readl(kms->reg_aperture+RADEON_CAP_INT_STATUS);
		mask=readl(kms->reg_aperture+RADEON_CAP_INT_CNTL);
		KM_DEBUG_LEVEL(3)("CAP_INT_STATUS=0x%08x mask=0x%08x\n", status_cap, mask);
		status_cap&=(CAP_INT_BIT_BUF0|CAP_INT_BIT_BUF0_EVEN|
			     CAP_INT_BIT_BUF1|CAP_INT_BIT_BUF1_EVEN|
			     CAP_INT_BIT_VBI0|CAP_INT_BIT_VBI1) & mask; /* be nice to other users */
		}
	status &= ~INT_BIT_CAP0;
	
	     /* Acknowledge if necessary */
	if(status){
		writel(status, kms->reg_aperture+RADEON_GEN_INT_STATUS);
		}

	if(status_cap){
		writel(status_cap, kms->reg_aperture+RADEON_CAP_INT_STATUS);
		}
        wmb();
		/* Respond to events */
             /* check DMA and vblank bits in GEN_INT_STATUS */
	if(status & INT_BIT_GUIDMA){
		acknowledge_dma(kms);
		}

	if(status & (INT_BIT_VBLANK|INT_BIT_VLINE|INT_BIT_VSYNC)){
		if(status & INT_BIT_VBLANK)kms->vblank_count++;
		if(status & INT_BIT_VLINE)kms->vline_count++;
		if(status & INT_BIT_VSYNC)kms->vsync_count++;
		kmd_signal_state_change(kms->kmd);
		}

	if(status_cap){
		/*radeon_wait_for_idle(kms); */
		wmb();

		if(spin_trylock(&(kms->gui_dma_queue.lock))){
			if(status_cap & CAP_INT_BIT_BUF0)radeon_schedule_request(kms, &(kms->capture), kms->buf0_odd_offset, KM_FI_ODD);
			if(status_cap & CAP_INT_BIT_BUF0_EVEN)radeon_schedule_request(kms, &(kms->capture), kms->buf0_even_offset, 0);
			if(status_cap & CAP_INT_BIT_BUF1)radeon_schedule_request(kms, &(kms->capture), kms->buf1_odd_offset, KM_FI_ODD);
			if(status_cap & CAP_INT_BIT_BUF1_EVEN)radeon_schedule_request(kms, &(kms->capture), kms->buf1_even_offset, 0);
			if(status_cap & CAP_INT_BIT_VBI0)radeon_schedule_request(kms, &(kms->vbi),  kms->vbi0_offset, KM_FI_ODD);
			if(status_cap & CAP_INT_BIT_VBI1)radeon_schedule_request(kms, &(kms->vbi),  kms->vbi1_offset, 0);		
			spin_unlock(&(kms->gui_dma_queue.lock));
			} else {
			printk("GUI_DMA_QUEUE is locked");
			}
		}
	count--;
	if(count<0){
		KM_DEBUG(KERN_ERR "Kmultimedia: IRQ %d locked up, disabling interrupts in the hardware\n", irq);
		writel(0, kms->reg_aperture+RADEON_GEN_INT_CNTL);
		writel(0, kms->reg_aperture+RADEON_CAP_INT_CNTL);
		}
	atomic_dec(&(kms->recursion_count));
	}
 return  IRQ_HANDLED;
}

/* this is now done in ati.2 XFree86 driver */
void radeon_update_base_addr(KM_STRUCT *kms)
{
u32 aperture, aperture_size;
u32 new_mc_fb_location;
u32 mc_fb_location;
u32 default_offset;
u32 new_default_offset;
u32 cap0_config;
u32 trig_cntl;
u32 fcp_cntl;
u32 scale_cntl;
u32 display_base;

aperture=pci_resource_start(kms->dev,0);
radeon_wait_for_idle(kms);
aperture_size=readl(kms->reg_aperture+RADEON_CONFIG_APER_SIZE);
mc_fb_location=readl(kms->reg_aperture+RADEON_MC_FB_LOCATION);
default_offset=readl(kms->reg_aperture+RADEON_DEFAULT_OFFSET);
cap0_config=readl(kms->reg_aperture+RADEON_CAP0_CONFIG);
trig_cntl=readl(kms->reg_aperture+RADEON_TRIG_CNTL);
fcp_cntl=readl(kms->reg_aperture+RADEON_FCP_CNTL);
scale_cntl=readl(kms->reg_aperture+RADEON_SCALE_CNTL);
display_base=readl(kms->reg_aperture+RADEON_DISPLAY_BASE_ADDR);

new_mc_fb_location=(aperture>>16)|
	((aperture+aperture_size-1)&0xffff0000);
new_default_offset=(default_offset & (~0x3FFFFF))|(aperture>>10);
printk("new MC_FB_LOCATION=0x%08x versus 0x%08x\n", new_mc_fb_location, mc_fb_location);
printk("new DEFAULT_OFFSET=0x%08x versus 0x%08x\n", new_default_offset, default_offset);
if(new_mc_fb_location!=mc_fb_location){
	printk("WARNING !   upgrade your Xserver and DRM driver\n");
	printk("WARNING !   resetting MC_FB_LOCATION, DISPLAY_BASE_ADDR, OVERLAY_BASE_ADDR and DEFAULT_OFFSET\n");
	/* disable capture and overlay
	      - as they are the only ones that are still accessing memory */
	writel(0, kms->reg_aperture+RADEON_SCALE_CNTL);
	writel(0, kms->reg_aperture+RADEON_TRIG_CNTL);
	writel(0, kms->reg_aperture+RADEON_FCP_CNTL);
	/* set new base addresses */
	radeon_wait_for_idle(kms);
	writel(new_mc_fb_location, kms->reg_aperture+RADEON_MC_FB_LOCATION);
	writel(aperture, kms->reg_aperture+RADEON_DISPLAY_BASE_ADDR);
	writel(aperture, kms->reg_aperture+RADEON_OVERLAY_BASE_ADDR);
	writel(new_default_offset, kms->reg_aperture+RADEON_DEFAULT_OFFSET);
	radeon_wait_for_idle(kms);
	/* update capture buffers */
	writel(readl(kms->reg_aperture+RADEON_CAP0_BUF0_OFFSET)+aperture-display_base,kms->reg_aperture+ RADEON_CAP0_BUF0_OFFSET);
	writel(readl(kms->reg_aperture+RADEON_CAP0_BUF1_OFFSET)+aperture-display_base,kms->reg_aperture+ RADEON_CAP0_BUF1_OFFSET);
	writel(readl(kms->reg_aperture+RADEON_CAP0_BUF0_EVEN_OFFSET)+aperture-display_base,kms->reg_aperture+ RADEON_CAP0_BUF0_EVEN_OFFSET);
	writel(readl(kms->reg_aperture+RADEON_CAP0_BUF1_EVEN_OFFSET)+aperture-display_base,kms->reg_aperture+ RADEON_CAP0_BUF1_EVEN_OFFSET);
	/* restore capture state */
	radeon_wait_for_idle(kms);
	writel(fcp_cntl, kms->reg_aperture+RADEON_FCP_CNTL);
	writel(trig_cntl, kms->reg_aperture+RADEON_TRIG_CNTL);
	writel(scale_cntl, kms->reg_aperture+RADEON_SCALE_CNTL);
	}
}


/* setup statistics counting.. */
int radeon_init_hardware(KM_STRUCT *kms)
{
u32 a;

a=readl(kms->reg_aperture+RADEON_GEN_INT_CNTL);
#if 0
writel(a|(INT_BIT_VBLANK|INT_BIT_VLINE|INT_BIT_VSYNC), kms->reg_aperture+RADEON_GEN_INT_CNTL);
#endif
/* turn off any capture related interrupts 
   km should be the only code that uses it
   Note that there is a separate bit 30 in GEN_INT_CNTL
   that determines whether any of bits of CAP_INT_CNTL
   cause an interrupt */
writel(0, kms->reg_aperture+RADEON_CAP_INT_CNTL);
radeon_purge_gui_dma_queue(kms);
return 0;
}

int radeon_verify_page(KM_STRUCT *kms, long addr)
{
u32  mc_fb_location, mc_agp_location;
mc_fb_location=readl(kms->reg_aperture+RADEON_MC_FB_LOCATION);
mc_agp_location=readl(kms->reg_aperture+RADEON_MC_AGP_LOCATION);
addr=(addr>>16) & 0xffff;
if((addr>=(mc_fb_location & 0xffff))&& (addr<=((mc_fb_location >> 16)& 0xffff))){
	printk(KERN_ERR "km: page address 0x%08lx is within framebuffer aperture\n", addr);
	return 0;
	}
if((addr>=(mc_agp_location & 0xffff))&& (addr<=((mc_agp_location >> 16)& 0xffff))){
	printk(KERN_ERR "km: page address 0x%08lx is within AGP aperture\n", addr);
	return 0;
	}
return 1;
}

void radeon_uninit_hardware(KM_STRUCT *kms)
{
u32 a;

a=readl(kms->reg_aperture+RADEON_GEN_INT_CNTL);
writel(a& ~(INT_BIT_VBLANK|INT_BIT_VLINE|INT_BIT_VSYNC|INT_BIT_GUIDMA), kms->reg_aperture+RADEON_GEN_INT_CNTL);

writel(0, kms->reg_aperture+RADEON_CAP_INT_CNTL);
}
