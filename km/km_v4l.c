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

static int km_open(struct video_device *dev, int flags)
{
u32 a;
KM_STRUCT *kms=(KM_STRUCT *)dev;
kms->frame.buf_ptr=0;
kms->frame_even.buf_ptr=0;
kms->buf_read_from=0;
kms->total_frames=0;
kms->overrun=0;
writel(3, kms->reg_aperture+CAP_INT_STATUS);
writel(1<<30, kms->reg_aperture+GEN_INT_STATUS);
a=readl(kms->reg_aperture+CAP_INT_CNTL);
writel(a|3, kms->reg_aperture+CAP_INT_CNTL);
a=readl(kms->reg_aperture+GEN_INT_CNTL);
writel(a|(1<<30), kms->reg_aperture+GEN_INT_CNTL);
return 0;
}


static void km_close(struct video_device *dev)
{
u32 a;
KM_STRUCT *kms=(KM_STRUCT *)dev;
/* stop interrupts */
a=readl(kms->reg_aperture+CAP_INT_CNTL);
writel(a & ~3, kms->reg_aperture+CAP_INT_CNTL);
a=readl(kms->reg_aperture+GEN_INT_CNTL);
writel(a & ~(1<<30), kms->reg_aperture+GEN_INT_CNTL);
kms->frame.buf_ptr=0;
kms->frame_even.buf_ptr=0;
kms->buf_read_from=-1; /* none */
printk("km: total frames: %ld, overrun: %ld\n", kms->total_frames, kms->overrun);
}

static long km_write(struct video_device *v, const char *buf, unsigned long count, int nonblock)
{
return -EINVAL;
}

static long km_read(struct video_device *v, char *buf, unsigned long count, int nonblock)
{
KM_STRUCT *kms=(KM_STRUCT *)v;
SINGLE_FRAME *frame;
int q,todo;
DECLARE_WAITQUEUE(wait, current);

todo=count;
if(kms->buf_read_from<0){
	printk("Internal error in km_v4l:km_read()\n");
	return -1;
	}
if(kms->buf_read_from==1)frame=&(kms->frame_even);
	else frame=&(kms->frame);
#if 0
printk("frame->buf_ptr=%d frame->buf_free=%d\n", frame->buf_ptr, frame->buf_free);
#endif
while(frame->buf_ptr==frame->buf_free){
	printk("frame->dma_active=%d\n", frame->dma_active);
	if(nonblock)return -EWOULDBLOCK;
	add_wait_queue(&(kms->frameq), &wait);
	current->state=TASK_INTERRUPTIBLE;
	schedule();
	if(signal_pending(current)){
		remove_wait_queue(&(kms->frameq), &wait);
		current->state=TASK_RUNNING;
		return -EINTR;
		}
	remove_wait_queue(&(kms->frameq), &wait);
	current->state=TASK_RUNNING;
	}
while(todo>0){	
	q=todo;
	if((frame->buf_ptr+q)>=frame->buf_free)q=frame->buf_free-frame->buf_ptr;   
	if(copy_to_user((void *) buf, (void *) (frame->buffer+frame->buf_ptr), q))
		return -EFAULT;
	todo-=q;
	frame->buf_ptr+=q;
	buf+=q;
	if(frame->buf_ptr>=frame->buf_free){
		kms->buf_read_from=kms->buf_read_from ? 0: 1;
		break;
		}
	}
return (count-todo);
}

static int km_ioctl(struct video_device *dev, unsigned int cmd, void *arg)
{
KM_STRUCT *kms=(KM_STRUCT *)dev;
switch(cmd){
	case VIDIOCGCAP:{
		struct video_capability b;
		strcpy(b.name,kms->vd.name);
		b.type = VID_TYPE_CAPTURE;
		b.channels = 1;
		b.audios = 0;
		b.maxwidth = 640;
		b.maxheight = 240;
		b.minwidth = 640;
		b.minheight = 240;
		if(copy_to_user(arg,&b,sizeof(b)))
			return -EFAULT;
		return 0;
		}
	case VIDIOCGPICT:{
		#if 0 /* ignore for now */
		struct video_picture p;
		if(copy_to_user(arg, &p, sizeof(p)))
			return -EFAULT;
		#endif
		return 0;
		}
	case VIDIOCSPICT:{
		struct video_picture p;
		if(copy_from_user(&p, arg,sizeof(p)))
			return -EFAULT;
		if(p.palette!=VIDEO_PALETTE_YUV422)return -EINVAL;
		return 0;
		}
	case VIDIOCGWIN:{
		struct video_window vwin;
		vwin.x=0;
		vwin.y=0;
		vwin.width=640;
		vwin.height=240;
		if(copy_to_user(arg,&vwin,sizeof(vwin)))
			return -EFAULT;
		return 0;
		}
	case VIDIOCSWIN:
		return 0;
	}
return -EINVAL;
}

static int km_mmap(struct video_device *dev, const char *adr, unsigned long size)
{
KM_STRUCT *kms=(KM_STRUCT *)dev;
SINGLE_FRAME *frame;
unsigned long start=(unsigned long) adr;
unsigned long page,pos;

if (size>(kms->frame.buf_size+kms->frame_even.buf_size))
        return -EINVAL;
if (!kms->frame.buffer || !kms->frame_even.buffer) {
/*	if(fbuffer_alloc(kms)) */
        return -EINVAL;
        }
frame=&(kms->frame);
pos=(unsigned long) frame->buffer;
while(size>0){
	page = kvirt_to_pa(pos);
        if(remap_page_range(start, page, PAGE_SIZE, PAGE_SHARED))
        	return -EAGAIN;
                start+=PAGE_SIZE;
                pos+=PAGE_SIZE;
                size-=PAGE_SIZE;
	if((pos-(unsigned long)frame->buffer)>frame->buf_size){
		frame=&(kms->frame_even);
		pos=frame->buffer;
		}
        }
return 0;
}

static unsigned int km_poll(struct video_device *dev, struct file *file,
	poll_table *wait)
{
KM_STRUCT *kms=(KM_STRUCT *)dev;
unsigned int mask=0;
SINGLE_FRAME *frame;

if(kms->buf_read_from<0){
	printk("km: internal error in kmv4l:km_poll\n");
	return 0;
	}
if(kms->buf_read_from==0)frame=&(kms->frame);
	else frame=&(kms->frame_even);

if(frame->buf_ptr==frame->buf_free)
	poll_wait(file, &(kms->frameq), wait);

#if 0
	if (btv->vbip < VBIBUF_SIZE)
#endif
/* by now we have more data.. */
mask |= (POLLIN | POLLRDNORM);

return mask;
}

static struct video_device km_template=
{
	owner:		THIS_MODULE,
	name:		"Km",
	type:		VID_TYPE_CAPTURE|VID_TYPE_TELETEXT,
	hardware:	VID_HARDWARE_BT848,
	open:		km_open,
	close:		km_close,
	read:		km_read,
	write:		km_write,
	ioctl:		km_ioctl,
	poll:		km_poll,
	mmap:		km_mmap,
	minor:		-1,
};

void init_km_v4l(KM_STRUCT *kms)
{
memcpy(&(kms->vd), &km_template, sizeof(km_template));
video_register_device(&(kms->vd), VFL_TYPE_GRABBER, -1);
}

void cleanup_km_v4l(KM_STRUCT *kms)
{
video_unregister_device(&(kms->vd));
}
