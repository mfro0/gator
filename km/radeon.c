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

int radeon_check_mc_settings(KM_STRUCT *kms)
{
u32 aperture, aperture_size;
u32 mc_fb_location;
u32 mc_agp_location;
u32 new_mc_fb_location;
u32 new_mc_agp_location;

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
a=readl(kms->reg_aperture+RADEON_CAP0_BUF0_EVEN_OFFSET);
kms->even_offset=a;
a=readl(kms->reg_aperture+RADEON_CAP0_BUF0_OFFSET);
kms->odd_offset=a;
printk("radeon_get_window_parameters: width=%d height=%d\n", vwin->width, vwin->height);
}

void radeon_start_transfer(KM_STRUCT *kms)
{
u32 a;

kms->capture_active=radeon_check_mc_settings(kms);
if(!kms->capture_active)return;
wmb();
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
wmb();
kms->capture_active=0;
}

static int radeon_setup_single_frame_buffer(KM_STRUCT *kms, bm_list_descriptor *dma_table, long offset, long free)
{
int i;
long count;
u32 mem_aperture;
count=free;
mem_aperture=pci_resource_start(kms->dev, 0);
for(i=0;i<(kms->v4l_dvb.size/PAGE_SIZE);i++){
	dma_table[i].from_addr=offset+i*PAGE_SIZE;
	if(count>PAGE_SIZE){
		dma_table[i].command=PAGE_SIZE;
		count-=PAGE_SIZE;
		} else {
		dma_table[i].command=count | RADEON_DMA_GUI_COMMAND__EOL ;
		}
	}
/* verify_dma_table(kms, frame); */
return 0;
}

static void radeon_start_frame_transfer(KM_STRUCT *kms, int buffer, int field)
{
long status;
long offset;
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
radeon_setup_single_frame_buffer(kms, (kms->dma_table[buffer]), offset, kms->v4l_free[buffer]);
wmb();
/* wait for at least one available queue */
do {
	status=readl(kms->reg_aperture+RADEON_DMA_GUI_STATUS);
	KM_DEBUG("status=0x%08lx\n", status);
	} while (!(status & 0x1f));
/* start transfer */
kms->total_frames++;
kms->fi[buffer].flag|=KM_FI_DMA_ACTIVE;
kms->fi[buffer].age=kms->total_frames;
wmb();
writel(kvirt_to_pa(kms->dma_table[buffer]), (u32)(kms->reg_aperture+RADEON_DMA_GUI_TABLE_ADDR)| (0));
KM_DEBUG("start_frame_transfer_buf0\n");
}

static int radeon_is_capture_irq_active(KM_STRUCT *kms)
{
long status, mask;
status=readl(kms->reg_aperture+RADEON_GEN_INT_STATUS);
if(!(status & (1<<8)))return 0;
status=readl(kms->reg_aperture+RADEON_CAP_INT_STATUS);
mask=readl(kms->reg_aperture+RADEON_CAP_INT_CNTL);
if(!(status & mask))return 0;
/*radeon_wait_for_idle(kms); */
wmb();
writel(status & mask, kms->reg_aperture+RADEON_CAP_INT_STATUS);
KM_DEBUG("CAP_INT_STATUS=0x%08x\n", status);
/* do not start dma transfer if capture is not active anymore */
if(!kms->capture_active)return 1;
if(status & 1)radeon_start_frame_transfer(kms, find_free_buffer(kms), 1);
if(status & 2)radeon_start_frame_transfer(kms, find_free_buffer(kms), 0);
return 1;
}

void radeon_km_irq(int irq, void *dev_id, struct pt_regs *regs)
{
KM_STRUCT *kms;
long status, mask;
int count;

/* spin_lock(&(kms->kms_lock)); */
kms=dev_id;
kms->interrupt_count++;

/* we should only get tens per second, no more */
count=10000;

while(1){
/*	KM_DEBUG("beep %ld\n", kms->interrupt_count); */
	if(!radeon_is_capture_irq_active(kms)){
		status=readl(kms->reg_aperture+RADEON_GEN_INT_STATUS);
		mask=readl(kms->reg_aperture+RADEON_GEN_INT_CNTL);
		if(!(status & mask)){
/*			spin_unlock(&(kms->kms_lock)); */
			return;
			}
		if(status & (1<<30)){
			wmb();
			acknowledge_dma(kms);
			}
		if(status & (1<<0))kms->vblank_count++;
		if(status & (1<<1))kms->vline_count++;
		if(status & (1<<2))kms->vsync_count++;
		if(status & 7)kmd_signal_state_change(kms->kmd);
		writel(status & mask, kms->reg_aperture+RADEON_GEN_INT_STATUS);
		count--;
		if(count<0){
			KM_DEBUG(KERN_ERR "Kmultimedia: IRQ %d locked up, disabling interrupts in the hardware\n", irq);
			writel(0, kms->reg_aperture+RADEON_GEN_INT_STATUS);
			}
		}
	}
}

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

int radeon_allocate_v4l_dvb(KM_STRUCT *kms, long size)
{
int i,k;
KM_CHECKPOINT
if(size>(4096*4096/sizeof(bm_list_descriptor))){
	printk("Too large buffer allocation requested: %ld bytes\n", size);
	return -1;
	}
/* allocate data unit to hold video data */
kms->v4l_dvb.size=((size+PAGE_SIZE-1)/PAGE_SIZE)*PAGE_SIZE;
kms->v4l_dvb.n=kms->num_buffers;
kms->v4l_dvb.free=kms->v4l_free;
kms->v4l_dvb.ptr=kms->buffer;
kms->v4l_du=km_allocate_data_virtual_block(&(kms->v4l_dvb), S_IFREG | S_IRUGO);
if(kms->v4l_du<0)return -1;
KM_CHECKPOINT
/* allocate data unit to hold field info */
kms->v4l_dvb_info.size=kms->num_buffers*sizeof(FIELD_INFO);
kms->v4l_dvb_info.n=1;
kms->v4l_dvb_info.free=&(kms->info_free);
kms->info_free=kms->num_buffers*sizeof(FIELD_INFO);
kms->v4l_dvb_info.ptr=&(kms->fi);
kms->v4l_info_du=km_allocate_data_virtual_block(&(kms->v4l_dvb_info), S_IFREG | S_IRUGO);
if(kms->v4l_info_du<0){
	km_deallocate_data(kms->v4l_du);
	kms->v4l_du=-1;
	return -1;
	}
kms->dma_table=kmalloc(sizeof(*(kms->dma_table))*kms->num_buffers, GFP_KERNEL);
memset(kms->fi, 0 , sizeof(*(kms->fi))*kms->num_buffers);
for(k=0;k<kms->num_buffers;k++){
	kms->v4l_dvb.free[k]=size;
	kms->fi[k].flag=0;
	kms->fi[k].next=k+1;
	kms->fi[k].prev=k-1;
	kms->dma_table[k]=rvmalloc(4096);
	kms->v4l_free[k]=size;
	memset(kms->dma_table[k], 0, 4096);
	/* create DMA table */
	for(i=0;i<(kms->v4l_dvb.size/PAGE_SIZE);i++){
		kms->dma_table[k][i].to_addr=kvirt_to_pa(kms->buffer[k]+i*PAGE_SIZE);
		if(kvirt_to_pa(kms->buffer[k]+i*PAGE_SIZE)!=kvirt_to_bus(kms->buffer[k]+i*PAGE_SIZE)){
			printk(KERN_ERR "pa!=bus for entry %d frame %d\n", i, k);
			}
		}
	}
kms->fi[0].prev=kms->num_buffers-1;
kms->fi[kms->num_buffers-1].next=0;
kms->next_cap_buf=0;
KM_CHECKPOINT
return 0;
}

int radeon_deallocate_v4l_dvb(KM_STRUCT *kms)
{
int k;
for(k=0;k<kms->num_buffers;k++){
	rvfree(kms->dma_table[k], 4096);
	}
kfree(kms->dma_table);
kms->dma_table=NULL;
km_deallocate_data(kms->v4l_info_du);
kms->v4l_info_du=-1;
km_deallocate_data(kms->v4l_du);
kms->v4l_du=-1;
return 0;
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
/* frame->dma_table=__get_dma_pages(GFP_KERNEL | GFP_DMA, 1); */
frame->dma_table=rvmalloc(4096);
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
	#if 0
	printk("entry virt %p phys %p %s\n", frame->buffer+i*PAGE_SIZE, frame->dma_table[i].to_addr,
		((unsigned long)frame->dma_table[i].to_addr)<64*1024*1024?"*":"");
	#endif
	if(kvirt_to_pa(frame->buffer+i*PAGE_SIZE)!=kvirt_to_bus(frame->buffer+i*PAGE_SIZE)){
		printk(KERN_ERR "pa!=bus for entry %ld frame %p\n", i, frame);
		}
	}
/* Reset MC_FB_LOCATION */
/* radeon_update_base_addr(kms); */
return 0;
}


/* setup statistics counting.. */
int radeon_init_hardware(KM_STRUCT *kms)
{
u32 a;

a=readl(kms->reg_aperture+RADEON_GEN_INT_CNTL);
writel(a|(7), kms->reg_aperture+RADEON_GEN_INT_CNTL);
return 0;
}

/* setup statistics counting.. */
int radeon_uninit_hardware(KM_STRUCT *kms)
{
u32 a;

a=readl(kms->reg_aperture+RADEON_GEN_INT_CNTL);
writel(a& ~((7)|(1<<30)), kms->reg_aperture+RADEON_GEN_INT_CNTL);
return 0;
}
