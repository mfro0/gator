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

#include "km.h"
#include "km_memory.h"

static int km_open(struct video_device *dev, int flags)
{
u32 buf_size;
int result;
KM_STRUCT *kms=(KM_STRUCT *)dev;

spin_lock(&(kms->kms_lock));
if(!kms->is_capture_active(kms)){
	printk("km: no data is available until AVview or xawtv is started\n");
	result=-ENODATA;
	spin_unlock(&(kms->kms_lock));
	goto fail;
	}

kms->v4l_buf_parity=0;
kms->total_frames=0;
kms->overrun=0;
kms->get_window_parameters(kms, &(kms->vwin));


buf_size=kms->vwin.width*kms->vwin.height*2;

if(kms->allocate_dvb!=NULL){
	if(kms->allocate_dvb(kms, buf_size)<0){
		result=-ENOMEM;
		goto fail;
		}
	kmd_signal_state_change(kms->kmd);
	} else {
	goto fail;
	}
kms->v4l_kdufpd=km_data_create_kdufpd(kms->capture_du);
spin_unlock(&(kms->kms_lock));
kms->start_transfer(kms);
return 0;

fail:
  spin_unlock(&(kms->kms_lock));
  return result;
}


static void km_close(struct video_device *dev)
{
KM_STRUCT *kms=(KM_STRUCT *)dev;
DECLARE_WAITQUEUE(wait, current);

spin_lock(&(kms->kms_lock));
km_data_destroy_kdufpd(kms->v4l_kdufpd);
kms->stop_transfer(kms);
if(kms->deallocate_dvb!=NULL){
	kms->deallocate_dvb(kms);
	kmd_signal_state_change(kms->kmd);
	} else {
	}
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
int done;
KDU_FILE_PRIVATE_DATA *kdufpd=kms->v4l_kdufpd;

done=km_data_generic_stream_read(kdufpd, kms->kmsbi, &(kms->dvb), 
	buf, count, nonblock,
	kms->v4l_buf_parity, 1);
	
if((done > 0) && (kdufpd->bytes_read>=kms->dvb.free[kdufpd->buffer])){
	kms->v4l_buf_parity=!kms->v4l_buf_parity;
	}
	
return done;
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
		if(kms->get_window_parameters==NULL)return -EINVAL;
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
		if(kms->get_window_parameters==NULL)return -EINVAL;
		kms->get_window_parameters(kms, &(vwin));
		spin_unlock(&(kms->kms_lock));

		if(copy_to_user(arg,&vwin,sizeof(vwin)))
			return -EFAULT;
		return 0;
		}
	case VIDIOCSWIN: {
		struct video_window vwin, vwin1;
		if(kms->get_window_parameters==NULL)return -EINVAL;
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

/* ignore this - it is bogus */
static int km_mmap(struct video_device *dev, const char *adr, unsigned long size)
{
KM_STRUCT *kms=(KM_STRUCT *)dev;
unsigned long start=(unsigned long) adr;
unsigned long page,pos;
KDU_FILE_PRIVATE_DATA *kdufpd=kms->v4l_kdufpd;

#if 0
if (size>(kms->frame_info[FRAME_ODD].buf_size+kms->frame_info[FRAME_EVEN].buf_size))
        return -EINVAL;
if (!kms->frame_info[FRAME_ODD].buffer || !kms->frame_info[FRAME_EVEN].buffer) {
/*	if(fbuffer_alloc(kms)) */
        return -EINVAL;
        }
frame=&(kms->frame_info[FRAME_ODD]);
pos=(unsigned long) kms->buffer[kdufpd->buffer];
while(size>0){
	page = kvirt_to_pa(pos);
        if(remap_page_range(start, page, PAGE_SIZE, PAGE_SHARED))
        	return -EAGAIN;
                start+=PAGE_SIZE;
                pos+=PAGE_SIZE;
                size-=PAGE_SIZE;
	if((pos-(unsigned long)kms->buffer[kdufpd->buffer])>frame->buf_size){
		frame=&(kms->frame_info[FRAME_EVEN]);
		pos=(unsigned long)kms->buffer[kdufpd->buffer];
		}
        }
#endif
return 0;
}

static unsigned int km_poll(struct video_device *dev, struct file *file,
	poll_table *wait)
{
KM_STRUCT *kms=(KM_STRUCT *)dev;

return km_data_generic_stream_poll(kms->v4l_kdufpd, kms->kmsbi, &(kms->dvb), file, wait);
}

#ifndef VID_HARDWARE_KM
#define VID_HARDWARE_KM 100
#endif

static struct video_device km_template=
{
	owner:		THIS_MODULE,
	name:		"Km",
	type:		VID_TYPE_CAPTURE|VID_TYPE_TELETEXT,
	hardware:	VID_HARDWARE_KM,
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
