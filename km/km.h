/*     km preliminary version

       (C) Vladimir Dergachev 2001-2003
       
       GNU Public License
       
*/

#ifndef __KM_H__
#define __KM_H__

#include <linux/videodev.h>
#include <linux/spinlock.h>
#include <linux/version.h>
#include "km_api.h"
#include "km_api_data.h"

#define KM_VERSION      "alpha-3.0"

/* already in >= 2.4.22 */
#ifndef LINUX_2_6 
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,22)
typedef void irqreturn_t;
#define IRQ_NONE
#define IRQ_HANDLED
#define IRQ_RETVAL(x)
#endif
#endif

typedef struct {
	u32 from_addr;
	u32 to_addr;
	u32 command;
	u32 reserved;
	} bm_list_descriptor;

#define KM_FI_ODD		1

#define MAX_FRAME_BUFF_NUM 	10

/* this structure contains information corresponding to a single stream
   There should be one for each functions: video capture, vbi capture, etc */
typedef struct {
	spinlock_t lock;
	int num_buffers;
	/* dma buffers */
	bm_list_descriptor **dma_table;
	u32 *dma_table_physical;
	
	int du;
	KM_DATA_VIRTUAL_BLOCK dvb;
	long next_buf;
	long total_frames;

	void *buffer[MAX_FRAME_BUFF_NUM];
	long free[MAX_FRAME_BUFF_NUM];

	/* meta information */
	int info_du;
	KM_DATA_VIRTUAL_BLOCK dvb_info;
	long info_free;
	} KM_STREAM;

/* DMA request struct */
typedef struct S_KM_TRANSFER_REQUEST {
	KM_STREAM *stream;     /* buffers, meta info and waitqueue */
	int buffer;			/* subunit of dvb that this transfer uses */
	unsigned int flag;              /* see below: */
	#define KM_TRANSFER_NOP			0
	#define KM_TRANSFER_TO_SYSTEM_RAM	1
	#define KM_TRANSFER_FROM_SYSTEM_RAM	2
	#define KM_TRANSFER_IN_PROGRESS		(1<<31)
	int (*start_transfer)(struct S_KM_TRANSFER_REQUEST *kmtr);
	void * user_data;		/* whatever the code that submitted the request
					has use for */	
	} KM_TRANSFER_REQUEST;

typedef struct {
	KM_TRANSFER_REQUEST *request;  /* points to the memory area holding requests 
					 A good idea is to allocate more memory for
					 KM_TRANSFER_QUEUE and point at the end of it */
					 
	int size;			/* maximum number of requests the queue can hold */
	int first;			/* request we are processing now */
	int last;                       /* empty request slot */
	spinlock_t lock;		/* acquire this when modifying any of the fields,
					  including data pointed to by requests */
	} KM_TRANSFER_QUEUE;

typedef struct S_KM_STRUCT {
	struct video_device vd;
	struct video_device vbi_vd;
	struct video_window vwin;
	spinlock_t kms_lock;
	long irq;
	struct pci_dev *dev;
	long interrupt_count;
	long vblank_count;
	long vline_count;
	long vsync_count;
	unsigned char * reg_aperture;
	
	int v4l_buf_parity;
	int vbi_buf_parity;
	
	atomic_t recursion_count;

	
	KM_FILE_PRIVATE_DATA *kmfpd;
	KDU_FILE_PRIVATE_DATA *v4l_kdufpd;
	KDU_FILE_PRIVATE_DATA *vbi_kdufpd;
	
	
	KM_STREAM capture;
	KM_STREAM vbi;
	
	KM_TRANSFER_QUEUE gui_dma_queue;
	int gdq_usage;
	KM_TRANSFER_REQUEST gui_dma_request[10];  /* we should not have more than 10 
	                                             outstanding DMA requests */
	
	/* hardware state
	   these are the values 
	   that were valid when DMA transfers
	   were started */
	u32 buf0_odd_offset;
	u32 buf0_even_offset;
	u32 buf1_odd_offset;
	u32 buf1_even_offset;

	u32 vbi0_offset;
	u32 vbi1_offset;
	int vbi_width, vbi_height, vbi_start;

	long kmd;
	KM_FIELD *kmfl;
	int (*init_hardware)(struct S_KM_STRUCT *kms);
	void (*uninit_hardware)(struct S_KM_STRUCT *kms);
	irqreturn_t (*irq_handler)(int irq, void *dev_id, struct pt_regs *regs);
	int  (*is_capture_active)(struct S_KM_STRUCT *kms);
	int (*is_vbi_active)(struct S_KM_STRUCT *kms);
	void (*get_window_parameters)(struct S_KM_STRUCT *kms, struct video_window *vwin);
	long (*get_vbi_buf_size)(struct S_KM_STRUCT *kms);
	void (*start_transfer)(struct S_KM_STRUCT *kms);
	void (*stop_transfer)(struct S_KM_STRUCT *kms);
	void (*start_vbi_transfer)(struct S_KM_STRUCT *kms);
	void (*stop_vbi_transfer)(struct S_KM_STRUCT *kms);
	int (*allocate_dvb)(KM_STREAM *stream, int num_buffers, long size);
	int (*deallocate_dvb)(KM_STREAM *stream);
	int (*verify_page)(struct S_KM_STRUCT *kms, long addr);
	} KM_STRUCT;

int acknowledge_dma(KM_STRUCT *kms);
int find_free_buffer(KM_STREAM *stream);
int start_video_capture(KM_STRUCT *kms);
void stop_video_capture(KM_STRUCT *kms);
int start_vbi_capture(KM_STRUCT *kms);
void stop_vbi_capture(KM_STRUCT *kms);
int km_add_transfer_request(KM_TRANSFER_QUEUE *kmtq, 
	KM_STREAM *stream, int buffer, int flag,
	int (*start_transfer)(KM_TRANSFER_REQUEST *kmtr), void *user_data);

#define HARDWARE_MACH64		0
#define HARDWARE_RAGE128	1
#define HARDWARE_RADEON		2

#ifndef __KM_C__
extern int km_debug;
#endif

#define KM_DEBUG   if(km_debug)printk
#define KM_DEBUG_LEVEL(a) 	if(km_debug>=(a))printk

#if 1
#define KM_CHECKPOINT printk("**CKPT %s %s %d\n", __FILE__, __FUNCTION__, __LINE__);
#else
#define KM_CHECKPOINT
#endif
#endif
