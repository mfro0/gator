/*     km preliminary version

       (C) Vladimir Dergachev 2001-2003
       
       GNU Public License
       
*/

#ifndef __RAGE128_H__
#define __RAGE128_H__

int rage128_is_capture_active(KM_STRUCT *kms);
void rage128_get_window_parameters(KM_STRUCT *kms, struct video_window *vwin);
void rage128_start_transfer(KM_STRUCT *kms);
void rage128_stop_transfer(KM_STRUCT *kms);
irqreturn_t rage128_km_irq(int irq, void *dev_id, struct pt_regs *regs);



#endif
