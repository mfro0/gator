#ifndef __RADEON_REG_H__
#define __RADEON_REG_H__

#define GEN_INT_STATUS 0x44
#define CAP_INT_CNTL   0x908
#define CAP_INT_STATUS 0x90C

#define CAP0_BUF0_OFFSET 	0x920
#define CAP0_BUF1_OFFSET 	0x924
#define CAP0_BUF0_EVEN_OFFSET 	0x928
#define CAP0_BUF1_EVEN_OFFSET 	0x92C

#define DMA_GUI_STATUS    0x790
#define DMA_GUI_TABLE_ADDR 0x780

#define DMA_GUI_COMMAND__BYTE_COUNT_MASK                   0x001fffff
#define DMA_GUI_COMMAND__INTDIS                            0x40000000
#define DMA_GUI_COMMAND__EOL                               0x80000000



#endif
