#ifndef __MACH64_H__
#define __MACH64_H__

int mach64_is_capture_active(KM_STRUCT *kms);
void mach64_get_window_parameters(KM_STRUCT *kms, struct video_window *vwin);
void mach64_start_transfer(KM_STRUCT *kms);
void mach64_stop_transfer(KM_STRUCT *kms);
void mach64_km_irq(int irq, void *dev_id, struct pt_regs *regs);
int mach64_allocate_single_frame_buffer(KM_STRUCT *kms, SINGLE_FRAME *frame, long size);



#endif
