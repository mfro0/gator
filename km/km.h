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

#define KM_VERSION      "alpha-3.0"

typedef struct {
	u32 from_addr;
	u32 to_addr;
	u32 command;
	u32 reserved;
	} bm_list_descriptor;

#define KM_FI_ODD		1
#define KM_FI_DMA_ACTIVE	2
#define KM_FI_PINNED		4

typedef struct {
	long timestamp_start;
	long timestamp_end;
	} FIELD_INFO;

typedef struct S_KM_STRUCT {
	struct video_device vd;
	struct video_window vwin;
	spinlock_t kms_lock;
	long irq;
	struct pci_dev *dev;
	long interrupt_count;
	long vblank_count;
	long vline_count;
	long vsync_count;
	long total_frames;
	long overrun;
	unsigned char * reg_aperture;
	
	long next_cap_buf;
	int v4l_buf_parity;

#define FRAME_ODD 		0
#define FRAME_EVEN 		1
#define MAX_FRAME_BUFF_NUM 	10

	KM_DATA_VIRTUAL_BLOCK dvb_info;
	int info_du;
	FIELD_INFO *fi;
	KM_STREAM_BUFFER_INFO *kmsbi;
	long info_free;
	
	KM_DATA_VIRTUAL_BLOCK dvb;
	int capture_du;
	void *buffer[MAX_FRAME_BUFF_NUM];
	long v4l_free[MAX_FRAME_BUFF_NUM];
	KM_FILE_PRIVATE_DATA *v4l_kdufpd;
	
	bm_list_descriptor **dma_table;
	int num_buffers;
	
	unsigned long buf0_odd_offset;
	unsigned long buf0_even_offset;
	unsigned long buf1_odd_offset;
	unsigned long buf1_even_offset;

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
	int (*allocate_dvb)(struct S_KM_STRUCT *kms, long size);
	int (*deallocate_dvb)(struct S_KM_STRUCT *kms);
	} KM_STRUCT;

int acknowledge_dma(KM_STRUCT *kms);
int find_free_buffer(KM_STRUCT *kms);
int start_video_capture(KM_STRUCT *kms);
void stop_video_capture(KM_STRUCT *kms);

#define HARDWARE_MACH64		0
#define HARDWARE_RAGE128	1
#define HARDWARE_RADEON		2

#ifndef __KM_C__
extern int km_debug;
#endif

#define KM_DEBUG   if(km_debug)printk

#if 1
#define KM_CHECKPOINT printk("**CKPT %s %s %d\n", __FILE__, __FUNCTION__, __LINE__);
#else
#define KM_CHECKPOINT
#endif
#endif
