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
KM_STRUCT *kms=(KM_STRUCT *)dev;
kms->frame.buf_ptr=0;
kms->frame_even.buf_ptr=0;
kms->buf_read_from=0;
writel(3, kms->reg_aperture+CAP_INT_CNTL);
return 0;
}


static void km_close(struct video_device *dev)
{
KM_STRUCT *kms=(KM_STRUCT *)dev;
/* stop interrupts */
writel(0, kms->reg_aperture+CAP_INT_CNTL);
kms->frame.buf_ptr=0;
kms->frame_even.buf_ptr=0;
kms->buf_read_from=-1; /* none */
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

todo=count;
if(kms->buf_read_from<0){
	printk("Internal error in km_v4l:km_read()\n");
	return -1;
	}
if(kms->buf_read_from==1)frame=&(kms->frame_even);
	else frame=&(kms->frame);
printk("frame->buf_ptr=%d frame->buf_free=%d\n", frame->buf_ptr, frame->buf_free);
while(todo>0){	
	q=todo;
	if((frame->buf_ptr+q)>=frame->buf_free)q=frame->buf_free-frame->buf_ptr;   
	if(copy_to_user((void *) buf, (void *) (frame->buffer+frame->buf_ptr), q))
		return -EFAULT;
	todo-=q;
	frame->buf_ptr+=q;
	buf+=q;
	if(frame->buf_ptr>=frame->buf_free){
		frame->buf_ptr=0;
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
	case VIDIOCSPICT:
		return 0;
	case VIDIOCGWIN:
		return 0;
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
	unsigned int mask = 0;

#if 0
	poll_wait(file, &btv->vbiq, wait);

	if (btv->vbip < VBIBUF_SIZE)
#endif
/* for now we always have data.. */
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
