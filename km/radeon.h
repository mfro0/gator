#ifndef __RADEON_H__
#define __RADEON_H__

int radeon_is_capture_active(KM_STRUCT *kms);
void radeon_get_window_parameters(KM_STRUCT *kms, struct video_window *vwin);
void radeon_start_transfer(KM_STRUCT *kms);
void radeon_stop_transfer(KM_STRUCT *kms);
int radeon_is_capture_irq_active(KM_STRUCT *kms);



#endif
