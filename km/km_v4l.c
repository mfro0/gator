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

#include "km.h"
#include "km_memory.h"

static int km_open(struct video_device *dev, int flags)
{
u32 buf_size;
int result;
KM_STRUCT *kms=(KM_STRUCT *)dev;

spin_lock(&(kms->kms_lock));
if(!kms->is_capture_active(kms)){
	printk("km: no data is available until xawtv is started\n");
	result=-ENODATA;
	spin_unlock(&(kms->kms_lock));
	goto fail;
	}

kms->frame.buf_ptr=0;
kms->frame_even.buf_ptr=0;
kms->buf_read_from=0;
kms->total_frames=0;
kms->overrun=0;
kms->get_window_parameters(kms, &(kms->vwin));


buf_size=kms->vwin.width*kms->vwin.height*2;

kms->deallocate_single_frame_buffer(kms, &(kms->frame));
kms->deallocate_single_frame_buffer(kms, &(kms->frame_even));

if(kms->allocate_single_frame_buffer(kms, &(kms->frame), buf_size)<0){
	result=-ENOMEM;
	goto fail;
	}

if(kms->allocate_single_frame_buffer(kms, &(kms->frame_even), buf_size)<0){
	result=-ENOMEM;
	goto fail;
	}

spin_unlock(&(kms->kms_lock));
kms->start_transfer(kms);
return 0;

fail:

  kms->deallocate_single_frame_buffer(kms, &(kms->frame));
  kms->deallocate_single_frame_buffer(kms, &(kms->frame_even));
  spin_unlock(&(kms->kms_lock));
  return result;

}


static void km_close(struct video_device *dev)
{
KM_STRUCT *kms=(KM_STRUCT *)dev;
spin_lock(&(kms->kms_lock));
kms->stop_transfer(kms);
kms->frame.buf_ptr=0;
kms->frame_even.buf_ptr=0;
kms->buf_read_from=-1; /* none */
kms->deallocate_single_frame_buffer(kms, &(kms->frame));
kms->deallocate_single_frame_buffer(kms, &(kms->frame_even));
printk("km: total frames: %ld, overrun: %ld\n", kms->total_frames, kms->overrun);
spin_unlock(&(kms->kms_lock));
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

spin_lock(&(kms->kms_lock));
todo=count;
if(kms->buf_read_from<0){
	printk("Internal error in km_v4l:km_read()\n");
	spin_unlock(&(kms->kms_lock));
	return -EIO;
	}
if(kms->buf_read_from==1)frame=&(kms->frame_even);
	else frame=&(kms->frame);
#if 0
printk("frame->buf_ptr=%d frame->buf_free=%d\n", frame->buf_ptr, frame->buf_free);
#endif
while(frame->buf_ptr==frame->buf_free){
	KM_DEBUG("frame->dma_active=%d\n", frame->dma_active);
	if(nonblock){
		spin_unlock(&(kms->kms_lock));
		return -EWOULDBLOCK;
		}
	add_wait_queue(&(kms->frameq), &wait);
	current->state=TASK_INTERRUPTIBLE;
	spin_unlock(&(kms->kms_lock));
	schedule();
	if(signal_pending(current)){
		spin_lock(&(kms->kms_lock));
		remove_wait_queue(&(kms->frameq), &wait);
		current->state=TASK_RUNNING;
		spin_unlock(&(kms->kms_lock));
		return -EINTR;
		}
	spin_lock(&(kms->kms_lock));
	remove_wait_queue(&(kms->frameq), &wait);
	current->state=TASK_RUNNING;
	}
while(todo>0){	
	q=todo;
	if((frame->buf_ptr+q)>=frame->buf_free)q=frame->buf_free-frame->buf_ptr;   
	if(copy_to_user((void *) buf, (void *) (frame->buffer+frame->buf_ptr), q)){
		spin_unlock(&(kms->kms_lock));
		return -EFAULT;
		}
	todo-=q;
	frame->buf_ptr+=q;
	buf+=q;
	if(frame->buf_ptr>=frame->buf_free){
		kms->buf_read_from=kms->buf_read_from ? 0: 1;
		break;
		}
	}
spin_unlock(&(kms->kms_lock));
return (count-todo);
}

static int km_ioctl(struct video_device *dev, unsigned int cmd, void *arg)
{
KM_STRUCT *kms=(KM_STRUCT *)dev;
spin_lock(&(kms->kms_lock));
switch(cmd){
	case VIDIOCGCAP:{
		struct video_capability b;
		struct video_window vwin;
		strcpy(b.name,kms->vd.name);
		b.type = VID_TYPE_CAPTURE;
		kms->get_window_parameters(kms, &(vwin));
		spin_unlock(&(kms->kms_lock));

		b.channels = 1;
		b.audios = 0;
		b.maxwidth = vwin.width;
		b.maxheight = vwin.height;
		b.minwidth = vwin.width;
		b.minheight = vwin.height;
		if(copy_to_user(arg,&b,sizeof(b)))
			return -EFAULT;
		return 0;
		}
	case VIDIOCGPICT:{
		struct video_picture p;
		p.palette=VIDEO_PALETTE_YUV422;
		p.brightness=32768;
		p.hue=32768;
		p.colour=32768;
		p.whiteness=32768;
		p.depth=16;
		spin_unlock(&(kms->kms_lock));

		if(copy_to_user(arg, &p, sizeof(p)))
			return -EFAULT;
		return 0;
		}
	case VIDIOCSPICT:{
		struct video_picture p;
		spin_unlock(&(kms->kms_lock));

		if(copy_from_user(&p, arg,sizeof(p)))
			return -EFAULT;
		if(p.palette!=VIDEO_PALETTE_YUV422)return -EINVAL;
		return 0;
		}
	case VIDIOCGWIN:{
		struct video_window vwin;
		kms->get_window_parameters(kms, &(vwin));
		spin_unlock(&(kms->kms_lock));

		if(copy_to_user(arg,&vwin,sizeof(vwin)))
			return -EFAULT;
		return 0;
		}
	case VIDIOCSWIN: {
		struct video_window vwin, vwin1;
		kms->get_window_parameters(kms, &(vwin1));
		spin_unlock(&(kms->kms_lock));

		if(copy_from_user(&vwin, arg, sizeof(vwin)))
			return -EFAULT;
		if((vwin.width!=vwin1.width)||(vwin.height!=vwin1.height)){
			printk("km: /dev/video%d uses frame format %dx%d\n",
				kms->vd.minor, vwin1.width, vwin1.height);
			return -EINVAL;
			}
		return 0;
		}
	}
spin_unlock(&(kms->kms_lock));
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
		pos=(unsigned long)frame->buffer;
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

spin_lock(&(kms->kms_lock));
if(kms->buf_read_from<0){
	printk("km: internal error in kmv4l:km_poll\n");
	spin_unlock(&(kms->kms_lock));
	return 0;
	}
if(kms->buf_read_from==0)frame=&(kms->frame);
	else frame=&(kms->frame_even);

if(frame->buf_ptr==frame->buf_free){
	spin_unlock(&(kms->kms_lock));
	poll_wait(file, &(kms->frameq), wait);
	spin_lock(&(kms->kms_lock));
	}

#if 0
	if (btv->vbip < VBIBUF_SIZE)
#endif
if(kms->buf_read_from==0)frame=&(kms->frame);
	else frame=&(kms->frame_even);

if(frame->buf_ptr<frame->buf_free)
	/* Now we have more data.. */
	mask |= (POLLIN | POLLRDNORM);

spin_unlock(&(kms->kms_lock));
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
