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
if(status & 1)start_frame_transfer_buf0(kms);
if(status & 2)start_frame_transfer_buf0_even(kms); 
return 1;
}
