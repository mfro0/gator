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
#include "radeon_reg.h"


int radeon_is_capture_active(KM_STRUCT *kms)
{
return (readl(kms->reg_aperture+RADEON_CAP0_CONFIG) & 0x1);
}

void radeon_get_window_parameters(KM_STRUCT *kms, struct video_window *vwin)
{
u32 a;
vwin->x=0;
vwin->y=0;
a=readl(kms->reg_aperture+RADEON_CAP0_BUF_PITCH);
vwin->width=a/2;
a=readl(kms->reg_aperture+RADEON_CAP0_V_WINDOW);
vwin->height=(((a>>16)& 0xffff)-(a & 0xffff))+1;
printk("radeon_get_window_parameters: width=%d height=%d\n", vwin->width, vwin->height);
}

void radeon_start_transfer(KM_STRUCT *kms)
{
u32 a;

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

static void radeon_start_frame_transfer_buf0(KM_STRUCT *kms)
{
long offset, status;
if(kms->frame.buffer==NULL)return;
kms->frame.timestamp=jiffies;
offset=readl(kms->reg_aperture+RADEON_CAP0_BUF0_OFFSET);
setup_single_frame_buffer(kms, &(kms->frame), offset);
/* wait for at least one available queue */
do {
	status=readl(kms->reg_aperture+RADEON_DMA_GUI_STATUS);
	printk("status=0x%08x\n", status);
	} while (!(status & 0x1f));
/* start transfer */
if(kms->frame.dma_active)printk("DMA overrun\n");
if(kms->frame.buf_ptr!=kms->frame.buf_free){
	kms->overrun++;
	printk("Data overrun\n");
	}
kms->total_frames++;
kms->frame.dma_active=1;
writel(kvirt_to_pa(kms->frame.dma_table), kms->reg_aperture+RADEON_DMA_GUI_TABLE_ADDR);
printk("start_frame_transfer_buf0\n");
}

static void radeon_start_frame_transfer_buf0_even(KM_STRUCT *kms)
{
long offset, status;
if(kms->frame_even.buffer==NULL)return;
kms->frame_even.timestamp=jiffies;
offset=readl(kms->reg_aperture+RADEON_CAP0_BUF0_EVEN_OFFSET);
setup_single_frame_buffer(kms, &(kms->frame_even), offset);
/* wait for at least one available queue */
do {
	status=readl(kms->reg_aperture+RADEON_DMA_GUI_STATUS);
	printk("status=0x%08x\n", status);
	} while (!(status & 0x1f));
/* start transfer */
if(kms->frame_even.dma_active)printk("DMA overrun\n");
if(kms->frame_even.buf_ptr!=kms->frame_even.buf_free){
	kms->overrun++;
	printk("Data overrun\n");
	}
kms->total_frames++;
kms->frame_even.dma_active=1;
writel(kvirt_to_pa(kms->frame_even.dma_table), kms->reg_aperture+RADEON_DMA_GUI_TABLE_ADDR);
printk("start_frame_transfer_buf0_even\n");
}

int radeon_is_capture_irq_active(int irq, KM_STRUCT *kms)
{
long status, mask;
status=readl(kms->reg_aperture+RADEON_GEN_INT_STATUS);
if(!(status & (1<<8)))return 0;
status=readl(kms->reg_aperture+RADEON_CAP_INT_STATUS);
mask=readl(kms->reg_aperture+RADEON_CAP_INT_CNTL);
if(!(status & mask))return 0;
writel(status & mask, kms->reg_aperture+RADEON_CAP_INT_STATUS);
printk("CAP_INT_STATUS=0x%08x\n", status);
if(status & 1)radeon_start_frame_transfer_buf0(kms);
if(status & 2)radeon_start_frame_transfer_buf0_even(kms); 
return 1;
}


