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
kms->even_offset=a;
a=readl(kms->reg_aperture+RAGE128_CAP0_BUF0_OFFSET);
kms->odd_offset=a;
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

a=readl(kms->reg_aperture+RAGE128_CAP_INT_CNTL);
writel(a & ~3, kms->reg_aperture+RAGE128_CAP_INT_CNTL);
a=readl(kms->reg_aperture+RAGE128_GEN_INT_CNTL);
writel(a & ~((1<<16)|(1<<24)), kms->reg_aperture+RAGE128_GEN_INT_CNTL);
}

static int rage128_setup_single_frame_buffer(KM_STRUCT *kms, bm_list_descriptor *dma_table, long offset, long free)
{
int i;
long count;
count=free;
for(i=0;i<(kms->v4l_dvb.size/PAGE_SIZE);i++){
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

static void rage128_start_frame_transfer(KM_STRUCT *kms, int buffer, int field)
{
long offset, status;
u32 a;
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
rage128_setup_single_frame_buffer(kms, (kms->dma_table[buffer]), offset, kms->v4l_free[buffer]);
rage128_wait_for_idle(kms);
#if 0 
/* no analog for rage128.. yet ? */
/* wait for at least one available queue */
do {
	status=readl(kms->reg_aperture+RAGE128_DMA_GUI_STATUS);
	KM_DEBUG("status=0x%08x\n", status);
	} while (!(status & 0x1f));
#endif
/* start transfer */
kms->total_frames++;
kms->fi[buffer].flag|=KM_FI_DMA_ACTIVE;
kms->fi[buffer].age=kms->total_frames;
wmb();
writel(kvirt_to_pa(kms->dma_table[buffer])|RAGE128_SYSTEM_TRIGGER_VIDEO_TO_SYSTEM, 
	kms->reg_aperture+RAGE128_BM_VIDCAP_BUF0);
KM_DEBUG("start_frame_transfer_buf0\n");
}

#if 0
static void rage128_start_frame_transfer_buf0(KM_STRUCT *kms)
{
long offset, status;
u32 a;
if(kms->frame_info[FRAME_ODD].buffer==NULL)return;
kms->frame_info[FRAME_ODD].timestamp=jiffies;
offset=readl(kms->reg_aperture+RAGE128_CAP0_BUF0_OFFSET);
rage128_setup_single_frame_buffer(kms, &(kms->frame_info[FRAME_ODD]), offset);
rage128_wait_for_idle(kms);
#if 0 
/* no analog for rage128.. yet ? */
/* wait for at least one available queue */
do {
	status=readl(kms->reg_aperture+RAGE128_DMA_GUI_STATUS);
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
writel(kvirt_to_pa(kms->frame_info[FRAME_ODD].dma_table)|RAGE128_SYSTEM_TRIGGER_VIDEO_TO_SYSTEM, 
	kms->reg_aperture+RAGE128_BM_VIDCAP_BUF0);
KM_DEBUG("start_frame_transfer_buf0\n");
}

static void rage128_start_frame_transfer_buf0_even(KM_STRUCT *kms)
{
long offset, status;
u32 a;
if(kms->frame_info[FRAME_EVEN].buffer==NULL)return;
kms->frame_info[FRAME_EVEN].timestamp=jiffies;
offset=readl(kms->reg_aperture+RAGE128_CAP0_BUF0_EVEN_OFFSET);
rage128_setup_single_frame_buffer(kms, &(kms->frame_info[FRAME_EVEN]), offset);
rage128_wait_for_idle(kms);
#if 0 
/* no analog for rage128.. yet ? */
/* wait for at least one available queue */
do {
	status=readl(kms->reg_aperture+RAGE128_DMA_GUI_STATUS);
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
writel(kvirt_to_pa(kms->frame_info[FRAME_EVEN].dma_table)|RAGE128_SYSTEM_TRIGGER_VIDEO_TO_SYSTEM, 
	kms->reg_aperture+RAGE128_BM_VIDCAP_BUF0);
KM_DEBUG("start_frame_transfer_buf0_even\n");
}
#endif

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
if(status & 1)rage128_start_frame_transfer(kms, find_free_buffer(kms), 1);
if(status & 2)rage128_start_frame_transfer(kms, find_free_buffer(kms), 0); 
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

int rage128_allocate_v4l_dvb(KM_STRUCT *kms, long size)
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
		#if 0
		printk("entry virt %p phys %p %s\n", kms->frame_info[k].buffer+i*PAGE_SIZE, kms->frame_info[k].dma_table[i].to_addr,
		((unsigned long)kms->frame_info[k].dma_table[i].to_addr)<64*1024*1024?"*":"");
		#endif
		if(kvirt_to_pa(kms->buffer[k]+i*PAGE_SIZE)!=kvirt_to_bus(kms->buffer[k]+i*PAGE_SIZE)){
			printk(KERN_ERR "pa!=bus for entry %d frame %d\n", i, k);
			}
		}
	}
return 0;
}

int rage128_deallocate_v4l_dvb(KM_STRUCT *kms)
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

int rage128_allocate_single_frame_buffer(KM_STRUCT *kms, SINGLE_FRAME *frame, long size)
{
int i;
if(size>(4096*4096/sizeof(bm_list_descriptor))){
	printk("Too large buffer allocation requested: %d bytes\n", size);
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

