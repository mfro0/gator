/*     km preliminary version

       (C) Vladimir Dergachev 2001-2002
       
       GNU Public License
       
*/

#ifndef __MACH64_H__
#define __MACH64_H__

int mach64_is_capture_active(KM_STRUCT *kms);
void mach64_get_window_parameters(KM_STRUCT *kms, struct video_window *vwin);
void mach64_start_transfer(KM_STRUCT *kms);
void mach64_stop_transfer(KM_STRUCT *kms);
void mach64_km_irq(int irq, void *dev_id, struct pt_regs *regs);
int mach64_allocate_v4l_dvb(KM_STRUCT *kms, long size);
int mach64_deallocate_v4l_dvb(KM_STRUCT *kms);



#endif
