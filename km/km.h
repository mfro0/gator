/*     km preliminary version

       (C) Vladimir Dergachev 2001-2002
       
       GNU Public License
       
*/

#ifndef __KM_H__
#define __KM_H__

#include <linux/videodev.h>
#include <linux/spinlock.h>
#include "km_api.h"
#include "km_api_data.h"

#define KM_VERSION      "alpha-2.0"

typedef struct {
	u32 from_addr;
	u32 to_addr;
	u32 command;
	u32 reserved;
	} bm_list_descriptor;

typedef struct {
	char *buffer;
	long timestamp;
	long buf_size;
	long buf_ptr;
	long buf_free;
	int dma_active;
	bm_list_descriptor *dma_table;
	} SINGLE_FRAME;

typedef struct S_KM_STRUCT{
	struct video_device vd;
	struct video_window vwin;
	spinlock_t kms_lock;
	wait_queue_head_t frameq;
	long irq;
	struct pci_dev *dev;
	long interrupt_count;
	long vblank_count;
	long vline_count;
	long vsync_count;
	long total_frames;
	long overrun;
	unsigned char * reg_aperture;
	int buf_read_from;

#define FRAME_ODD 		0
#define FRAME_EVEN 		1
#define MAX_FRAME_BUFF_NUM 	10

	SINGLE_FRAME frame_info[MAX_FRAME_BUFF_NUM];
	KM_DATA_VIRTUAL_BLOCK v4l_dvb;
	void *v4l_ptr[MAX_FRAME_BUFF_NUM];
	long *v4l_free[MAX_FRAME_BUFF_NUM];
	int v4l_du;
	int num_buffers;

	int capture_active;
	long kmd;
	KM_FIELD *kmfl;
	int (*init_hardware)(struct S_KM_STRUCT *kms);
	void (*uninit_hardware)(struct S_KM_STRUCT *kms);
	void (*irq_handler)(int irq, void *dev_id, struct pt_regs *regs);
	int  (*is_capture_active)(struct S_KM_STRUCT *kms);
	void (*get_window_parameters)(struct S_KM_STRUCT *kms, struct video_window *vwin);
	void (*start_transfer)(struct S_KM_STRUCT *kms);
	void (*stop_transfer)(struct S_KM_STRUCT *kms);
	int (*allocate_v4l_dvb)(struct S_KM_STRUCT *kms);
	int (*allocate_single_frame_buffer)(struct S_KM_STRUCT *kms, SINGLE_FRAME *frame, long size);
	void (*deallocate_single_frame_buffer)(struct S_KM_STRUCT *kms, SINGLE_FRAME *frame);
	} KM_STRUCT;

int acknowledge_dma(KM_STRUCT *kms);

#define HARDWARE_MACH64		0
#define HARDWARE_RAGE128	1
#define HARDWARE_RADEON		2

#ifndef __KM_C__
extern int km_debug;
#endif

#define KM_DEBUG   if(km_debug)printk

#endif
