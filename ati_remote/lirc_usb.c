/* lirc_usb - USB remote support for LIRC
 * (currently only supports ATI Remote Wonder USB)
 * Version 0.1  [pre-alpha status]
 *
 * Copyright (C) 2003 Paul Miller <pmiller9@users.sourceforge.net>
 *
 * This driver was derived from:
 *   Vladimir Dergachev <volodya@minspring.com>'s 2002
 *      "USB ATI Remote support" (input device)
 *   Adrian Dewhurst <sailor-lk@sailorfrag.net>'s 2002
 *      "USB StreamZap remote driver" (LIRC)
 *   Artur Lipowski <alipowski@kki.net.pl>'s 2002
 *      "lirc_dev" and "lirc_gpio" LIRC modules
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
 */



#include <linux/config.h>
#if defined(CONFIG_MODVERSIONS) && !defined(__GENKSYMS__) && !defined(__DEPEND__)
#define MODVERSIONS
#include <linux/modversions.h>
#endif
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/poll.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/sched.h>
#include <asm/semaphore.h>
#include <asm/uaccess.h>

#include "drivers/lirc.h"

#define DRIVER_VERSION		"0.1"
#define DRIVER_AUTHOR		"Paul Miller <pmiller9@users.sourceforge.net>"
#define DRIVER_DESC			"USB remote driver for LIRC"
#define DRIVER_NAME			"lirc_usb"

#define MAX_DEVICES			16
#define DRIVER_MAJOR		USB_MAJOR
#define DRIVER_MINOR_BASE	200

#define BUFLEN				16
#define OUTLEN				32

#ifdef CONFIG_USB_DEBUG
	static int debug = 1;
#else
	static int debug = 0;
#endif
#define dprintk				if (debug) printk


/* get hi and low bytes of a 16-bits int */
#define HI(a)				((unsigned char)((a) >> 8))
#define LO(a)				((unsigned char)((a) & 0xff))

/* lock irctl structure */
#define IRLOCK				down_interruptible(&ir->lock)
#define IRUNLOCK			up(&ir->lock)

/* general constants */
#define SUCCESS					0
#define SEND_FLAG_IN_PROGRESS	1
#define SEND_FLAG_COMPLETE		2



struct irctl {
	/* usb */
	struct usb_device *usbdev;
	struct urb irq, out;

	/* devfs */
	devfs_handle_t devfs;

	/* lirc */
	unsigned long features;
	unsigned int buf_len;
	int bytes_in_key;

	/* queue */
	unsigned char buffer[BUFLEN];
	unsigned char buffer_out[OUTLEN];
	unsigned int in_buf;
	int head, tail;

	wait_queue_head_t wait;
    int minor;
	int open;
	int send_flags;

	struct semaphore lock;
};

DECLARE_MUTEX(plugin_lock);

extern devfs_handle_t usb_devfs_handle;
static struct irctl *irctls[MAX_DEVICES];

static char init1[] = {0x01, 0x00, 0x20, 0x14};
static char init2[] = {0x01, 0x00, 0x20, 0x14, 0x20, 0x20, 0x20};






static void send_packet(struct irctl *ir, u16 cmd, unsigned char* data)
{
	DECLARE_WAITQUEUE(wait, current);
	int timeout = HZ; /* 1 second */
	unsigned char buf[OUTLEN];

	dprintk(DRIVER_NAME "[%d]: send called (%#x)\n", ir->minor, cmd);

	IRLOCK;
	ir->out.transfer_buffer_length = LO(cmd) + 1;
	ir->out.dev = ir->usbdev;
	ir->send_flags = SEND_FLAG_IN_PROGRESS;

	memcpy(buf+1, data, LO(cmd));
	buf[0] = HI(cmd);
	memcpy(ir->buffer_out, buf, LO(cmd)+1);

	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&ir->wait, &wait);

	if (usb_submit_urb(&ir->out)) {
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&ir->wait, &wait);
		dprintk(DRIVER_NAME "[%d]: send complete\n", ir->minor);
		IRUNLOCK;
		return;
	}
	IRUNLOCK;

	while (timeout && (ir->out.status == -EINPROGRESS) && !(ir->send_flags & SEND_FLAG_COMPLETE)) {
		timeout = schedule_timeout(timeout);
		rmb();
	}

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&ir->wait, &wait);

	usb_unlink_urb(&ir->out);
}









static inline void init_irctl(struct irctl *ir)
{
	IRLOCK;

	ir->usbdev = NULL;

	memset(&ir->buffer, 0, BUFLEN);
	memset(&ir->buffer_out, 0, OUTLEN);
	ir->in_buf = 0;
	ir->head = ir->tail = 0;
	ir->open = 0;
	ir->send_flags = 0;

	ir->features = 0;
	ir->buf_len = 0;
	ir->bytes_in_key = 0;

	IRUNLOCK;
}







static int irctl_open(struct inode *inode, struct file *file)
{
	struct irctl *ir;
	int minor = MINOR(inode->i_rdev) - DRIVER_MINOR_BASE;

	dprintk(DRIVER_NAME "[%d]: open called\n", minor);

	if (minor < 0 || minor >= MAX_DEVICES) {
		dprintk(DRIVER_NAME "[%d]: open result = -ENODEV\n", minor);
		return -ENODEV;
	}

	down_interruptible(&plugin_lock);
	ir = irctls[minor];
	up(&plugin_lock);

	if (!ir) {
		dprintk(DRIVER_NAME "[%d]: open result = -ENODEV\n", minor);
		return -ENODEV;
	}

	IRLOCK;

	if (ir->open) {
		dprintk(DRIVER_NAME "[%d]: open result = -EBUSY\n", minor);
		IRUNLOCK;
		return -EBUSY;
	}

	ir->irq.dev = ir->usbdev;
	if (usb_submit_urb(&ir->irq)){
		printk(DRIVER_NAME "[%d]: open result = -E10 error submitting urb\n", minor);
		IRUNLOCK;
		return -EIO;
	}

	ir->head = ir->tail;
	ir->in_buf = 0;
	++ir->open;

	file->private_data = ir;

	IRUNLOCK;

	dprintk(DRIVER_NAME "[%d]: open result = SUCCESS\n", minor);
	return SUCCESS;
}





static int irctl_close(struct inode *inode, struct file *file)
{
	struct irctl *ir = file->private_data;

	dprintk(DRIVER_NAME "[%d]: close called\n", ir->minor);

	if (!ir) return -ENODEV;

	IRLOCK;

	if (ir->open <= 0) {
		printk(DRIVER_NAME "[%d]: close result = -ENODEV\n", ir->minor);
		file->private_data = NULL;
		IRUNLOCK;
		return -ENODEV;
	}

	if (!ir->usbdev) {
		if (--ir->open) {
			IRUNLOCK;
			usb_unlink_urb(&ir->irq);
		} else {
			IRUNLOCK;
		}
		dprintk(DRIVER_NAME "[%d]: close result = SUCCESS\n", ir->minor);
		return SUCCESS;
	}

	if (!--ir->open) usb_unlink_urb(&ir->irq);

	IRUNLOCK;

	dprintk(DRIVER_NAME "[%d]: close result = SUCCESS\n", ir->minor);
	return SUCCESS;
}





static unsigned int irctl_poll(struct file *file, poll_table *wait)
{
	struct irctl *ir = file->private_data;
	unsigned int retval = 0;

	dprintk(DRIVER_NAME "[%d]: poll called\n", ir->minor);

	if (!ir->in_buf) {
		poll_wait(file, &ir->wait, wait);
		retval = (!ir->usbdev) ? POLLHUP : SUCCESS;
	} else {
		retval = POLLIN | POLLRDNORM;
	}

	return retval;
}




static ssize_t irctl_read(struct file *file, char *buffer, size_t length, loff_t *ppos)
{
	struct irctl *ir = file->private_data;
	unsigned char buf[BUFLEN];
	int ret;
	DECLARE_WAITQUEUE(wait, current);

	dprintk(DRIVER_NAME "[%d]: read called\n", ir->minor);

	if (ir->bytes_in_key != length) {
		dprintk(DRIVER_NAME "[%d]: read result = -EIO\n", ir->minor);
		return -EIO;
	}

	/* we add ourselves to the task queue before buffer check 
         * to avoid losing scan code (in case when queue is awaken somewhere 
	 * beetwen while condition checking and scheduling)
	 */
	add_wait_queue(&ir->wait, &wait);
	current->state = TASK_INTERRUPTIBLE;

	/* while input buffer is empty and device opened in blocking mode, 
	 * wait for input 
	 */
	while (!ir->in_buf) {
		if (file->f_flags & O_NONBLOCK) {
			dprintk(DRIVER_NAME "[%d]: read result = -EWOULDBLOCK\n", ir->minor);
			remove_wait_queue(&ir->wait, &wait);
			current->state = TASK_RUNNING;
			return -EWOULDBLOCK;
		}
		if (signal_pending(current)) {
			dprintk(DRIVER_NAME "[%d]: read result = -ERESTARTSYS\n", ir->minor);
			remove_wait_queue(&ir->wait, &wait);
			current->state = TASK_RUNNING;
			return -ERESTARTSYS;
		}
		schedule();
		current->state = TASK_INTERRUPTIBLE;
	}

	remove_wait_queue(&ir->wait, &wait);
	current->state = TASK_RUNNING;

	/* here is the only point at which we remove key codes from 
	 * the buffer
	 */
	IRLOCK;
	memcpy(buf, &ir->buffer[ir->head], length);
	ir->head += length;
	ir->head %= ir->buf_len;
	ir->in_buf -= length;
	IRUNLOCK;

	ret = copy_to_user(buffer, buf, length);

	dprintk(DRIVER_NAME "[%d]: read result = %s (%d)\n",
		ir->minor, ret ? "-EFAULT" : "OK", ret);

	return ret ? -EFAULT : length;
}








static int irctl_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int minor = MINOR(inode->i_rdev) - DRIVER_MINOR_BASE;
	unsigned long mode;
	int result = SUCCESS;
	struct irctl *ir = irctls[minor];

	dprintk(DRIVER_NAME "[%d]: ioctl called (%u)\n", ir->minor, cmd);

	if (!ir->usbdev) {
		dprintk(DRIVER_NAME "[%d]: ioctl result = -ENODEV\n", ir->minor);
		return -ENODEV;
	}

	switch(cmd)
	{
	case LIRC_GET_FEATURES:
		result = put_user(ir->features, (unsigned long *)arg);
		break;
	case LIRC_GET_REC_MODE:
		result = put_user(LIRC_REC2MODE(ir->features), (unsigned long*)arg);
		break;
	case LIRC_SET_REC_MODE:
		result = get_user(mode, (unsigned long*)arg);
		if(!result && !(LIRC_MODE2REC(mode) & ir->features)) {
			result = -EINVAL;
		}
		break;
	case LIRC_GET_LENGTH:
		result = put_user((unsigned long)ir->bytes_in_key*8, (unsigned long*)arg);
		break;
	default:
		result = -ENOIOCTLCMD;
	}

	dprintk(DRIVER_NAME "[%d]: ioctl result = %d\n", ir->minor, result);

	return result;
}






static struct file_operations fops = {
	owner:		THIS_MODULE,

	read:		irctl_read,
	open:		irctl_open,
	release:	irctl_close,
	ioctl:		irctl_ioctl,
	poll:		irctl_poll
};










unsigned inline char get_key(void *data, unsigned char *key, int key_no)
{
	return LO(key[key_no]);
}



static void usb_remote_irq(struct urb *urb)
{
	struct irctl *ir = urb->context;
	unsigned char buf[BUFLEN];
	int i;

	if (!ir) {
		printk(DRIVER_NAME "[?]: usb irq called with no context\n");
		usb_unlink_urb(urb);
		return;
	}

	dprintk(DRIVER_NAME "[%d]: usb irq called\n", ir->minor);

	if (urb->status) return;
	if (urb->actual_length != ir->bytes_in_key) return;

	if (ir->in_buf == ir->buf_len) {
		dprintk(DRIVER_NAME "[%d]: buffer overflow\n", ir->minor);
		return;
	}

	dprintk(DRIVER_NAME "[%d]: remote data length = %d\n", ir->minor, urb->actual_length);
	for (i = 0; i < ir->bytes_in_key; i++) {
		buf[i] = get_key(ir, urb->transfer_buffer, i);
		dprintk(DRIVER_NAME "[%d]: remote code (%#02x) now in buffer\n",
			ir->minor, buf[i]);
	}

	/* here is the only point at which we add key codes to the buffer */
	IRLOCK;
	memcpy(&ir->buffer[ir->tail], buf, ir->bytes_in_key);
	ir->tail += ir->bytes_in_key;
	ir->tail %= ir->buf_len;
	ir->in_buf += ir->bytes_in_key;
	IRUNLOCK;

	wake_up_sync(&ir->wait);
	return;
}








static void usb_remote_out(struct urb *urb)
{
	struct irctl *ir = urb->context;

	if (!ir) {
		printk(DRIVER_NAME "[?]: usb out called with no context\n");
		usb_unlink_urb(urb);
		return;
	}

	dprintk(DRIVER_NAME "[%d]: usb out called\n", ir->minor);

	if (urb->status) return;

	ir->send_flags |= SEND_FLAG_COMPLETE;
	wmb();
	if (waitqueue_active(&ir->wait)) wake_up(&ir->wait);
}








static void *usb_remote_probe(struct usb_device *dev, unsigned int ifnum, const struct usb_device_id *id)
{
	struct usb_interface *iface;
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint, *epout;
	struct irctl *ir;
	int pipe, minor, maxp, len;
	char devname[10], buf[63], name[128]="";

	dprintk(DRIVER_NAME ": usb probe called\n");

	iface = &dev->actconfig->interface[ifnum];
	interface = &iface->altsetting[iface->act_altsetting];

	if (interface->bNumEndpoints != 2) return NULL;
	endpoint = interface->endpoint + 0;
    if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) != USB_DIR_IN) return NULL;
    if ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_INT) return NULL;
	epout = interface->endpoint + 1;

	pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
	maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

	usb_set_idle(dev, interface->bInterfaceNumber, 0, 0);

	down_interruptible(&plugin_lock);
	for (minor = 0; minor < MAX_DEVICES; minor++) {
		if (!irctls[minor]) break;
	}
	if (minor >= MAX_DEVICES) {
		printk(DRIVER_NAME "[%d]: too many devices connected!\n", minor);
		up(&plugin_lock);
		return NULL;
	}

	if (!(ir = kmalloc(sizeof(struct irctl), GFP_KERNEL))) {
		printk(DRIVER_NAME "[%d]: out of memory\n", minor);
		up(&plugin_lock);
		return NULL;
	}
	memset(ir, 0, sizeof(struct irctl));

	init_MUTEX(&ir->lock);
	init_waitqueue_head(&ir->wait);
	init_irctl(ir);

	ir->minor = minor;
	ir->usbdev = dev;

	ir->features = LIRC_CAN_REC_LIRCCODE;
	ir->bytes_in_key = 4;

	len = (maxp > BUFLEN) ? BUFLEN : maxp;
	ir->buf_len = len - (len % ir->bytes_in_key);

	FILL_INT_URB(&ir->irq, dev, pipe, ir->buffer, ir->buf_len, usb_remote_irq, ir, endpoint->bInterval);
	FILL_INT_URB(&ir->out, dev, usb_sndintpipe(dev, epout->bEndpointAddress), ir->buffer_out, OUTLEN, usb_remote_out, ir, epout->bInterval);

	snprintf(devname, 10, "remote%d", minor);
	ir->devfs = devfs_register(usb_devfs_handle, devname,
			DEVFS_FL_DEFAULT, DRIVER_MAJOR, DRIVER_MINOR_BASE+minor,
			S_IFCHR | S_IRUSR | S_IRGRP | S_IROTH, &fops, NULL);

	if (!ir->devfs) {
		printk(DRIVER_NAME "[%d]: unable to register devfs device\n", minor);
		kfree(ir);
		up(&plugin_lock);
		return NULL;
	}

	irctls[minor] = ir;
	up(&plugin_lock);

	if (dev->descriptor.iManufacturer && usb_string(dev, dev->descriptor.iManufacturer, buf, 63) > 0) strncpy(name, buf, 128);
	if (dev->descriptor.iProduct && usb_string(dev, dev->descriptor.iProduct, buf, 63) > 0) snprintf(name, 128, "%s %s", name, buf);
	printk(DRIVER_NAME "[%d]: %s on usb%d:%d.%d\n", minor, name, dev->bus->busnum, dev->devnum, ifnum);
	dprintk(DRIVER_NAME "[%d]: maxp = %d, buf_len = %d\n", minor, ir->buf_len, maxp);

	send_packet(ir, 0x8004, init1);
	send_packet(ir, 0x8007, init2);

	return ir;
}







static void usb_remote_disconnect(struct usb_device *dev, void *ptr)
{
	struct irctl *ir = ptr;
	int minor = ir->minor;

	ir->usbdev = NULL;

	wake_up_all(&ir->wait);

	down_interruptible(&plugin_lock);	
	irctls[ir->minor] = NULL;
	up(&plugin_lock);

	IRLOCK;
	devfs_unregister(ir->devfs);
	ir->devfs = NULL;
	usb_unlink_urb(&ir->irq);
	IRUNLOCK;

	kfree(ir);

	printk(DRIVER_NAME "[%d]: usb remote disconnected\n", minor);
}










static struct usb_device_id usb_remote_id_table [] = {
    { USB_DEVICE(0x0bc7, 0x0004) },		/* ATI Remote Wonder USB */
    { }									/* Terminating entry */
};

static struct usb_driver usb_remote_driver = {
	name:		DRIVER_NAME,
	probe:		usb_remote_probe,
	disconnect:	usb_remote_disconnect,
	fops:		&fops,
	minor:		DRIVER_MINOR_BASE,
	id_table:	usb_remote_id_table
};

static int __init usb_remote_init(void)
{
	int i;

	printk("\n" DRIVER_NAME ": " DRIVER_DESC " v" DRIVER_VERSION "\n");
	printk(DRIVER_NAME ": " DRIVER_AUTHOR "\n");
	dprintk(DRIVER_NAME ": debug mode enabled\n");

	for (i = 0; i < MAX_DEVICES; i++) {
		irctls[i] = NULL;
	}

	i = usb_register(&usb_remote_driver);
	if (i < 0) {
		printk(DRIVER_NAME ": usb register failed, result = %d\n", i);
		return -1;
	}
	return SUCCESS;
}

static void __exit usb_remote_exit(void)
{
	usb_deregister(&usb_remote_driver);
}

module_init(usb_remote_init);
module_exit(usb_remote_exit);

MODULE_AUTHOR (DRIVER_AUTHOR);
MODULE_DESCRIPTION (DRIVER_DESC);
MODULE_LICENSE ("GPL");
MODULE_DEVICE_TABLE (usb, usb_remote_id_table);

MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "enable driver debug mode");

EXPORT_NO_SYMBOLS;

