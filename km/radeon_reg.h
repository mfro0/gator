#ifndef __RADEON_REG_H__
#define __RADEON_REG_H__

#define RADEON_BUS_CNTL		0x30

#define RADEON_GEN_INT_STATUS 0x44
#define RADEON_GEN_INT_CNTL   0x40
#define RADEON_CAP_INT_CNTL   0x908
#define RADEON_CAP_INT_STATUS 0x90C
#define RADEON_RBBM_STATUS    0x1740
#define RADEON_ENGINE_ACTIVE				   (1<<31)

#define RADEON_SCALE_CNTL	0x420


#define RADEON_CAP0_BUF0_OFFSET 	0x920
#define RADEON_CAP0_BUF1_OFFSET 	0x924
#define RADEON_CAP0_BUF0_EVEN_OFFSET 	0x928
#define RADEON_CAP0_BUF1_EVEN_OFFSET 	0x92C

#define RADEON_CAP0_BUF_PITCH		0x930
#define RADEON_CAP0_V_WINDOW		0x934

#define RADEON_CAP0_CONFIG		0x958
#define RADEON_FCP_CNTL			0x910
#define RADEON_TRIG_CNTL		0x950

#define RADEON_DMA_GUI_STATUS    0x790
#define RADEON_DMA_GUI_TABLE_ADDR 0x780

#define BM_FRAME_BUF_OFFSET                        0x0a00
#define BM_SYSTEM_MEM_ADDR                         0x0a04
#define BM_COMMAND                                 0x0a08
#define BM_STATUS                                  0x0a0c
#define BM_QUEUE_STATUS                            0x0a10
#define BM_QUEUE_FREE_STATUS                       0x0A14
#define BM_CHUNK_0_VAL                             0x0a18
#define BM_CHUNK_1_VAL                             0x0a1C
#define BM_VIP0_BUF                                0x0A20
#define BM_VIP0_ACTIVE                             0x0A24
#define BM_VIP1_BUF                                0x0A30
#define BM_VIP1_ACTIVE                             0x0A34
#define BM_VIP2_BUF                                0x0A40
#define BM_VIP2_ACTIVE                             0x0A44
#define BM_VIP3_BUF                                0x0A50
#define BM_VIP3_ACTIVE                             0x0A54
#define BM_VIDCAP_BUF0                             0x0a60
#define BM_VIDCAP_BUF1                             0x0a64
#define BM_VIDCAP_BUF2                             0x0a68
#define BM_VIDCAP_ACTIVE                           0x0a6c
#define BM_GUI                                     0x0a80


#define RADEON_DMA_GUI_COMMAND__BYTE_COUNT_MASK                   0x001fffff
#define RADEON_DMA_GUI_COMMAND__INTDIS                            0x40000000
#define RADEON_DMA_GUI_COMMAND__EOL                               0x80000000

#define RADEON_MC_FB_LOCATION			0x0148
#define RADEON_DISPLAY_BASE_ADDR		0x023C
#define RADEON_OVERLAY_BASE_ADDR		0x043C
#define RADEON_DEFAULT_OFFSET			0x16E0

#define RADEON_CONFIG_APER_SIZE			0x0108

#endif
