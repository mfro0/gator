/*     km preliminary version

       (C) Vladimir Dergachev 2001-2002
       
       GNU Public License
       
*/

#ifndef __RADEON_H__
#define __RADEON_H__

int radeon_is_capture_active(KM_STRUCT *kms);
void radeon_get_window_parameters(KM_STRUCT *kms, struct video_window *vwin);
void radeon_start_transfer(KM_STRUCT *kms);
void radeon_stop_transfer(KM_STRUCT *kms);
void radeon_km_irq(int irq, void *dev_id, struct pt_regs *regs);
int radeon_allocate_single_frame_buffer(KM_STRUCT *kms, SINGLE_FRAME *frame, long size);
int radeon_init_hardware(KM_STRUCT *kms);
int radeon_uninit_hardware(KM_STRUCT *kms);



#endif
