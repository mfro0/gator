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

#ifndef GENERIC_RAGE128_HEADER
#define GENERIC_RAGE128_HEADER 1

#define R128_CLOCK_FREQ   80000 /* 7.5kHz .. 100kHz */

#define R128_CLOCK_CNTL_INDEX 		(*((u32*)(card->MMR+0x0008)))
#define R128_CLOCK_CNTL_INDEX0 		(*(( u8*)(card->MMR+0x0008)))
#define R128_CLOCK_CNTL_INDEX1 		(*(( u8*)(card->MMR+0x0009)))
#define R128_CLOCK_CNTL_DATA            (*((u32*)(card->MMR+0x000C)))
#define FCP_CNTL                     0x012
#define R128_PLL_WR_EN               (1 << 7)

#define R128_CAP0_BUF_PITCH             (*((u32*)(card->MMR+0x0930)))
#define R128_CAP1_BUF_PITCH		(*((u32*)(card->MMR+0x09A0)))
#define R128_CAP0_V_WINDOW              (*((u32*)(card->MMR+0x0934)))
#define R128_CAP0_H_WINDOW              (*((u32*)(card->MMR+0x0938)))
#define R128_CAP1_V_WINDOW              (*((u32*)(card->MMR+0x09A4)))
#define R128_CAP1_H_WINDOW              (*((u32*)(card->MMR+0x09A8)))
#define R128_CAP0_VBI_V_WINDOW          (*((u32*)(card->MMR+0x0944)))
#define R128_CAP0_VBI_H_WINDOW          (*((u32*)(card->MMR+0x0948)))
#define R128_CAP1_VBI_V_WINDOW          (*((u32*)(card->MMR+0x09B4)))
#define R128_CAP1_VBI_H_WINDOW          (*((u32*)(card->MMR+0x09B8)))
#define R128_CAP0_BUF0_OFFSET        (*((u32*)(card->MMR+0x0920)))
#define R128_CAP0_BUF1_OFFSET        (*((u32*)(card->MMR+0x0924)))
#define R128_CAP0_BUF0_EVEN_OFFSET   (*((u32*)(card->MMR+0x0928)))
#define R128_CAP0_BUF1_EVEN_OFFSET   (*((u32*)(card->MMR+0x092C)))
#define R128_CAP0_VBI_ODD_OFFSET        (*((u32*)(card->MMR+0x093C)))
#define R128_CAP0_VBI_EVEN_OFFSET       (*((u32*)(card->MMR+0x0940)))
#define R128_CAP1_VBI_ODD_OFFSET        (*((u32*)(card->MMR+0x09AC)))
#define R128_CAP1_VBI_EVEN_OFFSET       (*((u32*)(card->MMR+0x09B0)))
#define R128_CAP0_ONESHOT_BUF_OFFSET 	(*((u32*)(card->MMR+0x096C)))
#define R128_CAP1_ONESHOT_BUF_OFFSET 	(*((u32*)(card->MMR+0x09DC)))
#define R128_CAPTURE_PORT_MODE_CNTL     (*((u32*)(card->MMR+0x094C)))	
#define R128_CAP0_ANC_ODD_OFFSET        (*((u32*)(card->MMR+0x095C)))
#define R128_CAP0_ANC_EVEN_OFFSET       (*((u32*)(card->MMR+0x0960)))
#define R128_CAP1_ANC_ODD_OFFSET        (*((u32*)(card->MMR+0x09CC)))
#define R128_CAP1_ANC_EVEN_OFFSET       (*((u32*)(card->MMR+0x09D0)))


#define R128_I2C_CNTL_0                 (*((u32*)(card->MMR+0x0090)))
#define R128_I2C_CNTL_0_0               (*(( u8*)(card->MMR+0x0090)))
#define R128_I2C_CNTL_0_1               (*(( u8*)(card->MMR+0x0091)))
#define R128_I2C_CNTL_1                 (*((u32*)(card->MMR+0x0094)))
#define R128_I2C_DATA                   (*(( u8*)(card->MMR+0x0098)))
#define R128_CONFIG_MEMSIZE             (*((u32*)(card->MMR+0x00F8)))
#define R128_CONFIG_APER_0_BASE         (*((u32*)(card->MMR+0x0100)))

#define R128_VIDEOMUX_CNTL              (*((u32*)(card->MMR+0x0190)))
#define R128_DEVICE_ID                  (*((u16*)(card->MMR+0x0F02)))
#define R128_REVISION_ID                (*(( u8*)(card->MMR+0x0F08)))

/* R128_I2C_CNTL_0 bits */
#define I2C_DONE        0x00000001
#define I2C_NACK        0x00000002
#define I2C_HALT        0x00000004
#define I2C_SOFT_RST    0x00000020
#define I2C_DRIVE_EN    0x00000040
#define I2C_DRIVE_SEL   0x00000080
#define I2C_START       0x00000100
#define I2C_STOP        0x00000200
#define I2C_RECEIVE     0x00000400
#define I2C_ABORT       0x00000800
#define I2C_GO          0x00001000
#define I2C_CLEAR       (I2C_DONE|I2C_NACK|I2C_HALT|I2C_SOFT_RST)

/* R128_I2C_CNTL_1 bits */
#define I2C_SEL         0x00010000
#define I2C_EN          0x00020000

#define R128_CAP0_TRIG_CNTL			(*((u32*)(card->MMR+0x0950)))
#define R128_CAP1_TRIG_CNTL			(*((u32*)(card->MMR+0x09C0)))
#define R128_CAP0_TRIG_CNTL_TRIGGER_GET         0x00000003        
#define R128_CAP0_TRIG_CNTL_TRIGGER_SET         0x00000001        
#define R128_CAP0_TRIG_CNTL_CAPTURE_EN          0x00000010        
#define R128_CAP0_TRIG_CNTL_VSYNC_GET           0x0000FF00        
#define R128_CAP0_TRIG_CNTL_VSYNC_SET           0x00010000        

#define R128_BUS_CNTL                     (*((u32*)(card->MMR+0x0030)))
#define R128_BUS_MASTER_RESET             (1 << 1)
#define R128_BUS_MASTER_DIS               (1 << 6)
#define R128_BUS_RD_DISCARD_EN            (1 << 24)
#define R128_BUS_RD_ABORT_EN              (1 << 25)
#define R128_BUS_MSTR_DISCONNECT_EN       (1 << 28)
#define R128_BUS_WRT_BURST                (1 << 29)
#define R128_BUS_READ_BURST               (1 << 30)

#define R128_BM_VIDCAP_BUF0                (*((u32*)(card->MMR+0xA60)))
#define R128_BM_VIDCAP_BUF1                (*((u32*)(card->MMR+0xA64)))
#define R128_BM_VIDCAP_BUF2                (*((u32*)(card->MMR+0xA68)))

#define R128_BM_CHUNK_0_VAL             (*((u32*)(card->MMR+0xA18)))
#define R128_BM_PTR_FORCE_TO_PCI        (1 << 21)
#define R128_BM_PM4_RD_FORCE_TO_PCI     (1 << 22)
#define R128_BM_GLOBAL_FORCE_TO_PCI     (1 << 23)

#define RAGE128_BM_FORCE_TO_PCI            0x20000000
#define R128_BM_CHUNK_1_VAL             (*((u32*)(card->MMR+0xA1C)))

#define R128_GEN_INT_STATUS             (*((u32*)(card->MMR+0x44)))
#define R128_GEN_INT_CNTL 		(*((u32*)(card->MMR+0x40)))
#define R128_CAP_INT_CNTL               (*((u32*)(card->MMR+0x908)))
#define R128_CAP_INT_STATUS             (*((u32*)(card->MMR+0x90C)))
#define R128_CAP0_DEBUG                 (*((u32*)(card->MMR+0x0954)))


#define R128_CAP0_CONFIG                        (*((u32*)(card->MMR+0x0958)))
#define R128_CAP1_CONFIG                        (*((u32*)(card->MMR+0x09C8)))
#define R128_CAP0_CONFIG_CONTINUOS              0x00000001
#define R128_CAP0_CONFIG_START_FIELD_EVEN       0x00000002
#define R128_CAP0_CONFIG_START_BUF_GET          0x00000004
#define R128_CAP0_CONFIG_START_BUF_SET          0x00000008
#define R128_CAP0_CONFIG_BUF_TYPE_ALT           0x00000010
#define R128_CAP0_CONFIG_BUF_TYPE_FRAME         0x00000020
#define R128_CAP0_CONFIG_ONESHOT_MODE_FRAME     0x00000040
#define R128_CAP0_CONFIG_BUF_MODE_DOUBLE        0x00000080
#define R128_CAP0_CONFIG_BUF_MODE_TRIPLE        0x00000100
#define R128_CAP0_CONFIG_MIRROR_EN              0x00000200
#define R128_CAP0_CONFIG_ONESHOT_MIRROR_EN      0x00000400
#define R128_CAP0_CONFIG_VIDEO_SIGNED_UV        0x00000800
#define R128_CAP0_CONFIG_ANC_DECODE_EN          0x00001000
#define R128_CAP0_CONFIG_VBI_EN                 0x00002000
#define R128_CAP0_CONFIG_SOFT_PULL_DOWN_EN      0x00004000
#define R128_CAP0_CONFIG_VIP_EXTEND_FLAG_EN     0x00008000
#define R128_CAP0_CONFIG_FAKE_FIELD_EN          0x00010000
#define R128_CAP0_CONFIG_ODD_ONE_MORE_LINE      0x00020000
#define R128_CAP0_CONFIG_EVEN_ONE_MORE_LINE     0x00040000
#define R128_CAP0_CONFIG_HORZ_DIVIDE_2          0x00080000
#define R128_CAP0_CONFIG_HORZ_DIVIDE_4          0x00100000
#define R128_CAP0_CONFIG_VERT_DIVIDE_2          0x00200000
#define R128_CAP0_CONFIG_VERT_DIVIDE_4          0x00400000
#define R128_CAP0_CONFIG_FORMAT_BROOKTREE       0x00000000
#define R128_CAP0_CONFIG_FORMAT_CCIR656         0x00800000
#define R128_CAP0_CONFIG_FORMAT_ZV              0x01000000
#define R128_CAP0_CONFIG_FORMAT_VIP             0x01800000
#define R128_CAP0_CONFIG_FORMAT_TRANSPORT       0x02000000
#define R128_CAP0_CONFIG_HORZ_DECIMATOR         0x04000000
#define R128_CAP0_CONFIG_VIDEO_IN_YVYU422       0x00000000
#define R128_CAP0_CONFIG_VIDEO_IN_VYUY422       0x20000000

#define R128_SYSTEM_TRIGGER_SYSTEM_TO_VIDEO  0x0
#define R128_SYSTEM_TRIGGER_VIDEO_TO_SYSTEM  0x1


int r128_inita(GENERIC_CARD *card);
void rage128_enable_capture(GENERIC_CARD *card);
void rage128_disable_capture(GENERIC_CARD *card);

#endif
