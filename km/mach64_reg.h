#ifndef __MACH64_REG_H__
#define __MACH64_REG_H__

#define MACH64_FIFO_STAT	(0xC4*4+1024)
#define MACH64_GUI_STAT		(0xCE*4+1024)
#define MACH64_BUS_CNTL		(0x28*4+1024)

#define MACH64_CRTC_INT_CNTL (0x06*4+1024)

#define MACH64_CAPBUF0_INT_EN  (1<<16)
#define MACH64_CAPBUF0_INT_ACK (1<<17)
#define MACH64_CAPBUF1_INT_EN  (1<<18)
#define MACH64_CAPBUF1_INT_ACK (1<<19)
#define MACH64_BUSMASTER_INT_EN  (1<<24)
#define MACH64_BUSMASTER_INT_ACK (1<<25)
#define MACH64_ACKS_MASK        ((1<<2)|(1<<4)|(1<<8)|(1<<10)|(1<<17)|(1<<19)|(1<<21)|(1<<23)|(1<<25)|(1<<31))

#define ACK_INTERRUPT(value, acks)    ( ( (value) & ~MACH64_ACKS_MASK) | (acks))

#define MACH64_CAP0_BUF0_OFFSET 	(0x20*4)
#define MACH64_CAP0_BUF0_EVEN_OFFSET 	(0x21*4)

#define MACH64_CAP0_X_WIDTH		(0x11*4)
#define MACH64_CAP0_START_END		(0x10*4)

#define MACH64_CAP0_CONFIG		(0x14*4)

#define MACH64_BM_STATUS    		(0x63*4)
#define MACH64_BM_SYSTEM_TABLE   	(0x6f*4)
#define MACH64_SYSTEM_TRIGGER_SYSTEM_TO_VIDEO 	0x0
#define MACH64_SYSTEM_TRIGGER_VIDEO_TO_SYSTEM	0x1
#define MACH64_SYSTEM_TRIGGER_VIDEO_TO_SYSTEM_AFTER_BUF0_READY	0x2
#define MACH64_SYSTEM_TRIGGER_VIDEO_TO_SYSTEM_AFTER_BUF1_READY	0x3
#define MACH64_SYSTEM_TRIGGER_VIDEO_TO_SYSTEM_AFTER_SNAPSHOT_READY	0x4


#define MACH64_DMA_GUI_COMMAND__BYTE_COUNT_MASK                   0x001fffff
#define MACH64_DMA_GUI_COMMAND__HOLD_VIDEO_OFFSET                 0x40000000
#define MACH64_DMA_GUI_COMMAND__EOL                               0x80000000



#endif
