#ifndef __KM_H__
#define __KM_H__

#include <linux/videodev.h>

#define KM_VERSION      "alpha-1.0"

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
	long irq;
	struct pci_dev *dev;
	long interrupt_count;
	unsigned char * reg_aperture;
	int buf_read_from;
	SINGLE_FRAME frame;
	SINGLE_FRAME frame_even;
	wait_queue_head_t frameq;
	long total_frames;
	long overrun;
	int  (*is_capture_active)(struct S_KM_STRUCT *kms);
	void (*get_window_parameters)(struct S_KM_STRUCT *kms, struct video_window *vwin);
	void (*start_transfer)(struct S_KM_STRUCT *kms);
	void (*stop_transfer)(struct S_KM_STRUCT *kms);
	int (*allocate_single_frame_buffer)(struct S_KM_STRUCT *kms, SINGLE_FRAME *frame, long size);
	void (*deallocate_single_frame_buffer)(struct S_KM_STRUCT *kms, SINGLE_FRAME *frame);
	} KM_STRUCT;

int acknowledge_dma(KM_STRUCT *kms);

#endif
