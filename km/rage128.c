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
#include "rage128_reg.h"

void rage128_wait_for_fifo(KM_STRUCT *kms, int entries)
{
long count;
u32 a;
count=10000;
while(((a=readl(kms->reg_aperture+RAGE128_GUI_STAT))&0xFFF)<entries){
	udelay(1);
	count--;
	if(count<0){
		printk(KERN_ERR "km: rage128 FIFO locked up\n");
		return;
		}
	}
}

void rage128_wait_for_idle(KM_STRUCT *kms)
{
u32 a;
long count;
rage128_wait_for_fifo(kms,64);
count=1000;
while(((a=readl(kms->reg_aperture+RAGE128_GUI_STAT)) & RAGE128_ENGINE_ACTIVE)!=0){
	udelay(1);
	count--;
	if(count<0){
		printk(KERN_ERR "km: rage128 engine lock up\n");
		return;
		}
	}
/* according to docs we should do FlushPixelCache here. However we don't really
read back framebuffer data, so no need */
}

int rage128_is_capture_active(KM_STRUCT *kms)
{
return (readl(kms->reg_aperture+RAGE128_CAP0_CONFIG) & 0x1);
}

void rage128_get_window_parameters(KM_STRUCT *kms, struct video_window *vwin)
{
u32 a;
vwin->x=0;
vwin->y=0;
rage128_wait_for_idle(kms);
a=readl(kms->reg_aperture+RAGE128_CAP0_BUF_PITCH);
vwin->width=a/2;
a=readl(kms->reg_aperture+RAGE128_CAP0_V_WINDOW);
vwin->height=(((a>>16)& 0xffff)-(a & 0xffff))+1;
a=readl(kms->reg_aperture+RAGE128_CAP0_BUF0_EVEN_OFFSET);
kms->buf0_even_offset=a;
a=readl(kms->reg_aperture+RAGE128_CAP0_BUF0_OFFSET);
kms->buf0_odd_offset=a;
printk("rage128_get_window_parameters: width=%d height=%d\n", vwin->width, vwin->height);
}

void rage128_start_transfer(KM_STRUCT *kms)
{
u32 a;
/* general settings */
a=readl(kms->reg_aperture+RAGE128_BUS_CNTL);
writel(a & ~(1<<6), kms->reg_aperture+RAGE128_BUS_CNTL);
writel(0xFF|RAGE128_BM_GLOBAL_FORCE_TO_PCI, kms->reg_aperture+RAGE128_BM_CHUNK_0_VAL);
writel(0xF0F0F0F, kms->reg_aperture+RAGE128_BM_CHUNK_1_VAL);

writel(3, kms->reg_aperture+RAGE128_CAP_INT_STATUS);
writel((1<<8)|(1<<16), kms->reg_aperture+RAGE128_GEN_INT_STATUS);
a=readl(kms->reg_aperture+RAGE128_CAP_INT_CNTL);
writel(a|3, kms->reg_aperture+RAGE128_CAP_INT_CNTL);
a=readl(kms->reg_aperture+RAGE128_GEN_INT_CNTL);
writel(a|(1<<16), kms->reg_aperture+RAGE128_GEN_INT_CNTL);
}

void rage128_stop_transfer(KM_STRUCT *kms)
{
u32 a;

/* stop interrupts */
a=readl(kms->reg_aperture+RAGE128_CAP_INT_CNTL);
writel(a & ~3, kms->reg_aperture+RAGE128_CAP_INT_CNTL);
a=readl(kms->reg_aperture+RAGE128_GEN_INT_CNTL);
writel(a & ~((1<<16)|(1<<24)), kms->reg_aperture+RAGE128_GEN_INT_CNTL);
wmb();
/* stop outstanding DMA transfers */
a=readl(kms->reg_aperture+RAGE128_BUS_CNTL);
writel(a|RAGE128_BUS_CNTL__BM_RESET, kms->reg_aperture+RAGE128_BUS_CNTL);
}

static int rage128_setup_dma_table(KM_STRUCT *kms, bm_list_descriptor *dma_table, long offset, long free)
{
int i;
long count;
count=free;
for(i=0;i<(kms->dvb.size/PAGE_SIZE);i++){
	dma_table[i].from_addr=offset+i*PAGE_SIZE;
	if(count>PAGE_SIZE){
		dma_table[i].command=PAGE_SIZE | RAGE128_BM_FORCE_TO_PCI;
		count-=PAGE_SIZE;
		} else {
		dma_table[i].command=count | RAGE128_BM_FORCE_TO_PCI | RAGE128_BM_END_OF_LIST;
		return 0;
		}
	}
return 0;
}

static void rage128_start_request_transfer(KM_TRANSFER_REQUEST *kmtr)
{
long status;
KM_STRUCT *kms=kmtr->user_data;
rage128_wait_for_idle(kms);
wmb();
writel(kvirt_to_pa(kms->dma_table[kmtr->buffer])|RAGE128_SYSTEM_TRIGGER_VIDEO_TO_SYSTEM, 
	kms->reg_aperture+RAGE128_BM_VIDCAP_BUF0);
}

static void rage128_schedule_request(KM_STRUCT *kms, int buffer, int field)
{
long offset;
if(buffer<0){
	KM_DEBUG("mach64_schedule_request buffer=%d field=%d\n",buffer, field);
	return;
	}
if(field){
	offset=kms->buf0_even_offset;
	kms->kmsbi[buffer].user_flag&=~KM_FI_ODD;
	} else {
	offset=kms->buf0_odd_offset;
	kms->kmsbi[buffer].user_flag|=KM_FI_ODD;
	}
KM_DEBUG("buf=%d field=%d\n", buffer, field);
kms->fi[buffer].timestamp_start=jiffies;
rage128_setup_dma_table(kms, (kms->dma_table[buffer]), offset, kms->v4l_free[buffer]);
/* start transfer */
kms->total_frames++;
kms->kmsbi[buffer].age=kms->total_frames;
wmb();
km_add_transfer_request(&(kms->gui_dma_queue),
	kms->kmsbi, &(kms->dvb), buffer, KM_TRANSFER_TO_SYSTEM_RAM, rage128_start_request_transfer, kms);
}

static int rage128_is_capture_irq_active(KM_STRUCT *kms)
{
long status, mask;
status=readl(kms->reg_aperture+RAGE128_GEN_INT_STATUS);
if(!(status & (1<<8)))return 0;
status=readl(kms->reg_aperture+RAGE128_CAP_INT_STATUS);
mask=readl(kms->reg_aperture+RAGE128_CAP_INT_CNTL);
if(!(status & mask))return 0;
rage128_wait_for_idle(kms);
writel(status & mask, kms->reg_aperture+RAGE128_CAP_INT_STATUS);
/* do not start dma transfer if capture is not active anymore */
if(!rage128_is_capture_active(kms))return 1;
if(status & 1)rage128_schedule_request(kms, find_free_buffer(kms), 0);
if(status & 2)rage128_schedule_request(kms, find_free_buffer(kms), 1); 
return 1;
}

void rage128_km_irq(int irq, void *dev_id, struct pt_regs *regs)
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
	if(!rage128_is_capture_irq_active(kms)){
		status=readl(kms->reg_aperture+RAGE128_GEN_INT_STATUS);
		mask=readl(kms->reg_aperture+RAGE128_GEN_INT_CNTL);
		if(!(status & mask))return;
		rage128_wait_for_idle(kms);
		if(status & (1<<16))acknowledge_dma(kms);
		writel(status & mask, kms->reg_aperture+RAGE128_GEN_INT_STATUS);
		count--;
		if(count<0){
			KM_DEBUG(KERN_ERR "Kmultimedia: IRQ %d locked up, disabling interrupts in the hardware\n", irq);
			writel(0, kms->reg_aperture+RAGE128_GEN_INT_STATUS);
			}
		}
	}
}
