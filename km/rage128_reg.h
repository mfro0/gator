#ifndef __RAGE128_REG_H__
#define __RAGE128_REG_H__

#define RAGE128_BUS_CNTL      0x30
#define RAGE128_BUS_CNTL__BM_RESET	(1<<1)

#define RAGE128_GEN_INT_STATUS 0x44
#define RAGE128_GEN_INT_CNTL   0x40
#define RAGE128_CAP_INT_CNTL   0x908
#define RAGE128_CAP_INT_STATUS 0x90C

#define RAGE128_CAP0_BUF0_OFFSET 	0x920
#define RAGE128_CAP0_BUF1_OFFSET 	0x924
#define RAGE128_CAP0_BUF0_EVEN_OFFSET 	0x928
#define RAGE128_CAP0_BUF1_EVEN_OFFSET 	0x92C

#define RAGE128_CAP0_BUF_PITCH		0x930
#define RAGE128_CAP0_V_WINDOW		0x934

#define RAGE128_CAP0_CONFIG		0x958

#define RAGE128_BM_FRAME_BUF_OFFSET                        0xA00
#define RAGE128_BM_SYSTEM_MEM_ADDR                         0xA04
#define RAGE128_BM_COMMAND                                 0xA08
#define RAGE128_BM_STATUS                                  0xA0c
#define RAGE128_BM_QUEUE_STATUS                            0xA10
#define RAGE128_BM_QUEUE_FREE_STATUS                       0xA14
#define RAGE128_BM_CHUNK_0_VAL                             0xA18
#define RAGE128_BM_CHUNK_1_VAL                             0xA1C
#define RAGE128_BM_VIP0_BUF                                0xA20
#define RAGE128_BM_VIP0_ACTIVE                             0xA24
#define RAGE128_BM_VIP1_BUF                                0xA30
#define RAGE128_BM_VIP1_ACTIVE                             0xA34
#define RAGE128_BM_VIP2_BUF                                0xA40
#define RAGE128_BM_VIP2_ACTIVE                             0xA44
#define RAGE128_BM_VIP3_BUF                                0xA50
#define RAGE128_BM_VIP3_ACTIVE                             0xA54
#define RAGE128_BM_VIDCAP_BUF0                             0xA60
#define RAGE128_BM_VIDCAP_BUF1                             0xA64
#define RAGE128_BM_VIDCAP_BUF2                             0xA68
#define RAGE128_BM_VIDCAP_ACTIVE                           0xA6c
#define RAGE128_BM_GUI                                     0xA80

/* RAGE128_BM_CHUNK_0_VAL bit constants */
#define RAGE128_BM_PTR_FORCE_TO_PCI                        0x00200000
#define RAGE128_BM_PM4_RD_FORCE_TO_PCI                     0x00400000
#define RAGE128_BM_GLOBAL_FORCE_TO_PCI                     0x00800000
#define RAGE128_BM_VIP3_NOCHUNK                            0x10000000
#define RAGE128_BM_VIP2_NOCHUNK                            0x20000000
#define RAGE128_BM_VIP1_NOCHUNK                            0x40000000
#define RAGE128_BM_VIP0_NOCHUNK                            0x80000000

/* RAGE128_BM_COMMAND bit constants */
#define RAGE128_BM_INTERRUPT_DIS                           0x08000000
#define RAGE128_BM_TRANSFER_DEST_REG                       0x10000000
#define RAGE128_BM_FORCE_TO_PCI                            0x20000000
#define RAGE128_BM_FRAME_OFFSET_HOLD                       0x40000000
#define RAGE128_BM_END_OF_LIST                             0x80000000



#define RAGE128_SYSTEM_TRIGGER_SYSTEM_TO_VIDEO 	0x0
#define RAGE128_SYSTEM_TRIGGER_VIDEO_TO_SYSTEM	0x1

#define RAGE128_GUI_STAT                                   0x1740
#define RAGE128_ENGINE_ACTIVE				   (1<<31)

#define RAGE128_DMA_GUI_COMMAND__BYTE_COUNT_MASK                   0x001fffff
#define RAGE128_DMA_GUI_COMMAND__INTDIS                            0x40000000
#define RAGE128_DMA_GUI_COMMAND__EOL                               0x80000000



#endif
