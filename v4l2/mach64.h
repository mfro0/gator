/* 
    This file is part of genericv4l.

    genericv4l is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    genericv4l is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef GENERIC_MACH64_HEADER
#define GENERIC_MACH64_HEADER 1

#define MACH64_DMA_GUI_COMMAND__EOL     0x80000000

#define MACH64_CONFIG_CHIP_ID        (*(card->MEM_0+0x38))
#define MACH64_MEM_CNTL              (*(card->MEM_0+0x2C))
#define MACH64_DAC_CNTL              (*(card->MEM_0+0x31))
#define MACH64_DAC_CNTL_PTR            (card->MEM_0+0x31)
#define MACH64_GEN_TEST_CNTL         (*(card->MEM_0+0x34))
#define MACH64_GEN_TEST_CNTL_PTR       (card->MEM_0+0x34)
#define MACH64_GP_IO                 (*(card->MEM_0+0x1E))
#define MACH64_GP_IO_PTR               (card->MEM_0+0x1E)
#define MACH64_CRTC_H_TOTAL_DISP     (*(card->MEM_0+0x00))
#define MACH64_CRTC_V_TOTAL_DISP     (*(card->MEM_0+0x02))
#define MACH64_CRTC_INT_CNTL         (*(card->MEM_0+0x06))
#define MACH64_BUS_CNTL              (*(card->MEM_0+0x28))
#define MACH64_CRTC_GEN_CNTL         (*(card->MEM_0+0x07))
#define MACH64_TV_I2C_CNTL             0x0015
#define MACH64_I2C_CNTL_0            (*(card->MEM_0+0x0F))
#define MACH64_I2C_CNTL_1            (*(card->MEM_0+0x2F))
#define MACH64_EXT_DAC_REGS          (*(card->MEM_0+0x32))
#define MACH64_FIFO_STAT	     (*(card->MEM_0+0xC4))
#define MACH64_GUI_STAT              (*(card->MEM_0+0xCE))
#define MACH64_BM_SYSTEM_TABLE       (*(card->MEM_1+0x6f)) 

/* bus control stuff */
#define MACH64_BUS_APER_REG_DIS		0x00000010ul
#define MACH64_BUS_MSTR_RESET		0x00000002ul
#define MACH64_BUS_FLUSH_BUF		0x00000004ul
#define MACH64_BUS_PCI_DAC_DLY		0x08000000ul
#define MACH64_BUS_RD_DISCARD_EN	0x01000000ul
#define MACH64_BUS_RD_ABORT_EN		0x02000000ul
#define MACH64_BUS_MASTER_DIS		0x00000040ul

/* Video Capture Registers */
#define MACH64_CAPTURE_START_END       (*(card->MEM_1+0x10))
#define MACH64_CAPTURE_X_WIDTH         (*(card->MEM_1+0x11))
#define MACH64_VIDEO_FORMAT            (*(card->MEM_1+0x12))
#define MACH64_VIDEO_IN			0x0000000ful
#define MACH64_VIDEO_VYUY422		0x0000000bul
#define MACH64_VIDEO_YVYU422		0x0000000cul
#define MACH64_VIDEO_SIGNED_UV          0x00000010ul

#define MACH64_VBI_START_END           (*(card->MEM_1+0x13))
#define MACH64_CAPTURE_CONFIG          (*(card->MEM_1+0x14))
#define MACH64_CAP_INPUT_MODE		(1<<0)
#define MACH64_CAP_START_FIELD		(1<<1)
#define MACH64_CAP_BUF_MODE		(1<<2)
#define MACH64_CAP_START_BUF		(1<<3)
#define MACH64_CAP_BUF_ALTERNATING	(1<<4)
#define MACH64_CAP_BUF_FRAME		(1<<5)
#define MACH64_CAP_FIELD_FLIP		(1<<6)
//#define MACH64_CAP_CCIR656_EN		(1<<7)

/* mirror image (horizontal flip) */
#define MACH64_CAP_MIRROR_EN		(1<<12)
#define MACH64_ONESHOT_MIRROR_EN	(1<<13)

#define MACH64_ONESHOT_MODE	        (1<<14)
/* ovl, I think thats overlay... so we dont need it */
#define MACH64_OVL_BUF_MODE		(1<<29)

#define MACH64_TRIG_CNTL               (*(card->MEM_1+0x15))
#define MACH64_CAP_BUF_STATUS		(1<<7)
#define MACH64_CAPTURE_EN		(1<<31)

#define MACH64_VBI_WIDTH               (*(card->MEM_1+0x18))
/* tell it where the capture buffers are located in atifb mem */
#define MACH64_CAPTURE_BUF0_OFFSET     (*(card->MEM_1+0x20))
#define MACH64_CAPTURE_BUF1_OFFSET     (*(card->MEM_1+0x21))
#define MACH64_ONESHOT_BUF_OFFSET      (*(card->MEM_1+0x22))

#define MACH64_CAPBUF0_INT_EN  (1<<16)
#define MACH64_CAPBUF0_INT_ACK (1<<17)
#define MACH64_CAPBUF1_INT_EN  (1<<18)
#define MACH64_CAPBUF1_INT_ACK (1<<19)
#define MACH64_CAPONESHOT_INT_EN  (1<<22)
#define MACH64_CAPONESHOT_INT_ACK (1<<23)
#define MACH64_BUSMASTER_INT_EN  (1<<24)
#define MACH64_BUSMASTER_INT_ACK (1<<25)

int m64_inita(GENERIC_CARD *card);
void mach64_enable_capture(GENERIC_CARD *card);
void mach64_disable_capture(GENERIC_CARD *card);

#endif
