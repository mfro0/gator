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

static int km_v4l_open(struct video_device *dev, int flags)
{
KM_STRUCT *kms=(KM_STRUCT *)dev->priv;
int result;

if((result=start_video_capture(kms))<0)return result;

kms->v4l_buf_parity=0;
kms->v4l_kdufpd=km_data_create_kdufpd(kms->capture.du);
if(kms->v4l_kdufpd==NULL){
	stop_video_capture(kms);
	return -EINVAL;
	}
return 0;
}


static void km_v4l_close(struct video_device *dev)
{
KM_STRUCT *kms=(KM_STRUCT *)dev->priv;

km_data_destroy_kdufpd(kms->v4l_kdufpd);
kms->v4l_kdufpd=NULL;
stop_video_capture(kms);
}

static long km_v4l_write(struct video_device *v, const char *buf, unsigned long count, int nonblock)
{
return -EINVAL;
}

static long km_v4l_read(struct video_device *v, char *buf, unsigned long count, int nonblock)
{
KM_STRUCT *kms=(KM_STRUCT *)v->priv;
int done;
KDU_FILE_PRIVATE_DATA *kdufpd=kms->v4l_kdufpd;

done=km_data_generic_stream_read(kdufpd, &(kms->capture.dvb), 
	buf, count, nonblock,
	kms->v4l_buf_parity, 1);
	
if((done > 0) && (kdufpd->bytes_read>=kms->capture.dvb.free[kdufpd->buffer])){
	kms->v4l_buf_parity=!kms->v4l_buf_parity;
	}
	
return done;
}

static int km_v4l_ioctl(struct video_device *dev, unsigned int cmd, void *arg)
{
KM_STRUCT *kms=(KM_STRUCT *)dev->priv;
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
static int km_v4l_mmap(struct video_device *dev, const char *adr, unsigned long size)
{
KM_STRUCT *kms=(KM_STRUCT *)dev->priv;
unsigned long start=(unsigned long) adr;
unsigned long page,pos;
KDU_FILE_PRIVATE_DATA *kdufpd=kms->v4l_kdufpd;
return -ENOMEM;
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

static unsigned int km_v4l_poll(struct video_device *dev, struct file *file,
	poll_table *wait)
{
KM_STRUCT *kms=(KM_STRUCT *)dev->priv;

return km_data_generic_stream_poll(kms->v4l_kdufpd, &(kms->capture.dvb), file, wait);
}

static int km_vbi_open(struct video_device *dev, int flags)
{
KM_STRUCT *kms=(KM_STRUCT *)dev->priv;
int result;

if((result=start_vbi_capture(kms))<0)return result;

kms->vbi_kdufpd=km_data_create_kdufpd(kms->vbi.du);
if(kms->vbi_kdufpd==NULL){
	stop_vbi_capture(kms);
	return -EINVAL;
	}
return 0;
}


static void km_vbi_close(struct video_device *dev)
{
KM_STRUCT *kms=(KM_STRUCT *)dev->priv;

km_data_destroy_kdufpd(kms->vbi_kdufpd);
kms->vbi_kdufpd=NULL;
stop_vbi_capture(kms);
}

static long km_vbi_write(struct video_device *v, const char *buf, unsigned long count, int nonblock)
{
return -EINVAL;
}

static long km_vbi_read(struct video_device *v, char *buf, unsigned long count, int nonblock)
{
KM_STRUCT *kms=(KM_STRUCT *)v->priv;
int done;
KDU_FILE_PRIVATE_DATA *kdufpd=kms->vbi_kdufpd;

done=km_data_generic_stream_read(kdufpd, &(kms->vbi.dvb), 
	buf, count, nonblock,
	0, 0);
	
return done;
}

static int km_vbi_ioctl(struct video_device *dev, unsigned int cmd, void *arg)
{
KM_STRUCT *kms=(KM_STRUCT *)dev->priv;
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

static unsigned int km_vbi_poll(struct video_device *dev, struct file *file,
	poll_table *wait)
{
KM_STRUCT *kms=(KM_STRUCT *)dev->priv;

return km_data_generic_stream_poll(kms->vbi_kdufpd, &(kms->vbi.dvb), file, wait);
}

#ifndef VID_HARDWARE_KM
#define VID_HARDWARE_KM 100
#endif

static struct video_device km_v4l_template=
{
	owner:		THIS_MODULE,
	name:		"Km",
	type:		VID_TYPE_CAPTURE|VID_TYPE_TELETEXT,
	hardware:	VID_HARDWARE_KM,
	open:		km_v4l_open,
	close:		km_v4l_close,
	read:		km_v4l_read,
	write:		km_v4l_write,
	ioctl:		km_v4l_ioctl,
	poll:		km_v4l_poll,
	mmap:		km_v4l_mmap,
	minor:		-1,
};

static struct video_device km_v4l_vbi_template=
{
	owner:		THIS_MODULE,
	name:		"Km",
	type:		VID_TYPE_CAPTURE|VID_TYPE_TELETEXT,
	hardware:	VID_HARDWARE_KM,
	open:		km_vbi_open,
	close:		km_vbi_close,
	read:		km_vbi_read,
	ioctl:		km_vbi_ioctl,
	poll:		km_vbi_poll,
	minor:		-1,
};

void init_km_v4l(KM_STRUCT *kms)
{
memcpy(&(kms->vd), &km_v4l_template, sizeof(km_v4l_template));
kms->vd.priv=kms;
memcpy(&(kms->vbi_vd), &km_v4l_vbi_template, sizeof(km_v4l_vbi_template));
kms->vbi_vd.priv=kms;

if(kms->is_capture_active!=NULL)
	video_register_device(&(kms->vd), VFL_TYPE_GRABBER, -1);
if(kms->is_vbi_active!=NULL)
	video_register_device(&(kms->vbi_vd), VFL_TYPE_VBI, -1);
}

void cleanup_km_v4l(KM_STRUCT *kms)
{
if(kms->is_capture_active!=NULL)
	video_unregister_device(&(kms->vd));
if(kms->is_vbi_active!=NULL)
	video_unregister_device(&(kms->vbi_vd));
}
