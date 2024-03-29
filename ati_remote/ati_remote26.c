/*
 *
 *  Copyright (c) 2002 Vladimir Dergachev
 *  Copyright (c) 2003 Brian S. Julin (2.6 port)
 *
 *  USB ATI Remote support
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * 
 * Should you need to contact me, the author, you can do so by
 * e-mail - mail your message to volodya@mindspring.com
 *
 * This driver was derived from usbati_remote and usbkbd drivers by 
 * Vojtech Pavlik
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>

/*
 * Version Information
 */
#define DRIVER_VERSION "2.1.1"
#define DRIVER_AUTHOR "Vladimir Dergachev <volodya@minspring.com>"
#define DRIVER_DESC "USB ATI Remote driver for 2.6 kernels"
#define DRIVER_LICENSE "GPL"

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE( DRIVER_LICENSE );

int channel_mask=0;

#ifdef MODULE
MODULE_PARM(channel_mask, "i");
MODULE_PARM_DESC(channel_mask, "bitmask that determines which remote control channels to ignore");
#else
#error bootprompt parameter code TODO and probably other stuff too
#endif

/* Get hi and low bytes of a 16-bits int */
#define HI(a)	((unsigned char)((a) >> 8))
#define LO(a)	((unsigned char)((a) & 0xff))

#define SEND_FLAG_IN_PROGRESS	1
#define SEND_FLAG_COMPLETE	2

struct ati_remote {
	unsigned char *data, *data_o;
	dma_addr_t data_dma, data_dma_o;
	char name[128];
	char phys[64];
	char input_name[16][160];
	unsigned char old[16][2];
	unsigned long old_jiffies[16];
	struct usb_device *usbdev;
	struct input_dev dev[16];
	struct urb *irq, *out;
	wait_queue_head_t wait;
	int open;
	int send_flags;
};

static char init1[]={
	0x01, 0x00, 0x20, 0x14 };
static char init2[]={
	0x01, 0x00, 0x20, 0x14, 0x20, 0x20, 0x20 };

#define KIND_END	0
#define KIND_LITERAL	1
#define KIND_FILTERED	2
#define KIND_LU		3
#define KIND_RU		4
#define KIND_LD		5
#define KIND_RD		6
#define KIND_ACCEL	7

static struct {
	short kind;
	unsigned char data1, data2;
	int type; 
	unsigned int code;
	int value;
	} ati_remote_translation_table[]={
	{KIND_LITERAL, 0x3d, 0x78, EV_KEY, BTN_LEFT, 1},	/* left ati_remote button */
	{KIND_LITERAL, 0x3e, 0x79, EV_KEY, BTN_LEFT, 0},
	{KIND_LITERAL, 0x41, 0x7c, EV_KEY, BTN_RIGHT, 1},	/* right ati_remote button */
	{KIND_LITERAL, 0x42, 0x7d, EV_KEY, BTN_RIGHT, 0},
		/* ati_remote */
	{KIND_ACCEL, 0x35, 0x70, EV_REL, REL_X, -1},   /* left */
	{KIND_ACCEL, 0x36, 0x71, EV_REL, REL_X, 1},   /* right */
	{KIND_ACCEL, 0x37, 0x72, EV_REL, REL_Y, -1},   /* up */
	{KIND_ACCEL, 0x38, 0x73, EV_REL, REL_Y, 1},   /* down */

	{KIND_LU, 0x39, 0x74, EV_REL, 0, 0},   /* left up */
	{KIND_RU, 0x3a, 0x75, EV_REL, 0, 0},   /* right up */
	{KIND_LD, 0x3c, 0x77, EV_REL, 0, 0},   /* left down */
	{KIND_RD, 0x3b, 0x76, EV_REL, 0, 0},   /* right down */

		/* keyboard.. */
	{KIND_FILTERED, 0xe2, 0x1d, EV_KEY, KEY_LEFT, 1},   /* key left */
	{KIND_FILTERED, 0xe4, 0x1f, EV_KEY, KEY_RIGHT, 1},   /* key right */
	{KIND_FILTERED, 0xe7, 0x22, EV_KEY, KEY_DOWN, 1},   /* key down */
	{KIND_FILTERED, 0xdf, 0x1a, EV_KEY, KEY_UP, 1},   /* key left */

	{KIND_FILTERED, 0xe3, 0x1e, EV_KEY, KEY_ENTER, 1},   /* key enter */

	{KIND_FILTERED, 0xd2, 0x0d, EV_KEY, KEY_1, 1},   
	{KIND_FILTERED, 0xd3, 0x0e, EV_KEY, KEY_2, 1},   
	{KIND_FILTERED, 0xd4, 0x0f, EV_KEY, KEY_3, 1},  
	{KIND_FILTERED, 0xd5, 0x10, EV_KEY, KEY_4, 1},  
	{KIND_FILTERED, 0xd6, 0x11, EV_KEY, KEY_5, 1},  
	{KIND_FILTERED, 0xd7, 0x12, EV_KEY, KEY_6, 1},   
	{KIND_FILTERED, 0xd8, 0x13, EV_KEY, KEY_7, 1},   
	{KIND_FILTERED, 0xd9, 0x14, EV_KEY, KEY_8, 1},   
	{KIND_FILTERED, 0xda, 0x15, EV_KEY, KEY_9, 1},   
	{KIND_FILTERED, 0xdc, 0x17, EV_KEY, KEY_0, 1},   

	{KIND_FILTERED, 0xdd, 0x18, EV_KEY, KEY_KPENTER, 1},   /* key "checkbox" */

	{KIND_FILTERED, 0xc5, 0x00, EV_KEY, KEY_A, 1},   
	{KIND_FILTERED, 0xc6, 0x01, EV_KEY, KEY_B, 1},   
	{KIND_FILTERED, 0xde, 0x19, EV_KEY, KEY_C, 1},   
	{KIND_FILTERED, 0xe0, 0x1b, EV_KEY, KEY_D, 1},   
	{KIND_FILTERED, 0xe6, 0x21, EV_KEY, KEY_E, 1},   
	{KIND_FILTERED, 0xe8, 0x23, EV_KEY, KEY_F, 1},   

	{KIND_FILTERED, 0xdb, 0x16, EV_KEY, KEY_MENU, 1},   /* key menu */
	{KIND_FILTERED, 0xc7, 0x02, EV_KEY, KEY_POWER, 1},   /* key power */
	{KIND_FILTERED, 0xc8, 0x03, EV_KEY, KEY_PROG1, 1},   /* key TV */
	{KIND_FILTERED, 0xc9, 0x04, EV_KEY, KEY_PROG2, 1},   /* key DVD */
	{KIND_FILTERED, 0xca, 0x05, EV_KEY, KEY_WWW, 1},   /* key Web */
	{KIND_FILTERED, 0xcb, 0x06, EV_KEY, KEY_BOOKMARKS, 1},   /* key "open book" */
	{KIND_FILTERED, 0xcc, 0x07, EV_KEY, KEY_EDIT, 1},   /* key "hand" */
	{KIND_FILTERED, 0xe1, 0x1c, EV_KEY, KEY_COFFEE, 1},   /* key "timer" */
	
	{KIND_FILTERED, 0xce, 0x09, EV_KEY, KEY_VOLUMEDOWN, 1},  
	{KIND_FILTERED, 0xcd, 0x08, EV_KEY, KEY_VOLUMEUP, 1},   
	{KIND_FILTERED, 0xcf, 0x0a, EV_KEY, KEY_MUTE, 1},   
	{KIND_FILTERED, 0xd1, 0x0c, EV_KEY, KEY_PAGEUP, 1},    /* next channel*/
	{KIND_FILTERED, 0xd0, 0x0b, EV_KEY, KEY_PAGEDOWN, 1},   /* prev channel */
	
	{KIND_FILTERED, 0xec, 0x27, EV_KEY, KEY_RECORD, 1},   
	{KIND_FILTERED, 0xea, 0x25, EV_KEY, KEY_PLAYCD, 1},   /* ke pay */
	{KIND_FILTERED, 0xe9, 0x24, EV_KEY, KEY_REWIND, 1},   
	{KIND_FILTERED, 0xeb, 0x26, EV_KEY, KEY_FORWARD, 1},   
	{KIND_FILTERED, 0xed, 0x28, EV_KEY, KEY_STOP, 1},   
	{KIND_FILTERED, 0xee, 0x29, EV_KEY, KEY_PAUSE, 1},   

	{KIND_FILTERED, 0xe5, 0x20, EV_KEY, KEY_FRONT, 1},   /* maximize */
	
	{KIND_END, 0x00, 0x00, EV_MAX+1, 0, 0} /* END */
	};

/*
 * Send a packet of bytes to the device
 */
static void send_packet(struct ati_remote *ati_remote, u16 cmd, unsigned char* data)
{
	DECLARE_WAITQUEUE(wait, current);
	int timeout = HZ; /* 1 second */

	memcpy(ati_remote->out->transfer_buffer + 1, data, LO(cmd));
	((char*)ati_remote->out->transfer_buffer)[0] = HI(cmd);
	ati_remote->out->transfer_buffer_length = LO(cmd) + 1;
	ati_remote->out->dev = ati_remote->usbdev;
	ati_remote->send_flags=SEND_FLAG_IN_PROGRESS;

	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&ati_remote->wait, &wait);

	if (usb_submit_urb(ati_remote->out, SLAB_ATOMIC)) {
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&ati_remote->wait, &wait);
		printk("done\n");
		return;
		}

	while (timeout && (ati_remote->out->status == -EINPROGRESS) && 
		!(ati_remote->send_flags & SEND_FLAG_COMPLETE)){
		timeout = schedule_timeout(timeout);
		rmb();
		}

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&ati_remote->wait, &wait);

	usb_unlink_urb(ati_remote->out);
}

static void ati_remote_irq(struct urb *urb, struct pt_regs *regs)
{
	struct ati_remote *ati_remote = urb->context;
	unsigned char *data = ati_remote->data;
	struct input_dev *dev = &(ati_remote->dev[0x0f]);
	int i;
	int accel;
	int status;
	int remote_num=0x0f;

	switch (urb->status) {
	case 0:			/* success */
		break;
	case -ECONNRESET:	/* unlink */
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	/* -EPIPE:  should clear the halt */
	default:		/* error */
		goto resubmit;
	}
	
	if(urb->actual_length==4){
		if((data[0]!=0x14)||((data[3] & 0x0f)!=0x00))
			printk("** weird key=%02x%02x%02x%02x\n", data[0], data[1], data[2], data[3]);
		remote_num=(data[3]>>4)&0x0f;
		dev=&(ati_remote->dev[remote_num]);
		if(channel_mask & (1<<(remote_num+1)))goto resubmit; /* ignore signals from this channel */
		} else
	if(urb->actual_length==1){
		if((data[0]!=(unsigned char)0xff)&&(data[0]!=0x00))
			printk("** weird byte=0x%02x\n", data[0]);
		} else {
		printk("length=%d  %02x %02x %02x %02x %02x %02x %02x %02x\n",
			urb->actual_length, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
		}

	accel=1;
	if((urb->actual_length==4) && (ati_remote->old[remote_num][0]==data[1])&&(ati_remote->old[remote_num][1]==data[2])){
		if(ati_remote->old_jiffies[remote_num]+4*HZ<jiffies)accel=8;
			else
		if(ati_remote->old_jiffies[remote_num]+3*HZ<jiffies)accel=6;
			else
		if(ati_remote->old_jiffies[remote_num]+2*HZ<jiffies)accel=4;
			else
		if(ati_remote->old_jiffies[remote_num]+HZ<jiffies)accel=3;
			else
		if(ati_remote->old_jiffies[remote_num]+(HZ>>1)<jiffies)accel=2;
		}
	if((urb->actual_length==4) && (data[0]==0x14) && ((data[3]&0x0f)==0x00)){
		for(i=0;ati_remote_translation_table[i].kind!=KIND_END;i++){
			if((((ati_remote_translation_table[i].data1 & 0x0f)==(data[1] & 0x0f))) && 
				((((ati_remote_translation_table[i].data1>>4)-(data[1]>>4)+remote_num)&0x0f)==0x0f) &&
				(ati_remote_translation_table[i].data2==data[2])){
				switch(ati_remote_translation_table[i].kind){
					case KIND_LITERAL:
						input_event(dev, ati_remote_translation_table[i].type,
							ati_remote_translation_table[i].code,
							ati_remote_translation_table[i].value);
						break;
					case KIND_ACCEL:
						input_event(dev, ati_remote_translation_table[i].type,
							ati_remote_translation_table[i].code,
							ati_remote_translation_table[i].value*accel);
						break;
					case KIND_LU:
						input_report_rel(dev, REL_X, -accel);
						input_report_rel(dev, REL_Y, -accel);
						break;
					case KIND_RU:
						input_report_rel(dev, REL_X, accel);
						input_report_rel(dev, REL_Y, -accel);
						break;
					case KIND_LD:
						input_report_rel(dev, REL_X, -accel);
						input_report_rel(dev, REL_Y, accel);
						break;
					case KIND_RD:
						input_report_rel(dev, REL_X, accel);
						input_report_rel(dev, REL_Y, accel);
						break;
					case KIND_FILTERED:
						if((ati_remote->old[remote_num][0]==data[1])&&(ati_remote->old[remote_num][1]==data[2])&&((ati_remote->old_jiffies[remote_num]+(HZ>>2))>jiffies)){
							goto resubmit;
							}
						input_event(dev, ati_remote_translation_table[i].type,
							ati_remote_translation_table[i].code,
							1);
						input_event(dev, ati_remote_translation_table[i].type,
							ati_remote_translation_table[i].code,
							0);
							ati_remote->old_jiffies[remote_num]=jiffies;
						break;
					default:
						printk("kind=%d\n", ati_remote_translation_table[i].kind);
					}
				break;
				}
			}
		if(ati_remote_translation_table[i].kind==KIND_END){
			printk("** unknown key=%02x%02x\n", data[1], data[2]);
			}
		if((ati_remote->old[remote_num][0]!=data[1])||(ati_remote->old[remote_num][1]!=data[2]))
			ati_remote->old_jiffies[remote_num]=jiffies;
		ati_remote->old[remote_num][0]=data[1];
		ati_remote->old[remote_num][1]=data[2];
		}

	input_sync(dev);
resubmit:
	status = usb_submit_urb (urb, SLAB_ATOMIC);
	if (status)
		err ("can't resubmit intr, %s-%s/input0, status %d",
				ati_remote->usbdev->bus->bus_name,
				ati_remote->usbdev->devpath, status);
}


static void ati_remote_usb_out(struct urb *urb, struct pt_regs *regs)
{
	struct ati_remote *ati_remote = urb->context;
	if (urb->status) return;
	ati_remote->send_flags|=SEND_FLAG_COMPLETE;
	wmb();
	if (waitqueue_active(&ati_remote->wait))
		wake_up(&ati_remote->wait);
}

static int ati_remote_open(struct input_dev *dev)
{
	struct ati_remote *ati_remote = dev->private;

/*	printk("ati_remote_open %d\n", ati_remote->open); */

	if (ati_remote->open++)
		return 0;

	ati_remote->irq->dev = ati_remote->usbdev;
	if (usb_submit_urb(ati_remote->irq, GFP_KERNEL)){
		printk(KERN_ERR "ati_remote: error submitting urb\n");
		ati_remote->open--;
		return -EIO;
	}

/*	printk("done: ati_remote_open now open=%d\n", ati_remote->open); */
	return 0;
}

static void ati_remote_close(struct input_dev *dev)
{
	struct ati_remote *ati_remote = dev->private;

	if (!--ati_remote->open) {
		usb_unlink_urb(ati_remote->irq);
		/* Technically should deal with pending wait queue here */
		usb_unlink_urb(ati_remote->out);
	}
}

static int ati_remote_probe(struct usb_interface *intf,
			    const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint, *epout;
	struct ati_remote *ati_remote;
	int pipe, maxp;
	char path[64]; /* Dunno why not use buf.  Dumbly copy usbmouse.c */
	char *buf;
	int i,j;
	int retval = -ENOMEM;

	interface = &intf->altsetting[intf->act_altsetting];

	if (interface->desc.bNumEndpoints != 2) return -ENODEV;

	/* use the first endpoint only for now */
	endpoint = &(interface->endpoint[0].desc);
	if (!(endpoint->bEndpointAddress & 0x80)) 
		return -ENODEV;
	if ((endpoint->bmAttributes & 3) != 3) 
		return -ENODEV;
	epout = &(interface->endpoint[1].desc);

	pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
	maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));
	printk("maxp=%d endpoint=0x%02x\n", maxp, endpoint->bEndpointAddress);

	/* Something still needed here? 
	   usb_set_idle(dev, interface->bInterfaceNumber, 0, 0);
	*/

	if (!(ati_remote = kmalloc(sizeof(struct ati_remote), GFP_KERNEL)))
		return -ENOMEM;
	memset(ati_remote, 0, sizeof(struct ati_remote)+32);

	ati_remote->data = 
		usb_buffer_alloc(dev, 8, SLAB_ATOMIC, &ati_remote->data_dma);
	if (!ati_remote->data) goto bail0;

	ati_remote->data_o = 
		usb_buffer_alloc(dev, 8, SLAB_ATOMIC, &ati_remote->data_dma_o);
	if (!ati_remote->data_o) goto bail1;

	retval = -ENODEV;
	ati_remote->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!ati_remote->irq) goto bail2;
	ati_remote->out = usb_alloc_urb(0, GFP_KERNEL);
	if (!ati_remote->out) goto bail3;

	ati_remote->usbdev = dev;

	retval = -ENOMEM;
	if (!(buf = kmalloc(63, GFP_KERNEL))) goto bail4;

	if (dev->descriptor.iManufacturer &&
		usb_string(dev, dev->descriptor.iManufacturer, buf, 63) > 0)
			strcat(ati_remote->name, buf);
	if (dev->descriptor.iProduct &&
		usb_string(dev, dev->descriptor.iProduct, buf, 63) > 0)
			sprintf(ati_remote->name, "%s %s", 
				ati_remote->name, buf);

	if (!strlen(ati_remote->name))
		sprintf(ati_remote->name, "USB ATI (X10) remote %04x:%04x",
			dev->descriptor.idVendor, dev->descriptor.idProduct);

	usb_make_path(dev, path, 64);
	sprintf(ati_remote->phys, "%s/input0", path);

	kfree(buf);

	for(i=0;i<16;i++){
		for(j=0;ati_remote_translation_table[j].kind!=KIND_END;j++)
			if(ati_remote_translation_table[j].type==EV_KEY)
				set_bit(ati_remote_translation_table[j].code, 
					ati_remote->dev[i].keybit);
		clear_bit(BTN_LEFT, ati_remote->dev[i].keybit);
		clear_bit(BTN_RIGHT, ati_remote->dev[i].keybit);

		ati_remote->dev[i].evbit[0] = BIT(EV_KEY) | BIT(EV_REL);
		ati_remote->dev[i].keybit[LONG(BTN_MOUSE)] = 
			BIT(BTN_LEFT) | BIT(BTN_RIGHT) | BIT(BTN_MIDDLE);
		ati_remote->dev[i].relbit[0] = BIT(REL_X) | BIT(REL_Y);
		ati_remote->dev[i].keybit[LONG(BTN_MOUSE)] |= 
			BIT(BTN_SIDE) | BIT(BTN_EXTRA);
		ati_remote->dev[i].relbit[0] |= BIT(REL_WHEEL);


		ati_remote->dev[i].private = ati_remote;
		ati_remote->dev[i].open = ati_remote_open;
		ati_remote->dev[i].close = ati_remote_close;

		sprintf(ati_remote->input_name[i], 
			"%s remote channel %d", ati_remote->name, i+1);
		ati_remote->dev[i].name = ati_remote->input_name[i];
		ati_remote->dev[i].phys = ati_remote->phys;
		ati_remote->dev[i].id.bustype = BUS_USB;
		ati_remote->dev[i].id.vendor = dev->descriptor.idVendor;
		ati_remote->dev[i].id.product = dev->descriptor.idProduct;
		ati_remote->dev[i].id.version = dev->descriptor.bcdDevice;

		ati_remote->old[i][0]=0;
		ati_remote->old[i][1]=0;
		ati_remote->old_jiffies[i]=jiffies;
		
	}
	init_waitqueue_head(&ati_remote->wait);

	printk("bInterval=%d\n", endpoint->bInterval);
	usb_fill_int_urb(ati_remote->irq, dev, pipe, ati_remote->data, 
			 (maxp > 8 ? 8 : maxp),
			 ati_remote_irq, ati_remote, endpoint->bInterval);

	usb_fill_int_urb(ati_remote->out, dev, 
			 usb_sndintpipe(dev, epout->bEndpointAddress),
			 ati_remote->data_o, 32, 
			 ati_remote_usb_out, ati_remote, epout->bInterval);

	printk(KERN_INFO "input: %s on %s\n", ati_remote->name, path);
	printk(KERN_INFO "See /proc/bus/input/devices for dev<->chan map\n");
	for(i=0;i<16;i++) input_register_device(&(ati_remote->dev[i]));

	send_packet(ati_remote, 0x8004, init1);
	send_packet(ati_remote, 0x8007, init2);

	usb_set_intfdata(intf, ati_remote);
	return 0;

 bail4:
	usb_free_urb(ati_remote->out);
 bail3:
	usb_free_urb(ati_remote->irq);
 bail2:
	usb_buffer_free(dev, 8, ati_remote->data_o, ati_remote->data_dma_o);
 bail1:
	usb_buffer_free(dev, 8, ati_remote->data, ati_remote->data_dma);
 bail0:
	kfree(ati_remote);
	return retval;

}

static void ati_remote_disconnect(struct usb_interface *intf)
{
	struct ati_remote *ati_remote = usb_get_intfdata (intf);

	usb_set_intfdata(intf, NULL);
	if (ati_remote) {
		int i;

		usb_unlink_urb(ati_remote->irq);
		usb_unlink_urb(ati_remote->out);
		for(i=0;i<16;i++)
			input_unregister_device(&(ati_remote->dev[i]));
		usb_free_urb(ati_remote->irq);
		usb_free_urb(ati_remote->out);
		usb_buffer_free(interface_to_usbdev(intf), 8, 
				ati_remote->data, ati_remote->data_dma);
		usb_buffer_free(interface_to_usbdev(intf), 8, 
				ati_remote->data_o, ati_remote->data_dma_o);

		kfree(ati_remote);
	}
}

static struct usb_device_id ati_remote_id_table [] = {
	{ USB_DEVICE(0x0bc7, 0x004) },
    { }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, ati_remote_id_table);

static struct usb_driver ati_remote_driver = {
	.owner		= THIS_MODULE,
	.name		"ati_remote",
	.probe		ati_remote_probe,
	.disconnect	ati_remote_disconnect,
	.id_table	ati_remote_id_table,
};

static int __init ati_remote_init(void)
{
	int retval = usb_register(&ati_remote_driver);
	if (retval == 0)
		info(DRIVER_VERSION ":" DRIVER_DESC);
	return retval;
}

static void __exit ati_remote_exit(void)
{
	usb_deregister(&ati_remote_driver);
}

module_init(ati_remote_init);
module_exit(ati_remote_exit);
