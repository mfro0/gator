/* genericv4l - ATI mach64 capture card driver

   Based on Gatos/KM (http://gatos.sourceforge.net/)
   and bttv-0.9.10 for the v4l2 layout

   Written by Eric Sellers

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

#if defined(CONFIG_MODVERSIONS) && ! defined(MODVERSIONS)
#include <linux/modversions.h> 
#define MODVERSIONS
#endif

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>

#include "generic.h"
#include "i2c.h"
#include "bt829.h"
#include "board.h"
#include "mach64.h"
#include "rage128.h"
#include "memory.h"
#include "queue.h"
#include "pci_id.h"

MODULE_DESCRIPTION("GENERIC v4l/v4l2 driver module for ATI Mach64 TV cards");
MODULE_AUTHOR("Eric Sellers <sellers-eric@rogers.com>");
MODULE_LICENSE("GPL");

/* any params that this module can accept are listed here */
int debug=0;
MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Debugging level");
int disablev4l2=0;
MODULE_PARM(disablev4l2, "i");
MODULE_PARM_DESC(disablev4l2, "disable v4l2 support");
int disabledma=0;
MODULE_PARM(disabledma, "i");
MODULE_PARM_DESC(disabledma, "disable dma support");
int disableinterlace=0;
MODULE_PARM(disableinterlace, "i");
MODULE_PARM_DESC(disableinterlace, "disable interlace modes");
int tunertype=-1;
MODULE_PARM(tunertype, "i");
MODULE_PARM_DESC(tunertype, "Tuner type, 0=pal, 1=ntsc, 2=secam, 3=pal nc, 4=pal m, 5= pal n, 6=ntsc jp");

struct proc_dir_entry *proc_dir;

/* put any cards you want to support in this list, any card
that matches this will be passed to generic_probe for further 
testing and initialization */
static struct pci_device_id generic_pci_tbl[] __devinitdata = {
     /* Mach64 / Rage */
    {
     vendor:      PCI_VENDOR_ID_ATI,
     device:      PCI_DEVICE_ID_ATI_215GTB,
     subvendor:   PCI_ANY_ID,
     subdevice:   PCI_ANY_ID,
     class:       0,
     class_mask:  0,
     driver_data: MACH64CHIP /* can use to store extra data here */
    },
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215GB,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, MACH64CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215GD,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, MACH64CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215GI,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, MACH64CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215GP,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, MACH64CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215GQ,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, MACH64CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215XL,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, MACH64CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215GT,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, MACH64CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215_IV,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, MACH64CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215_IW,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, MACH64CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215_IZ,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, MACH64CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215_LB,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, MACH64CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215_LD,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, MACH64CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215_LG,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, MACH64CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215_LI,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, MACH64CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215_LM,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, MACH64CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215_LN,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, MACH64CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215_LR,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, MACH64CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215_LS,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, MACH64CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_264_LT,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, MACH64CHIP},
     /* Mach64 VT */
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_264VT,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, MACH64CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_264VU,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, MACH64CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_264VV,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, MACH64CHIP},
     /* Rage128 Pro GL */
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PA,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PB,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PC,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PD,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PE,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PF,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
     /* Rage128 Pro VR */
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PG,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PH,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PI,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PJ,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PK,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PL,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PM,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PN,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PO,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PP,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PQ,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PR,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PS,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PT,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PU,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PV,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PW,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_PX,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
     /* Rage128 GL */
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RE,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RF,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RG,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
     /* Rage128 VR */
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RK,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_RL,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_SE,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_SF,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_SG,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_SH,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_SK,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_SL,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_SM,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_SN,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
     /* Rage128 M3 */ 
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_LE,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_LF,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
     /* Rage128 M4 */
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_MF,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_ML,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
     /* Rage128 Ultra */
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_TF,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_TL,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_TR,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_TS,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_TT,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RAGE128_TU,
     PCI_ANY_ID, PCI_ANY_ID, 0, 0, RAGE128CHIP},
    {0,}
};

/* export what devices we support with this module */
MODULE_DEVICE_TABLE(pci, generic_pci_tbl);

char *tag="genericv4l";
GENERIC_CARD cards[MAX_CARDS];

/* used to keep track of how many cards are detected */
unsigned int num_cards_detected;

static DECLARE_WAIT_QUEUE_HEAD(generic_wait);
static DECLARE_TASKLET_DISABLED(generic_tasklet, generic_blockhandler, 0);


/* ---------------------------------------------------------------------- */
/* STATIC DATA                                                            */

/* pass this struct to pci_module_init and it will call generic_probe
on any cards that match generic_pci_tbl */
static struct pci_driver generic_pci_driver = {
        name:      "genericv4l",
        id_table:  generic_pci_tbl,
        probe:     generic_probe,
        remove:    __devexit_p(generic_remove),
};

/* for each file operation assign a function to handle them */
static struct file_operations generic_fops =
{
        owner:     THIS_MODULE,
        llseek:    no_llseek, /* cant seek on a capture card */
        read:      generic_read,
        write:     generic_write,
        ioctl:     generic_ioctl,
        mmap:      generic_mmap,
        open:      generic_open,
        release:   generic_release,
        poll:      generic_poll,
};

/* defaults for setting up a cards video4linux capture device */
static struct video_device generic_video_template =
{
        name:     "UNSET",
        type:      VID_TYPE_CAPTURE|VID_TYPE_TUNER|VID_TYPE_OVERLAY|
                   VID_TYPE_CLIPPING|VID_TYPE_SCALES,
        hardware:  VID_HARDWARE_BT848, /* look in videodev.h for a list NONE IN THERE FOR bt829 */
        fops:      &generic_fops, /* file operations table above */
        minor:     -1,
};

/* defaults for boards that support closed captioning interface */
static struct video_device generic_vbi_template =
{
        name:      "generic vbi",
        type:      VID_TYPE_TUNER|VID_TYPE_TELETEXT,
        hardware:  VID_HARDWARE_BT848, /* look in videodev.h for a list */
        fops:      &generic_fops, /* file operations table above */
        minor:     -1,
};

/* list properties of different card models here 
card types are listed in mach64.h (eg CARD_ALL_IN_WONDER = 1) */
struct generic_tvcard generic_tvcards[] = {
{
        name:            "ATI Mach 64 Generic",
        video_inputs:    3, //1=Composite,  2=tuner, 3=S-Video
        audio_inputs:    1,
        tuner:           1,
        svhs:            2,
        captmin_x:       48,
        captmin_y:       32,
},{
        name:            "ATI Mach 64 Stand alone",
        video_inputs:    3, //1=Composite,  2=tuner, 3=S-Video
        audio_inputs:    1,
        tuner:           1,
        svhs:            2,
        captmin_x:       48,
        captmin_y:       32,
},{
        name:            "ATI Mach 64 All in wonder",
        video_inputs:    3, //1=Composite,  2=tuner, 3=S-Video
        audio_inputs:    1,
        tuner:           1,
        svhs:            2,
        captmin_x:       48,
        captmin_y:       32,
},{
        name:            "ATI Mach 64 All in wonder pro",
        video_inputs:    3, //1=Composite,  2=tuner, 3=S-Video
        audio_inputs:    1,
        tuner:           1,
        svhs:            2,
        captmin_x:       48,
        captmin_y:       32,
},{
        name:            "ATI Mach 64 All in wonder 128",
        video_inputs:    3, //1=Composite,  2=tuner, 3=S-Video
        audio_inputs:    1,
        tuner:           1,
        svhs:            2,
        captmin_x:       48,
        captmin_y:       32,
},{
        name:            "ATI Mach 64 NEC",
        video_inputs:    3, //1=Composite,  2=tuner, 3=S-Video
        audio_inputs:    1,
        tuner:           1,
        svhs:            2,
        captmin_x:       48,
        captmin_y:       32,
}
};
const unsigned int generic_num_tvcards = ARRAY_SIZE(generic_tvcards);

static const struct v4l2_queryctrl no_ctl = {
  .name  = "NOTUSED",
  .flags = V4L2_CTRL_FLAG_DISABLED,
};

/* generic controls for the card (eg bright/contrast) */
static const struct v4l2_queryctrl generic_ctls[] = {
        /* --- video --- */
        {
                id:             V4L2_CID_BRIGHTNESS,
                name:           "Brightness",
                minimum:        0,
                maximum:        65535,
                step:           256,
                default_value:  32768,
                type:           V4L2_CTRL_TYPE_INTEGER,
        },{
                id:             V4L2_CID_CONTRAST,
                name:           "Contrast",
                minimum:        0,
                maximum:        65535,
                step:           128,
                default_value:  32768,
                type:           V4L2_CTRL_TYPE_INTEGER,
        },{
                id:             V4L2_CID_SATURATION,
                name:           "Saturation",
                minimum:        0,
                maximum:        65535,
                step:           128,
                default_value:  32768,
                type:           V4L2_CTRL_TYPE_INTEGER,
        },{
                id:             V4L2_CID_HUE,
                name:           "Hue",
                minimum:        0,
                maximum:        65535,
                step:           256,
                default_value:  32768,
                type:           V4L2_CTRL_TYPE_INTEGER,
       },
        /* --- audio --- */
        {
                id:             V4L2_CID_AUDIO_MUTE,
                name:           "Mute",
                minimum:        0,
                maximum:        1,
                type:           V4L2_CTRL_TYPE_BOOLEAN,
        },{
                id:             V4L2_CID_AUDIO_VOLUME,
                name:           "Volume",
                minimum:        0,
                maximum:        65535,
                step:           65535/100,
                default_value:  65535,
                type:           V4L2_CTRL_TYPE_INTEGER,
        },{
                id:             V4L2_CID_AUDIO_BALANCE,
                name:           "Balance",
                minimum:        0,
                maximum:        65535,
                step:           65535/100,
                default_value:  32768,
                type:           V4L2_CTRL_TYPE_INTEGER,
        },{
                id:             V4L2_CID_AUDIO_BASS,
                name:           "Bass",
                minimum:        0,
                maximum:        65535,
                step:           65535/100,
                default_value:  32768,
                type:           V4L2_CTRL_TYPE_INTEGER,
        },{
                id:             V4L2_CID_AUDIO_TREBLE,
                name:           "Treble",
                minimum:        0,
                maximum:        65535,
                step:           65535/100,
                default_value:  32768,
                type:           V4L2_CTRL_TYPE_INTEGER,
        }
};
const int GENERIC_CTLS = ARRAY_SIZE(generic_ctls);

/* generic card video output formats */
const struct generic_format generic_formats[] = {
        {
                name:      "4:2:2, packed, YUYV",
                //palette:   VIDEO_PALETTE_YUYV,
                palette:   VIDEO_PALETTE_YUV422,
                fourcc:    V4L2_PIX_FMT_YUYV,
                depth:     16,
                flags:     FORMAT_FLAGS_PACKED,
/*        },{
                name:      "4:2:2, packed, UYVY",
                palette:   VIDEO_PALETTE_UYVY,
                fourcc:    V4L2_PIX_FMT_UYVY,
                depth:     16,
                flags:     FORMAT_FLAGS_PACKED,
*/
        }
};
const unsigned int GENERIC_FORMATS = ARRAY_SIZE(generic_formats);

struct generic_tvnorm generic_tvnorms[] = {
        {
                v4l2_id:         V4L2_STD_PAL,
                name:            "PAL",
                Fsc:             35468950,
                swidth:          768,
                sheight:         576,
                totalwidth:      1135,
                adelay:          0x7f,
                bdelay:          0x72,
                iform:           (GENERIC_IFORM_PAL_BDGHI|GENERIC_IFORM_XT1),
                scaledtwidth:    1135,
                hdelayx1:        186,
                hactivex1:       922,
                vdelay:          0x20,
                vbipack:         255,
                sram:            0,
        },{
                v4l2_id:         V4L2_STD_NTSC_M,
                name:            "NTSC",
                Fsc:             28636363,
                swidth:          720,
                sheight:         480,
                totalwidth:      910,
                adelay:          0x68,
                bdelay:          0x5d,
                iform:           (GENERIC_IFORM_NTSC|GENERIC_IFORM_XT0),
                scaledtwidth:    910,
                hdelayx1:        135,
                hactivex1:       754,
                vdelay:          0x1a,
                vbipack:         144,
                sram:            1,
        },{
                v4l2_id:         V4L2_STD_SECAM,
                name:            "SECAM",
                Fsc:             35468950,
                swidth:          768,
                sheight:         576,
                totalwidth:      1135,
                adelay:          0x7F,
                bdelay:          0xA0,
                iform:           (GENERIC_IFORM_SECAM|GENERIC_IFORM_XT1),
                scaledtwidth:    1135,
                hdelayx1:        186,
                hactivex1:       922,
                vdelay:          0x20,
                vbipack:         255,
                sram:           0,
        },{
                v4l2_id:         V4L2_STD_PAL_Nc,
                name:            "PAL-Nc",
                Fsc:             28636363,
                swidth:          720,
                sheight:         576,
                totalwidth:      910,
                adelay:          0x68,
                bdelay:          0x5d,
                iform:           (GENERIC_IFORM_PAL_NC|GENERIC_IFORM_XT0),
                scaledtwidth:    780,
                hdelayx1:        130,
                hactivex1:       754,
                vdelay:          0x1a,
                vbipack:         144,
                sram:            -1,
        },{
                v4l2_id:         V4L2_STD_PAL_M,
                name:            "PAL-M",
                Fsc:             28636363,
                swidth:          720,
                sheight:         480,
                totalwidth:      910,
                adelay:          0x68,
                bdelay:          0x5d,
                iform:           (GENERIC_IFORM_PAL_M|GENERIC_IFORM_XT0),
                scaledtwidth:    780,
                hdelayx1:        135,
                hactivex1:       754,
                vdelay:          0x1a,
                vbipack:         144,
                sram:            -1,
        },{
                v4l2_id:         V4L2_STD_PAL_N,
                name:            "PAL-N",
                Fsc:             35468950,
                swidth:          768,
                sheight:         576,
                totalwidth:      1135,
                adelay:          0x7f,
                bdelay:          0x72,
                iform:           (GENERIC_IFORM_PAL_N|GENERIC_IFORM_XT1),
                scaledtwidth:    944,
                hdelayx1:        186,
                hactivex1:       922,
                vdelay:          0x20,
                vbipack:         144,
                sram:            -1,
        },{
                v4l2_id:         V4L2_STD_NTSC_M_JP,
                name:            "NTSC-JP",
                Fsc:             28636363,
                swidth:          720,
                sheight:         480,
                totalwidth:      910,
                adelay:          0x68,
                bdelay:          0x5d,
                iform:           (GENERIC_IFORM_NTSC_J|GENERIC_IFORM_XT0),
                scaledtwidth:    780,
                hdelayx1:        135,
                hactivex1:       754,
                vdelay:          0x16,
                vbipack:         144,
                sram:            -1,
        }
};
const unsigned int GENERIC_TVNORMS = ARRAY_SIZE(generic_tvnorms);

/* ---------------------------------------------------------------------- */

int set_tvnorm(GENERIC_CARD *card, unsigned int norm)
{
  const struct generic_tvnorm *tvnorm;

  if (norm < 0 || norm >= GENERIC_TVNORMS)
    return -EINVAL;

  card->tvnorm = norm;
  tvnorm = &generic_tvnorms[norm];

  BTWRITE(card,BT829_ADELAY, tvnorm->adelay);
  BTWRITE(card,BT829_BDELAY, tvnorm->bdelay);

  return -EINVAL;
}

/* this is called when someone opens the video4linux device associated with this module (eg cat /dev/video0). All we want to do is identify what card is associated with the device they opened and store that information in the file struct*/
int generic_open(struct inode *inode, struct file *file)
{
  GENERIC_CARD *card=NULL;
  GENERIC_FH *fh;
  enum v4l2_buf_type type = 0;
  unsigned int i;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
  int minor = minor(inode->i_rdev);

  MOD_INC_USE_COUNT;
#else
  int minor = iminor(inode);
#endif


  /* find the card we just opened */
  for (i = 0; i < num_cards_detected; i++) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
    if (cards[i].video_dev.minor == minor) {
#else
    if (cards[i].video_dev->minor == minor) {
#endif
      card = &cards[i];
      type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      break;
    }
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
    if (cards[i].vbi_dev.minor == minor) {
#else
    if (cards[i].vbi_dev->minor == minor) {
#endif
      card = &cards[i];
      type = V4L2_BUF_TYPE_VBI_CAPTURE;
      printk (KERN_INFO "VBI open called\n");
      break;
    }
  }
  if (card == NULL){
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
    MOD_DEC_USE_COUNT;
#endif
    return -ENODEV;
  }

  dprintk(2, "card(%d) open called\n", card->cardnum);

  /* allocate some memory for our filehandle_information */
  fh = kmalloc(sizeof(*fh),GFP_KERNEL);
  if (fh == NULL){
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
    MOD_DEC_USE_COUNT;
#endif
    return -ENOMEM;
  }

  init_MUTEX(&fh->cap.lock);
  init_MUTEX(&fh->vbi.lock);
  /* save a pointer to fh in the file struct */
  file->private_data = fh;

/* *fh = card->init; */
  fh->type = type;
  fh->card = card;
  fh->resources = 0;

  return 0;
}

/* enable video blank interval capture (closed captioning)*/
void generic_enable_vbi(GENERIC_CARD *card)
{
  down_interruptible(&card->lock);
//  down_interruptible(&card->lockvbi);
  card->status |= STATUS_VBI_CAPTURE;
//  up(&card->lockvbi);
  up(&card->lock);
}

/* enable video blank interval capture (closed captioning)*/
void generic_disable_vbi(GENERIC_CARD *card)
{
  down_interruptible(&card->lock);
  //down_interruptible(&card->lockvbi);
  card->status &= ~STATUS_VBI_CAPTURE;
  //up(&card->lockvbi);
  up(&card->lock);
}

/* enable video capture to capture buffers */
void generic_enable_capture(GENERIC_CARD *card) 
{
  int temp;

  /* lock the card */
  down_interruptible(&card->lock);

  /* clear any existing buffers */
  card->status &= ~(STATUS_DMABUF_READY | STATUS_BUF0_READY | STATUS_BUF1_READY);
  /* reset field count */
  card->field_count = 0;

  if (card->driver_data & RAGE128CHIP) {
    rage128_enable_capture(card);
  } else {
    mach64_enable_capture(card);
  }

  /* set CAGC and CKILL (chroma agc and low color detector) */
  temp = BTREAD(card,BT829_SCLOOP);

  //temp |= BT829_CKILL | BT829_CAGC | BT829_PEAK;
  temp |= BT829_CKILL | BT829_CAGC;
  /* When decoding SECAM video this filter must be enabled */
  if (card->tvnorm == 2){
printk (KERN_INFO "Enabling HFILT for SECAM video\n");
    temp |= BT829_HFILT;
  }
  BTWRITE(card,BT829_SCLOOP,temp);

/* break this into flags or remove since it is default? */
  BTWRITE(card,BT829_WC_UP,0xCF);
  BTWRITE(card,BT829_WC_DN,0x7F);

//  if (card->driver_data & MACH64CHIP){
//    BTWRITE(card,BT829_P_IO,0x0);
//  }

  /* free the card */
  up(&card->lock);
}

/* this is called when the process releases access to the video4linux
device that it had opened */
int generic_release(struct inode *inode, struct file *file)
{
  GENERIC_FH *fh = (GENERIC_FH *) file->private_data;
  GENERIC_CARD *card = fh->card;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
MOD_DEC_USE_COUNT;
#endif
  switch (fh->type) {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
      /* stop video capture and anything else here */
      if (fh->resources & STATUS_CAPTURING){
        generic_disable_capture(card);
      }
dprintk (1, "Release video called\n");
      break;
    case V4L2_BUF_TYPE_VBI_CAPTURE:
      /* stop vbi capture here */
      if (fh->resources & STATUS_VBI_CAPTURE){
        generic_disable_vbi(card);
      }
dprintk (1, "Release vbi called\n");
      break;
    default:
      BUG();
  }


  file->private_data = NULL;
  kfree(fh);

  dprintk(2,"card(%d) Release called\n",card->cardnum);
  return 0;
}

int generic_ioctl(struct inode *inode, struct file *file,
                      unsigned int cmd, unsigned long arg)
{
  return video_usercopy(inode, file, cmd, arg, generic_do_ioctl);
}

void generic_blockhandler(unsigned long param)
{
   wake_up_interruptible(&generic_wait);
   return;
}

ssize_t generic_read(struct file *file, char *data,
                         size_t count, loff_t *ppos) 
{
  GENERIC_FH *fh = file->private_data;
  GENERIC_CARD *card = fh->card;
  int retval = 0;

// check for errors and re init? (wonder what the reset BT829_SRESET does)

  switch (fh->type) {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
  /* if already capturing and this isnt the fh that is doing it return busy */ 
      if (!(fh->resources & STATUS_CAPTURING)){
        if (card->status & STATUS_CAPTURING){
	   return -EBUSY;
	} else {
	   fh->resources = STATUS_CAPTURING;
           generic_enable_capture(card);
	}
      }
  grab_frame(card);

  switch (card->frame){
    case 0: 
      if (copy_to_user(data,card->framebuffer1,card->width*card->height*2)){
         return -EFAULT;
      } 
      break;
    case 1: 
      if (copy_to_user(data,card->framebuffer2,card->width*card->height*2)){
         return -EFAULT;
      } 
      break;
  }

      retval = card->width*card->height*2; /* should be width*height*2 */
      break;
    case V4L2_BUF_TYPE_VBI_CAPTURE:
  /* if already capturing and this isnt the fh that is doing it return busy */
      if (!(fh->resources & STATUS_VBI_CAPTURE)){
        if (card->status & STATUS_VBI_CAPTURE){
	   return -EBUSY;
	} else {
	   fh->resources = STATUS_VBI_CAPTURE;
	   generic_enable_vbi(card);
	}
      }

      if (card->status & STATUS_VBI_CAPTURE){
        /* grab_vbi returns the frame, 0 for odd 1 for even */
        if (grab_vbi(card) == 0){
          if (copy_to_user(data,card->vbidatabuffer, 32768)){
            return -EFAULT;
	  }
        } else {
          if (copy_to_user(data+32768,card->vbidatabuffer, 32768)){
            return -EFAULT;
	  }
        }
         /* grab two so we get even and odd  */
        if (grab_vbi(card) == 0){
          if (copy_to_user(data,card->vbidatabuffer, 32768)){
            return -EFAULT;
	  }
        } else {
          if (copy_to_user(data+32768,card->vbidatabuffer, 32768)){
            return -EFAULT;
	  }
        }
      }
      retval = 65536;
      break;
    default:
      BUG();
  }
  dprintk(2,"card(%d) Read called\n",card->cardnum);
  return retval;
}

/* memory handler functions */
static struct vm_operations_struct generic_mmap_vm_ops = {
  open:    generic_mmap_vopen, /* mmap-open */
  close:  generic_mmap_vclose,/* mmap-close */
//  nopage: generic_mmap_vmmap, /* no-page fault handler */
};

/* open handler for vm area */
void generic_mmap_vopen(struct vm_area_struct *vma)
{
        /* needed to prevent the unloading of the module while
           somebody still has memory mapped */ 
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	MOD_INC_USE_COUNT;
#endif
}

/* close handler form vm area */
void generic_mmap_vclose(struct vm_area_struct *vma)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	MOD_DEC_USE_COUNT;
#endif
}

int generic_mmap(struct file *file, struct vm_area_struct *vma)
{
  GENERIC_FH *fh = file->private_data;
  GENERIC_CARD *card = fh->card;
  int i,j;

unsigned long start = vma->vm_start;
unsigned long size  = vma->vm_end - vma->vm_start;
unsigned long page,pos;

  down_interruptible(&card->lock);
// should switch this to the page fault method?
// see if one is faster/better than the other

  //do the v4l2 mmap (several chunks)
  if (card->v4lmmaptype){
      size = (vma->vm_end - vma->vm_start);
      j = 1;
  } else { //do v4l mmap (all in one chunk)
      j = VIDEO_MAX_FRAMES;
  }
  for (i=0;i<j;i++){
    if (card->v4lmmaptype){
      if (card->frame == 0){
      //should be an array so I can change max frames
        pos = (unsigned long)card->framebuffer1;
      } else {
        pos = (unsigned long)card->framebuffer2;
      }
    } else { //do v4l mmap (all in one chunk)
      size = (vma->vm_end - vma->vm_start) / 2;
      if (i == 0){
        pos = (unsigned long)card->framebuffer1;
      } else {
        pos = (unsigned long)card->framebuffer2;
      }
    }
    while (size > 0){
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
      page = kvirt_to_pa(pos);
      if (remap_page_range(start,page,PAGE_SIZE, PAGE_SHARED)) {
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10)
      page = kvirt_to_pa(pos);
      if (remap_page_range(vma,start,page,PAGE_SIZE, PAGE_SHARED)) {
#else
      page = page_to_pfn(vmalloc_to_page((void *)pos));
      if (remap_pfn_range(vma,start,page,PAGE_SIZE, PAGE_SHARED)) {	      
#endif
#endif
        printk(KERN_INFO "remap page range failed");
        up(&card->lock);
        return -EAGAIN;
      }
      start += PAGE_SIZE;
      pos += PAGE_SIZE;
      if (size > PAGE_SIZE){
        size -= PAGE_SIZE;
      } else {
        size = 0;
      }
    }
  }
  up(&card->lock);

  vma->vm_ops = &generic_mmap_vm_ops;
  generic_mmap_vopen(vma); 

  dprintk(2,"card(%d) Mmap called\n",card->cardnum);
  return 0;
}

unsigned int generic_poll(struct file *file, poll_table *wait)
{
  GENERIC_FH *fh = file->private_data;
  GENERIC_CARD *card = fh->card;

  dprintk(3,"card(%d) Poll called\n",card->cardnum);

  poll_wait(file,&generic_wait,wait);
  if (card->status & STATUS_POLL_READY){
    card->status &= ~STATUS_POLL_READY;

    /* if this is a poll for vbi make sure a vbi frame is ready */
    if (fh->type == V4L2_BUF_TYPE_VBI_CAPTURE)  {
      if (disabledma || card->status & STATUS_VBI_DMABUF_READY){
        return POLLIN|POLLRDNORM;
      }
      return 0;
    }
  }
  /* better to tell it there is something there 
  otherwise xawtv drops to many frames? 15fps instead of 30 */
  return POLLIN|POLLRDNORM;
//  return 0;
}

ssize_t generic_write(struct file *file, const char *buf,
        size_t count, loff_t *ppos)
{
  GENERIC_FH *fh = file->private_data;
  GENERIC_CARD *card = fh->card;

  dprintk(1,"card(%d) Write called (unsupported)\n",card->cardnum);
  return 0;
}

#define DUMP_BT_REG(REG) \
  len += sprintf (buffer+len,"%-20s (0x%02X) = 0x%02X\n",#REG,REG, BTREAD(card,REG))

#define DUMP_BOARD_REG(REG) \
  len += sprintf (buffer+len,"%-20s = 0x%02X\n",#REG,REG)

/* should do one for each card? is it possible to do generic func? */
int proc_read(char *buffer, char **start, off_t offset, int size,
                        int *eof, void *data)
{
//  char buffer[8000];
  GENERIC_CARD *card = data;
  int len=0;

//  down_interruptible(&card->lockvbi);
//  down_interruptible(&card->lockcap);
//  down_interruptible(&card->lock);

  DUMP_BT_REG(BT829_DSTATUS);
  DUMP_BT_REG(BT829_IFORM);
  DUMP_BT_REG(BT829_TDEC);
  DUMP_BT_REG(BT829_CROP);
  DUMP_BT_REG(BT829_VDELAY_LO);
  DUMP_BT_REG(BT829_VACTIVE_LO);
  DUMP_BT_REG(BT829_HDELAY_LO);
  DUMP_BT_REG(BT829_HACTIVE_LO);
  DUMP_BT_REG(BT829_HSCALE_HI);
  DUMP_BT_REG(BT829_HSCALE_LO);
  DUMP_BT_REG(BT829_BRIGHT);
  DUMP_BT_REG(BT829_CONTROL);
  DUMP_BT_REG(BT829_CONTRAST_LO);
  DUMP_BT_REG(BT829_SAT_U_LO);
  DUMP_BT_REG(BT829_SAT_V_LO);
  DUMP_BT_REG(BT829_HUE);
  DUMP_BT_REG(BT829_SCLOOP);
  DUMP_BT_REG(BT829_WC_UP);
  DUMP_BT_REG(BT829_OFORM);
  DUMP_BT_REG(BT829_VSCALE_HI);
  DUMP_BT_REG(BT829_VSCALE_LO);
  DUMP_BT_REG(BT829_TEST);
  DUMP_BT_REG(BT829_VPOLE);
  DUMP_BT_REG(BT829_IDCODE);
  DUMP_BT_REG(BT829_ADELAY);
  DUMP_BT_REG(BT829_BDELAY);
  DUMP_BT_REG(BT829_ADC);
  DUMP_BT_REG(BT829_VTC);
  DUMP_BT_REG(BT829_CC_STATUS);
  DUMP_BT_REG(BT829_CC_DATA);
  DUMP_BT_REG(BT829_WC_DN);
  DUMP_BT_REG(BT829_P_IO);
  if (card->driver_data & MACH64CHIP){
    DUMP_BOARD_REG(MACH64_CAPTURE_BUF0_OFFSET);
    DUMP_BOARD_REG(MACH64_CAPTURE_BUF1_OFFSET);
    DUMP_BOARD_REG(MACH64_ONESHOT_BUF_OFFSET);
  } else {
    DUMP_BOARD_REG(R128_CAP0_BUF0_OFFSET);
    DUMP_BOARD_REG(R128_CAP0_BUF1_OFFSET);
    DUMP_BOARD_REG(R128_CAP0_ONESHOT_BUF_OFFSET);
    DUMP_BOARD_REG(R128_CAP0_CONFIG);
  }

//  up(&card->lock);
//  up(&card->lockcap);
//  up(&card->lockvbi);

  len += sprintf (buffer+len,"Field Count: %d\n",card->field_count);


  if (len <= offset+size) *eof = 1;
  *start = buffer + offset;
  len -= offset;
  if (len > size) len = size;
  if (len < 0) len = 0;
  return len;
}

int proc_write(struct file *file, const char *buffer,
                      unsigned long count, void *data)
{
  GENERIC_CARD *card = data;
  char inbuf[80];

  if (count > 79){
    count = 79;
  }
  if (copy_from_user(inbuf, buffer, count)){
    return -EFAULT;
  }
  inbuf[count] = '\0';

  printk(KERN_INFO "card(%d) Write called you sent [%s]\n",card->cardnum, inbuf);
/*  if (debug) {
    debug = 0;
  } else {
    debug = 3;
  } */

  return count;
}

static int get_control(GENERIC_CARD *card, struct v4l2_control *c)
{
//  struct video_audio va;
  int i;

  for (i = 0; i < GENERIC_CTLS; i++)
    if (generic_ctls[i].id == c->id)
      break;
  if (i == GENERIC_CTLS)
    return -EINVAL;

  c->value = 0;
  switch (c->id) {
    case V4L2_CID_BRIGHTNESS:
    c->value = card->bright;
      break;
    case V4L2_CID_HUE:
      c->value = card->hue;
      break;
    case V4L2_CID_CONTRAST:
        c->value = card->contrast;
      break;
    case V4L2_CID_SATURATION:
        c->value = card->saturation;
      break;
    case V4L2_CID_AUDIO_MUTE:
      c->value = card->mute;
      break;
    case V4L2_CID_AUDIO_VOLUME:
dprintk(1,"card(%d) Get volume (not done yet)\n",card->cardnum);
      break;
    case V4L2_CID_AUDIO_BALANCE:
dprintk(1,"card(%d) Get balance (not done yet)\n",card->cardnum);
      break;
    case V4L2_CID_AUDIO_BASS:
dprintk(1,"card(%d) Get bass (not done yet)\n",card->cardnum);
      break;
    case V4L2_CID_AUDIO_TREBLE:
dprintk(1,"card(%d) Get treble (not done yet)\n",card->cardnum);
      break;
    default:
      return -EINVAL;
  }
  return 0;
}

static int set_control(GENERIC_CARD *card, struct v4l2_control *c)
{
//  struct video_audio va;
  int i;

  for (i = 0; i < GENERIC_CTLS; i++)
    if (generic_ctls[i].id == c->id)
      break;
  if (i == GENERIC_CTLS){
    dprintk(1,"card(%d) VIDIOC_S_CTRL c->id %d\n",card->cardnum,c->id);
    return -EINVAL;
  }

  switch (c->id) {
    case V4L2_CID_BRIGHTNESS:
      set_brightness(card,c->value);
      break;
    case V4L2_CID_HUE:
      set_hue(card,c->value);
      break;
    case V4L2_CID_CONTRAST:
      set_contrast(card,c->value);
      break;
    case V4L2_CID_SATURATION:
      set_saturation(card,c->value);
      break;
    case V4L2_CID_AUDIO_MUTE:
      set_mute(card,c->value);
      break;
    case V4L2_CID_AUDIO_VOLUME:
dprintk(1,"card(%d) Set volume to %d\n",card->cardnum,c->value);
      break;
    case V4L2_CID_AUDIO_BALANCE:
dprintk(1,"card(%d) Set balance to %d\n",card->cardnum,c->value);
      break;
    case V4L2_CID_AUDIO_BASS:
dprintk(1,"card(%d) Set bass to %d\n",card->cardnum,c->value);
      break;
    case V4L2_CID_AUDIO_TREBLE:
dprintk(1,"card(%d) Set treble to %d\n",card->cardnum,c->value);
      break;
    default:
      return -EINVAL;
  }
  return 0;
}

/*static const struct generic_format*
format_by_fourcc(int fourcc)
{
  unsigned int i;

  for (i = 0; i < GENERIC_FORMATS; i++) {
    if (generic_formats[i].fourcc == -1){
      continue;
    }
    if (generic_formats[i].fourcc == fourcc){
      return generic_formats+i;
    }
  }
  return NULL;
} */

void generic_vbi_setlines(GENERIC_FH *fh, GENERIC_CARD *card, int lines)
{
  int vdelay;

  if (lines < 1)
    lines = 1;
  if (lines > VBI_MAXLINES)
    lines = VBI_MAXLINES;
  fh->lines = lines;

  vdelay = BTREAD(card,BT829_VDELAY_LO);
  if (vdelay < lines*2) {
    vdelay = lines*2;
    BTWRITE(card,BT829_VDELAY_LO,vdelay);
  }
}

void generic_vbi_get_fmt(GENERIC_FH *fh, GENERIC_CARD *card, struct v4l2_format *f)
{
        memset(f,0,sizeof(*f));
        f->type = V4L2_BUF_TYPE_VBI_CAPTURE;
        f->fmt.vbi.sampling_rate    = generic_tvnorms[card->tvnorm].Fsc;
        f->fmt.vbi.samples_per_line = 2048;
        f->fmt.vbi.sample_format    = V4L2_PIX_FMT_GREY;
        f->fmt.vbi.offset           = 244;
        f->fmt.vbi.count[0]         = fh->lines;
        f->fmt.vbi.count[1]         = fh->lines;
        f->fmt.vbi.flags            = 0;
        switch (fh->card->tvnorm) {
        case 1: /* NTSC */
                f->fmt.vbi.start[0] = 10;
                f->fmt.vbi.start[1] = 273;
                break;
        case 0: /* PAL */
        case 2: /* SECAM */
        default:
                f->fmt.vbi.start[0] = 7;
                f->fmt.vbi.start[1] = 319;
        }
}

void generic_vbi_try_fmt(GENERIC_FH *fh, GENERIC_CARD *card, struct v4l2_format *f)
{
        u32 start0,start1,count;

        f->type = V4L2_BUF_TYPE_VBI_CAPTURE;
        f->fmt.vbi.sampling_rate    = generic_tvnorms[card->tvnorm].Fsc;
        f->fmt.vbi.samples_per_line = 2048;
        f->fmt.vbi.sample_format    = V4L2_PIX_FMT_GREY;
        f->fmt.vbi.offset           = 244;
        f->fmt.vbi.flags            = 0;
        switch (fh->card->tvnorm) {
        case 1: /* NTSC */
                start0 = 10;
                start1 = 273;
                break;
        case 0: /* PAL */
        case 2: /* SECAM */
        default:
                start0 = 7;
                start1 = 319;
        }

        count = max(f->fmt.vbi.count[0],f->fmt.vbi.count[1]);
        if (f->fmt.vbi.start[0] > start0)
                count += f->fmt.vbi.start[0] - start0;
        if (count > VBI_MAXLINES)
                count = VBI_MAXLINES;

        f->fmt.vbi.start[0] = start0;
        f->fmt.vbi.start[1] = start1;
        f->fmt.vbi.count[0] = count;
        f->fmt.vbi.count[1] = count;
}

static int generic_g_fmt(GENERIC_FH *fh, struct v4l2_format *f)
{
	GENERIC_CARD *card = fh->card;
        switch (f->type) {
        case V4L2_BUF_TYPE_VIDEO_CAPTURE:
                memset(&f->fmt.pix,0,sizeof(struct v4l2_pix_format));
                f->fmt.pix.width        = card->width;
                f->fmt.pix.height       = card->height;
                f->fmt.pix.field        = fh->cap.field;
                f->fmt.pix.pixelformat  = V4L2_PIX_FMT_YUYV;
/* 16 is for capture depth (i think capture depth is always 16 bit) */
                f->fmt.pix.bytesperline =
                        (f->fmt.pix.width * 16) >> 3;
                f->fmt.pix.sizeimage =
                        f->fmt.pix.height * f->fmt.pix.bytesperline;
                return 0;
        case V4L2_BUF_TYPE_VBI_CAPTURE:
                generic_vbi_get_fmt(fh,card,f);
                return 0;
        default:
                return -EINVAL;
        }
}

/* disable video capture to capture buffers */
void generic_disable_capture(GENERIC_CARD *card) 
{
  int grabbingvbi;

  /* get lock on vbi and capture so we know they are finished */
  down_interruptible(&card->lockvbi);
  down_interruptible(&card->lockcap);
  down_interruptible(&card->lock);

  /* check if we are capturing vbi */
  grabbingvbi = (card->status & STATUS_VBI_CAPTURE);

  if (card->driver_data & RAGE128CHIP) {
    rage128_disable_capture(card);
  } else {
    mach64_disable_capture(card);
  }

  /* if vbi was on turn it on again */
  card->status = grabbingvbi;

  up(&card->lock);
  up(&card->lockcap);
  up(&card->lockvbi);
}

void set_input(GENERIC_CARD *card, unsigned int input)
{
  card->mux = input;
  bt829_setmux(card);
  board_setaudio(card);
  board_setbyte(card);
}

/* Fill in the fields of a v4l2_standard structure according to the
   'id' and 'transmission' parameters.  Returns negative on error.  */
int v4l2_video_std_const(struct v4l2_standard *vs,
                             int id, char *name)
{
        memset(vs, 0, sizeof(struct v4l2_standard));

        vs->id = id;
        if (id & (V4L2_STD_NTSC | V4L2_STD_PAL_M)) {
                vs->frameperiod.numerator = 1001;
                vs->frameperiod.denominator = 30000;
                vs->framelines = 525;
        } else {
                vs->frameperiod.numerator = 1;
                vs->frameperiod.denominator = 25;
                vs->framelines = 625;
        }
        strncpy(vs->name,name,sizeof(vs->name));
        return 0;
}


//grab and dma a frame to card->framebuffer[card->frame]
void grab_frame(GENERIC_CARD *card)
{
  DMA_BM_TABLE *ptr = NULL;
  int stall = 0;

  /* lock the capture semaphore *use this to make sure everything is finished
  before disabling interrupts when disable_capture is called */
  down_interruptible(&card->lockcap);

  /* make sure we are still capturing, if not then return */
  if (!(card->status & STATUS_CAPTURING)){
    up(&card->lockcap);
    return;
  }

  // now wait for the video data (one of the frames)
  wait_event_interruptible(generic_wait, (card->status & STATUS_BUF0_READY) || (card->status & STATUS_BUF1_READY) || (stall++ > 10));
  if (stall > 10){
    printk (KERN_INFO "STALLED!!!!! aborting framegrab\n");
    up(&card->lockcap);
    return;
  } 

  // there are two dma tables, one for buf0 one for buf1
  // they are rebuilt when the capture size changes
  if (card->status & STATUS_BUF0_READY){
    card->status &= ~STATUS_BUF0_READY;
    switch (card->frame){
      case 0: ptr = card->dma_table_buf0;
              break;
      case 1: ptr = card->dma_table_buf2;
              break;
    }
  } else {
    card->status &= ~STATUS_BUF1_READY;
    switch (card->frame){
      case 0: ptr = card->dma_table_buf1;
              break;
      case 1: ptr = card->dma_table_buf3;
              break;
    }
  } 

  if (!disabledma) {
    down_interruptible(&card->lock);
    // pass the dma_table to the card
    if (card->driver_data & RAGE128CHIP) {
      R128_BM_VIDCAP_BUF0 = virt_to_bus(ptr) | R128_SYSTEM_TRIGGER_VIDEO_TO_SYSTEM; 
    } else if (card->driver_data & MACH64CHIP){
      MACH64_BM_SYSTEM_TABLE = virt_to_bus(ptr) | FRAME_BUFFER_TO_SYSTEM;
    }
  
    //wait for dma to finish
    /* this should transfer the vbi information as well */
    stall=0;
    wait_event_interruptible(generic_wait, (card->status & STATUS_DMABUF_READY) 
    	|| (stall++ > 10));
    card->status &= ~STATUS_DMABUF_READY;

    // release the lock so we can transfer data again
    up(&card->lock);
  }
  up(&card->lockcap);
}

//grab and dma the vbi to card->vbibuffer
/* we only grab vbi from one frame so read will have to 
call this twice */
int grab_vbi(GENERIC_CARD *card)
{
  int whichfield;
  /* lockvbi is used to track when vbi capture is in use
  so we can wait till its done before disabling capture */
  down_interruptible(&card->lockvbi);

  // if not capturing then return
  if (!(card->status & STATUS_VBI_CAPTURE)){
    up(&card->lockvbi);
    return -1;
  }

  wait_event_interruptible(generic_wait, (card->status & STATUS_VBI_READY));
  card->status &= ~STATUS_VBI_READY;

  down_interruptible(&card->lock);
  // odd vbi is 0 even is 1;
  whichfield = BTREAD(card,BT829_DSTATUS) & BT829_DSTATUS_FIELD;
  if (!disabledma) {
    if (card->driver_data & RAGE128CHIP) {
      R128_BM_VIDCAP_BUF0 = virt_to_bus(card->dma_table_vbi) | 
	R128_SYSTEM_TRIGGER_SYSTEM_TO_VIDEO; 
    } else if (card->driver_data & MACH64CHIP){
      MACH64_BM_SYSTEM_TABLE = virt_to_bus(card->dma_table_vbi) | 
        FRAME_BUFFER_TO_SYSTEM;
    }

    wait_event_interruptible(generic_wait, (card->status & STATUS_DMABUF_READY));
    card->status &= ~STATUS_DMABUF_READY;
  }
  up(&card->lock);
  up(&card->lockvbi);
  return whichfield;
}

/* handle ioctl calls to this video4linux device */
int generic_do_ioctl(struct inode *inode, struct file *file,
                         unsigned int cmd, void *arg)
{
  GENERIC_FH *fh = (GENERIC_FH *) file->private_data;
  GENERIC_CARD *card = fh->card;

  switch (cmd) {
    /* v4l stuff */
    /* Get capabilities */
    case VIDIOCGCAP:
     {
      struct video_capability *cap = arg;
dprintk(2,"card(%d) VIDIOCGCAP called\n",card->cardnum);
//      memset(cap,0,sizeof(*cap));
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
      strncpy(cap->name,card->video_dev.name,32);
#else
      strncpy(cap->name,card->video_dev->name,32);
#endif
      if (fh->type == V4L2_BUF_TYPE_VBI_CAPTURE) {
        cap->type = VID_TYPE_TUNER|VID_TYPE_TELETEXT;
      } else {
     //   cap->type = VID_TYPE_CAPTURE|VID_TYPE_TUNER|VID_TYPE_OVERLAY|VID_TYPE_CLIPPING|VID_TYPE_SCALES;
        cap->type = VID_TYPE_CAPTURE|VID_TYPE_TUNER|VID_TYPE_CLIPPING;

        cap->channels  = generic_tvcards[card->type].video_inputs; 
        cap->audios    = generic_tvcards[card->type].audio_inputs;
        cap->maxwidth  = generic_tvnorms[card->tvnorm].swidth;
        cap->maxheight = generic_tvnorms[card->tvnorm].sheight;
	cap->minwidth  = generic_tvcards[card->type].captmin_x;
	cap->minheight = generic_tvcards[card->type].captmin_y;
      }
     return 0;
     }
    /* Get picture properties */
    case VIDIOCGPICT:
     {
      struct video_picture *pic = arg;
dprintk(2,"card(%d) VIDIOCGPICT called\n",card->cardnum);
      memset(pic,0,sizeof(*pic));
      pic->brightness = card->bright;
      pic->contrast   = card->contrast;
      pic->hue        = card->hue;
      pic->colour     = card->saturation;
      pic->depth      = 16; /* i think capture depth is always 16 bit */
      pic->palette    = VIDEO_PALETTE_YUV422;
      return 0;
     }
    /* Set picture properties */
    case VIDIOCSPICT:
     {
      struct video_picture *pic = arg;

      if (pic->palette != VIDEO_PALETTE_YUV422){
         return -EAGAIN;
      }

      /* here you would set the card with the values supplied */ 
      set_brightness(card,pic->brightness);
      set_contrast(card,pic->contrast);
      set_hue(card,pic->hue);
      set_saturation(card,pic->colour);

      return 0;
     }
   /* Get the video overlay window */
    case VIDIOCGWIN:
     {
      struct video_window *win = arg;
dprintk(2,"card(%d) VIDIOCGWIN called\n",card->cardnum);

      memset(win,0,sizeof(*win));
      win->x      = 0;
      win->y      = 0; 
      win->width  = card->width;
      win->height = card->height;
      return 0;
     }
    /* Set the video overlay window - passes clip list for hardware smarts , chromakey etc */
    case VIDIOCSWIN:
     {
      struct video_window *win = arg;
      u8 framerate;

dprintk(2,"card(%d) VIDIOCSWIN called\n",card->cardnum);

      framerate = win->flags;
      if (framerate >= 1 && framerate <= 60){
        //check if ntsc or pal/secam?
        //card->framerate = framerate;

	//BTWRITE(card,BT829_TDEC,0x00);
	//BTWRITE(card,BT829_TDEC,60-framerate);
printk(KERN_INFO "card(%d) I WOULD set frame rate set to %d but it doesnt seem to work\n",card->cardnum,60-framerate);
       }

      /* should find working sizes and find the closest match */
      if (win->width > generic_tvnorms[card->tvnorm].swidth)
        win->width = generic_tvnorms[card->tvnorm].swidth;
      if (win->width < generic_tvcards[card->type].captmin_x)
        win->width = generic_tvcards[card->type].captmin_x;
      if (win->height > generic_tvnorms[card->tvnorm].sheight)
        win->height = generic_tvnorms[card->tvnorm].sheight;
      if (win->height < generic_tvcards[card->type].captmin_y)
        win->height = generic_tvcards[card->type].captmin_y;

      if (win->width != card->width ||
          win->height != card->height){
        card->width = win->width;
        card->height = win->height;

        if (card->status & STATUS_CAPTURING){
          //if we are already capturing, reenable to make
          //size change take effect
          generic_enable_capture(card);
        }
      }
      return 0;
     }
    /* Get frame buffer */
    case VIDIOCGFBUF: 
     {
      return -EINVAL;
     }
    /* Set frame buffer - root only */
    case VIDIOCSFBUF:
     {
     return -EINVAL;
     }
    /* Start, end capture */
    case VIDIOCCAPTURE:
     {
      return -EINVAL;
     }
    case VIDIOC_OVERLAY:
     {
      return -EINVAL;
     }
    /* Memory map buffer info */
    case VIDIOCGMBUF:
     {
      struct video_mbuf *mbuf = arg;

dprintk(2,"card(%d) VIDIOCGMBUF called %dx%d\n",card->cardnum,card->width, card->height);

      if (!(fh->resources & STATUS_CAPTURING)){
        if (card->status & STATUS_CAPTURING){
           return -EBUSY;
        } else {
           fh->resources = STATUS_CAPTURING;
        }
      }
      down_interruptible(&fh->cap.lock);
      //hack for mmap to map in one chunk
      card->v4lmmaptype = 0;
      memset(mbuf,0,sizeof(*mbuf));
      mbuf->frames = VIDEO_MAX_FRAMES;
      //card->fbmallocsize is always max buffer size (eg 640x480x2)
      mbuf->size   = card->fbmallocsize * mbuf->frames;
      mbuf->offsets[0] = 0;
      mbuf->offsets[1] = card->fbmallocsize;
      up(&fh->cap.lock);

      return 0;
     }
    /* Grab frames */
    case VIDIOCMCAPTURE:
     {
      struct video_mmap *vm = arg;

      if (vm->frame < 0 || vm->frame >= VIDEO_MAX_FRAMES){
         return -EINVAL;
      }
      down_interruptible(&fh->cap.lock);
      card->frame = vm->frame;

      // set format width height here and start capture
      //we only support yuyv packed
if (vm->format != VIDEO_PALETTE_YUV422){
up(&fh->cap.lock);
return -EINVAL;
}

      vm->format = VIDEO_PALETTE_YUV422;
      //check if diff from card if not ignore
  if (card->width != vm->width || card->height != vm->height){
    card->width = vm->width;
    card->height = vm->height;
    if (card->status & STATUS_CAPTURING){
dprintk(2,"card(%d) VIDIOCMCAPTURE called %dx%d\n",card->cardnum, vm->width, vm->height);
      generic_enable_capture(card);
    }
  }

  //set a value in card to make multiple buffers oh, pass the wanted frame value that would work
  if (!(card->status & STATUS_CAPTURING)){
    generic_enable_capture(card);
  }

  grab_frame(card);

      up(&fh->cap.lock);
      return 0;
     }
    /* Sync with mmap grabbing */
    case VIDIOCSYNC:
     {
      int *frame = arg;
      /* should put max frame in there incase i alloc more than one? */
      if (*frame < 0 || *frame >= VIDEO_MAX_FRAMES){
        return -EINVAL;
      }
     //already captured above, hmm whats this sync thing for?
     // should i be waiting here instead?
      //capture frame and transfer with dma and return
      return 0;
     }
    /* Get VBI information */
    case VIDIOCGVBIFMT:
     {
      struct vbi_format *fmt = (void *) arg;
      struct v4l2_format fmt2;

/* check if we are in capture mode and switch to vbi mode?? */

      generic_vbi_get_fmt(fh,card, &fmt2);

      /* no idea what these numbers are :) 
      i will just try to make it work with ntsc for now */
      memset(fmt,0,sizeof(*fmt));
      fmt->sampling_rate = fmt2.fmt.vbi.sampling_rate;
      fmt->samples_per_line = fmt2.fmt.vbi.samples_per_line;
      fmt->sample_format    = VIDEO_PALETTE_RAW;
      fmt->start[0] = fmt2.fmt.vbi.start[0];
      fmt->count[0] = fmt2.fmt.vbi.count[0]; 
      fmt->start[1] = fmt2.fmt.vbi.start[1];
      fmt->count[1] = fmt2.fmt.vbi.count[1]; 
//      if (fmt2.fmt.vbi.flags & VBI_UNSYNC)
//        fmt->flags   |= V4L2_VBI_UNSYNC;
//      if (fmt2.fmt.vbi.flags & VBI_INTERLACED)
//        fmt->flags   |= V4L2_VBI_INTERLACED;

dprintk(1,"card(%d) VIDIOCGVBIFMT called \n",card->cardnum);
      return 0;
     }

    /* Get tuner current frequency */
    case VIDIOCGFREQ:
     {
       unsigned long *freq = arg;
       *freq = card->freq;
dprintk(2,"card(%d) VIDIOCGFREQ called %ld\n",card->cardnum,card->freq);
     return 0;
    }
    /* Set tuner */
    case VIDIOCSFREQ:
    {
      unsigned long *freq = arg;
dprintk(2,"card(%d) VIDIOCSFREQ called %ld\n",card->cardnum, *freq);
      down_interruptible(&card->lock);
      card->freq=*freq;
      /* actually change the channel here */
      fi12xx_tune(card);
      up(&card->lock);
      return 0;
    }
    /* Get tuner abilities */
    case VIDIOCGTUNER:
    {
     struct video_tuner *v = arg;

//     if (v->tuner) /* Only tuner 0 */
//             return -EINVAL;
     strcpy(v->name, "tuner");
     v->rangelow  = 0;
     v->rangehigh = 0x7FFFFFFF;
     v->flags     = VIDEO_TUNER_PAL|VIDEO_TUNER_NTSC|VIDEO_TUNER_SECAM;
     switch (card->tvnorm){
       case 0: 
       case 3: 
       case 4: 
       case 5: 
		v->mode = VIDEO_TUNER_PAL;
		break;
       case 1: 
       case 6: 
		v->mode = VIDEO_TUNER_NTSC;
		break;
       case 2: 
		v->mode = VIDEO_TUNER_SECAM;
		break;
       default:
		v->mode = VIDEO_TUNER_NTSC;
     }

     v->signal = (BTREAD(card,BT829_DSTATUS) & BT829_DSTATUS_HLOC) ? 0xFFFF : 0;

     return 0;
    }
    /* Tune the tuner for the current channel */
    case VIDIOCSTUNER:
dprintk(2,"card(%d) VIDIOCSTUNER called (not done)\n",card->cardnum);
     return 0;
    /* Get channel info (sources) */
    case VIDIOCGCHAN:
        {
                struct video_channel *v = arg;
                unsigned int channel = v->channel;
dprintk(2,"card(%d) VIDIOCGCHAN called\n",card->cardnum);

                if (channel >= generic_tvcards[card->type].video_inputs)
                        return -EINVAL;
                v->tuners=0;
                v->flags = VIDEO_VC_AUDIO;
                v->type = VIDEO_TYPE_CAMERA;
                v->norm = card->tvnorm;
                if (channel == generic_tvcards[card->type].tuner)  {
                        strcpy(v->name,"tuner");
                        v->flags|=VIDEO_VC_TUNER;
                        v->type=VIDEO_TYPE_TV;
                        v->tuners=1;
                } else if (channel == generic_tvcards[card->type].svhs) {
                        strcpy(v->name,"svideo");
                } else {
                        sprintf(v->name,"composite");
                }
                return 0;
        }
    /* Set video input etc */
    case VIDIOCSCHAN:
     {
     struct video_channel *v = arg;
     unsigned int channel = v->channel;
dprintk(2,"card(%d) VIDIOCSCHAN called\n",card->cardnum);

     if (channel >= generic_tvcards[card->type].video_inputs)
             return -EINVAL;
     if (v->norm >= GENERIC_TVNORMS)
             return -EINVAL;

//     if (channel == card->mux && v->norm == card->tvnorm) {
        /* nothing to do */
//        return 0;
//     }

     down_interruptible(&card->lock);
     set_tvnorm(card, v->norm);
     set_input(card,v->channel+1);
     up(&card->lock);
     return 0;
     }
    /* Get audio info */
    case VIDIOCGAUDIO:
       {
                struct video_audio *v = arg;
dprintk(2,"card(%d) VIDIOCGAUDIO called\n",card->cardnum);

                memset(v,0,sizeof(*v));
                strcpy(v->name,"tuner");
                v->flags = VIDEO_AUDIO_MUTABLE;
                v->mode  = VIDEO_SOUND_STEREO;

                down_interruptible(&card->lock);

                /* card specific hooks ?? */

                up(&card->lock);
                return 0;
        }
    /* Audio source, mute etc */
    case VIDIOCSAUDIO:
    {
      struct video_audio *v = arg;
      unsigned int audio = v->audio;

dprintk(2,"card(%d) VIDIOCSAUDIO called %d flags %d mode %d\n",card->cardnum,audio, v->flags,v->mode);
      if (audio >= generic_tvcards[card->type].audio_inputs){
         return -EINVAL;
      }

      down_interruptible(&card->lock);
      if (v->flags & VIDEO_AUDIO_MUTE){
        set_mute(card,1); //mute volume
      } else {
        set_mute(card,0); //unmute volume
      } 
      /* card specific hooks */

       up(&card->lock);
       return 0;
     }
    /* v4l2 stuff */

    /* Get capabilities */
    case VIDIOC_QUERYCAP:
     {
      struct v4l2_capability *cap = arg;
      if (disablev4l2 == 1)
        return -EINVAL;
dprintk(2,"card(%d) VIDIOC_QUERYCAP called\n",card->cardnum);

      strncpy(cap->driver,"genericv4l",sizeof(cap->driver));
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
      strncpy(cap->card,card->video_dev.name,sizeof(cap->card));
#else
      strncpy(cap->card,card->video_dev->name,sizeof(cap->card));
#endif
      snprintf(cap->bus_info,sizeof(cap->bus_info),"PCI:%s",card->dev->slot_name);
      cap->version = GENERIC_VERSION_CODE;
      cap->capabilities =
              V4L2_CAP_VIDEO_CAPTURE |
	      V4L2_CAP_VBI_CAPTURE |
              //  V4L2_CAP_VIDEO_OVERLAY |
              V4L2_CAP_TUNER |
              V4L2_CAP_STREAMING |
              V4L2_CAP_READWRITE;

      return 0;
     }
    /* description of video4linux formats available from this device */
    case VIDIOC_ENUM_FMT:
     {
      struct v4l2_fmtdesc *f = arg;
      enum v4l2_buf_type type;
      unsigned int i;
      int index;

dprintk(2,"card(%d) VIDIOC_ENUM_FMT called\n",card->cardnum);
      type  = f->type;
      if (type == V4L2_BUF_TYPE_VBI_CAPTURE) {
        /* vbi */
        index = f->index;
        if (0 != index)
          return -EINVAL;
        memset(f,0,sizeof(*f));
        f->index       = index;
        f->type        = type;
        f->pixelformat = V4L2_PIX_FMT_GREY;
        strcpy(f->description,"vbi data");
        return 0;
      }

      /* video capture + overlay */
      index = -1;
      for (i = 0; i < GENERIC_FORMATS; i++) {
        if (generic_formats[i].fourcc != -1)
          index++;
        if ((unsigned int)index == f->index)
          break;
      }
      if (i == GENERIC_FORMATS)
        return -EINVAL;

      switch (f->type) {
        case V4L2_BUF_TYPE_VIDEO_CAPTURE:
          break;
        default:
          return -EINVAL;
      }
      memset(f,0,sizeof(*f));
      f->index       = index;
      f->type        = type;
      f->pixelformat = generic_formats[i].fourcc;
      strncpy(f->description,generic_formats[i].name,sizeof(f->description));
      return 0;
     }
    /* try out the specified video format */
    case VIDIOC_TRY_FMT:
     {
     // struct v4l2_format *f = arg;
     // THIS SHOULD FAIL FOR ALL BUT YUYV (what calls this anyway?)
dprintk(1,"card(%d) VIDIOC_TRY_FMT called MUST FAIL FOR ALL NOT YUYV (not done)\n",card->cardnum);
      return 0;
     }
    /* get current video format */
    case VIDIOC_G_FMT:
     {
      struct v4l2_format *f = arg;
dprintk(2,"card(%d) VIDIOC_G_FMT called\n",card->cardnum);
      return generic_g_fmt(fh,f);
     }
    /* set video format */
    case VIDIOC_S_FMT:
     {
      struct v4l2_format *f = arg;
dprintk(2,"card(%d) VIDIOC_S_FMT called\n",card->cardnum);
      switch (f->type){
        case V4L2_BUF_TYPE_VIDEO_CAPTURE:
          if (f->fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV){
            return -EINVAL;
          }

        if (f->fmt.pix.width > generic_tvnorms[card->tvnorm].swidth)
	  f->fmt.pix.width = generic_tvnorms[card->tvnorm].swidth;
	if (f->fmt.pix.width < generic_tvcards[card->type].captmin_x)
	  f->fmt.pix.width = generic_tvcards[card->type].captmin_x;
	if (f->fmt.pix.height > generic_tvnorms[card->tvnorm].sheight)
	  f->fmt.pix.height = generic_tvnorms[card->tvnorm].sheight;
	if (f->fmt.pix.height < generic_tvcards[card->type].captmin_y)
	  f->fmt.pix.height = generic_tvcards[card->type].captmin_y;

dprintk(2,"card(%d) vcap in VIDIOC_S_FMT x %d y %d fmt %d\n",card->cardnum,f->fmt.pix.width, f->fmt.pix.height, f->fmt.pix.pixelformat);
//Only support this format yuyv
	f->fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;

        if (f->fmt.pix.width != card->width ||
	    f->fmt.pix.height != card->height){
	  card->width = f->fmt.pix.width;
	  card->height = f->fmt.pix.height;

          if (card->status & STATUS_CAPTURING){
            //if we are already capturing, disable and reenable to make
            //size change take effect
            generic_enable_capture(card);
          }
        }
	break;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
dprintk(1,"card(%d) V4L2_BUF_TYPE_VBI_CAPTURE inside VIDIOC_S_FMT\n",card->cardnum);
        generic_vbi_try_fmt(fh,card,f);
        generic_vbi_setlines(fh,card,f->fmt.vbi.count[0]);
        generic_vbi_get_fmt(fh,card,f);
	break;
        default:
          return -EINVAL;
      }

      return 0;
     }
    /* get frame buffer overlay parameters */
    case VIDIOC_G_FBUF:
     {
      struct v4l2_framebuffer *fb = arg;

dprintk(2,"card(%d) VIDIOC_G_FBUF called\n",card->cardnum);
      memset(fb,0,sizeof(*fb));
      fb->base       = card->fbuf.base;
      fb->fmt.width  = card->fbuf.width;
      fb->fmt.height = card->fbuf.height;
      fb->fmt.bytesperline = card->fbuf.bytesperline;
      fb->capability = V4L2_FBUF_CAP_CHROMAKEY;
      fb->fmt.pixelformat  = V4L2_PIX_FMT_YUYV;
      return 0;

     }
    /* set frame buffer overlay parameters */
    case VIDIOC_S_FBUF:
     {
       return -EINVAL;
     }
    /* initiate memory mapping or user pointer i/o */
    case VIDIOC_REQBUFS: 
      {
      struct v4l2_requestbuffers *req = arg;
      if (req->memory != V4L2_MEMORY_MMAP){
         return -EINVAL;
      }
      if (req->type != V4L2_BUF_TYPE_VIDEO_CAPTURE){
         return -EINVAL;
      }
      if (req->count < 1){
         return -EINVAL;
      }
        //hack to set mmap to v4l2 mmap individual buffers
	card->v4lmmaptype = 1;
	init_frame_queue(&card->frame_queue);
        req->count = VIDEO_MAX_FRAMES;
        return 0; 
      }
    /* enumerate controls and menu control items (GIVES THEM NAMES) */
    case VIDIOC_QUERYMENU:
dprintk(1, "card(%d) VIDIOC_QUERYMENU called (not done)\n",card->cardnum);
      return -EINVAL;
    /* Query the status of a buffer */
    case VIDIOC_QUERYBUF:
      {
      struct v4l2_buffer *b = arg;

dprintk(2,"card(%d) VIDIOC_QUERYBUF\n",card->cardnum);
      if (b->memory != V4L2_MEMORY_MMAP){
        return -EINVAL;
      }
      if (b->type != V4L2_BUF_TYPE_VIDEO_CAPTURE){
        return -EINVAL;
      }
      if (b->index < 0 || b->index >= VIDEO_MAX_FRAMES){
        return -EINVAL;
      }
      down_interruptible(&fh->cap.lock);
//wrong, this should be telling if a buffer is ready or not
      //do_gettimeofday(&b->timestamp);
      b->flags = V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_DONE;
      b->bytesused = card->width * card->height * 2;
      b->memory = V4L2_MEMORY_MMAP;

      //b->m.offset = card->fbmallocsize * card->frame; /* should be 0 or card->fbmallocsize depending on frame */
      b->m.offset = 0; /* should be 0 we do one per frame*/
      b->length = card->fbmallocsize;
//      b->index = card->frame;
      card->frame = b->index; // so we know which one to map
      up(&fh->cap.lock);
     
      return 0;
      }
    /* Exchange a buffer with the driver (enqueue)*/
    case VIDIOC_QBUF:
      {
       struct v4l2_buffer *b = arg;
dprintk(3,"card(%d) VIDIOC_QBUF called\n",card->cardnum);
      if (b->type != V4L2_BUF_TYPE_VIDEO_CAPTURE){
        return -EINVAL;
      }
      if (b->index < 0 || b->index >= VIDEO_MAX_FRAMES){
        return -EINVAL;
      }
      add_frame_to_queue(&card->frame_queue,b->index);
      return 0;
      }
    /* Exchange a buffer with the driver (dequeue)*/
    case VIDIOC_DQBUF:
      {
       struct v4l2_buffer *b = arg;

dprintk(3,"card(%d) VIDIOC_DQBUF called\n",card->cardnum);
      if (b->type != V4L2_BUF_TYPE_VIDEO_CAPTURE){
        return -EINVAL;
      }
      down_interruptible(&fh->cap.lock);

      card->frame = remove_frame_from_queue(&card->frame_queue);
      if (card->frame < 0){
        if (file->f_flags & O_NONBLOCK){
          dprintk(1,"VIDIOC_DQBUF should do something here cause we ran out of buffers\n");
          return -EAGAIN;
	}
	/* should wait for a frame to be available here? */
	card->frame = 0;
      }

      grab_frame(card);

      do_gettimeofday(&b->timestamp);

      b->flags = V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_DONE;
      b->bytesused = card->width * card->height * 2;
      b->memory = V4L2_MEMORY_MMAP;
      b->m.offset = 0; 
      b->length = card->fbmallocsize;
      b->index = card->frame;
      //b->field = ?;
      b->sequence = card->field_count;
      up(&fh->cap.lock);

      return 0;
      }
    /* Start streaming I/O */
    case VIDIOC_STREAMON:
 /* if already capturing and this isnt the fh that is doing it return busy */ 
      if (!(fh->resources & STATUS_CAPTURING)){
        if (card->status & STATUS_CAPTURING){
           return -EBUSY;
        } else {
           fh->resources = STATUS_CAPTURING;
           generic_enable_capture(card);
        }
      }
dprintk(2,"card(%d) VIDIOC_STREAMON called\n",card->cardnum);
      return 0;
    /* Stop streaming I/O */
    case VIDIOC_STREAMOFF:
  /* stop video capture and anything else here */
  if (fh->resources & STATUS_CAPTURING){
    fh->resources &= ~STATUS_CAPTURING;
    generic_disable_capture(card);
  }
dprintk(2,"card(%d) VIDIOC_STREAMOFF called\n",card->cardnum);
      return 0;
    /* Enumerate controls and menu control items */
    case VIDIOC_QUERYCTRL:
     {
      struct v4l2_queryctrl *c = arg;
      int i;

dprintk(2,"card(%d) VIDIOC_QUERYCTRL called\n",card->cardnum);
      if (c->id <  V4L2_CID_BASE || c->id >= V4L2_CID_LASTP1)
        return -EINVAL;

      /* check what controls we support */
      for (i = 0; i < GENERIC_CTLS; i++)
        if (generic_ctls[i].id == c->id)
	  break;	 

      if (i == GENERIC_CTLS) {
         *c = no_ctl;
         return 0;
      }
      *c = generic_ctls[i];
      return 0;
     }
    /* Get the value of a control */
    case VIDIOC_G_CTRL:
dprintk(2,"card(%d) VIDIOC_G_CTRL called\n",card->cardnum);
      return get_control(card,arg);
    /* Set the value of a control */
    case VIDIOC_S_CTRL:
dprintk(2,"card(%d) VIDIOC_S_CTRL called\n",card->cardnum);
      return set_control(card,arg);
    /* Get streaming parameters */
    case VIDIOC_G_PARM:
     {
      struct v4l2_streamparm *parm = arg;
      struct v4l2_standard s;
      if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
              return -EINVAL;
      memset(parm,0,sizeof(*parm));
      if (card->tvnorm >= GENERIC_TVNORMS){
        card->tvnorm = 0;
      }
      v4l2_video_std_const(&s, generic_tvnorms[card->tvnorm].v4l2_id,
                               generic_tvnorms[card->tvnorm].name);
      parm->parm.capture.timeperframe = s.frameperiod;
      return 0;
     }
    /* Set streaming parameters */
    case VIDIOC_S_PARM:
     {
      //struct v4l2_streamparm *parm = arg;
dprintk(2,"card(%d) VIDIOC_S_PARM called (not done)\n",card->cardnum);
      return -EINVAL;
     }
    /* information about the video cropping and scaling abilities */
    case VIDIOC_CROPCAP:
     {
dprintk(2,"card(%d) VIDIOC_CROPCAP (not done)\n",card->cardnum);
      return -EINVAL;
     }
    /* get current cropping rectangle */
    case VIDIOC_G_CROP:
     {
dprintk(2,"card(%d) VIDIOC_G_CROP (not done)\n",card->cardnum);
      return -EINVAL;
     }
    /* set current cropping rectangle */
    case VIDIOC_S_CROP:
     {
dprintk(2,"card(%d) VIDIOC_S_CROP (not done)\n",card->cardnum);
      return -EINVAL;
     }

    /* Enumerate supported video standards */
    case VIDIOC_ENUMSTD:
     {
      struct v4l2_standard *e = arg;
      unsigned int index = e->index;

dprintk(2,"card(%d) VIDIOC_ENUMSTD called\n",card->cardnum);
      if (index >= GENERIC_TVNORMS)
        return -EINVAL;
      v4l2_video_std_const(e, generic_tvnorms[e->index].v4l2_id,
             generic_tvnorms[e->index].name);
      e->index = index;
      return 0;
     }
     /* Query the video standard of the current input */
     case VIDIOC_G_STD:
     {
      v4l2_std_id *id = arg;
      *id = generic_tvnorms[card->tvnorm].v4l2_id;
dprintk(2,"card(%d) VIDIOC_G_STD called\n",card->cardnum);
      return 0;
     }
     /* Select the video standard of the current input */
     case VIDIOC_S_STD:
     {
      v4l2_std_id *id = arg;
      unsigned int i;

dprintk(2,"card(%d) VIDIOC_S_STD called\n",card->cardnum);
      for (i = 0; i < GENERIC_TVNORMS; i++)
              if (*id & generic_tvnorms[i].v4l2_id)
                      break;
      if (i == GENERIC_TVNORMS)
              return -EINVAL;

      down_interruptible(&card->lock);
      set_tvnorm(card,i);
      up(&card->lock);
      return 0;
     }
     /* Enumerate video inputs */
     case VIDIOC_ENUMINPUT:
      {
       struct v4l2_input *i = arg;
       unsigned int n;

dprintk(2,"card(%d) VIDIOC_ENUMINPUT called\n",card->cardnum);
       n = i->index;
       if (n >= generic_tvcards[card->type].video_inputs)
         return -EINVAL;

       memset(i,0,sizeof(*i));
       i->index    = n;
       i->type     = V4L2_INPUT_TYPE_CAMERA;
       i->audioset = 1;

       /* you never know, they may make one without a tuner :) */
       if (i->index == generic_tvcards[card->type].tuner) {
               sprintf(i->name, "tuner");
               i->type  = V4L2_INPUT_TYPE_TUNER;
               i->tuner = 0;
       } else if (i->index == generic_tvcards[card->type].svhs) {
               sprintf(i->name, "svideo");
       } else {
               sprintf(i->name,"composite");
       }
       if (i->index == card->mux) {
          u32 dstatus = BTREAD(card,BT829_DSTATUS);
          /* check for video signal */
          if ((dstatus & BT829_DSTATUS_PRES) == 0)
            i->status |= V4L2_IN_ST_NO_SIGNAL;
          /* check for horizontal lock */
          if ((dstatus & BT829_DSTATUS_HLOC) == 0)
            i->status |= V4L2_IN_ST_NO_H_LOCK;
//        if ((dstatus & BT829_DSTATUS_NUML) == 0){
//	    printk(KERN_INFO "NTSC/PAL-M signal found\n");
//	  } else {
//	    printk(KERN_INFO "PAL/SECAM signal found\n");
//	  }
       }
       for (n = 0; n < GENERIC_TVNORMS; n++)
         i->std |= generic_tvnorms[n].v4l2_id;
       return 0;
      }
     /* Query the current video input */
     case VIDIOC_G_INPUT:
      {
       int *i = arg;
dprintk(2,"card(%d) VIDIOC_G_INPUT called\n",card->cardnum);
       *i = card->mux - 1;
       return 0;
      }
     /* Select the current video input */
     case VIDIOC_S_INPUT:
      {
       unsigned int *i = arg;

dprintk(2,"card(%d) VIDIOC_S_INPUT change input mux to %d\n",card->cardnum, *i);
       if (*i > generic_tvcards[card->type].video_inputs)
          return -EINVAL;
       down_interruptible(&card->lock);
       set_input(card,*i+1);
       up(&card->lock);
      return 0;
      }
     /*  Get tuner attributes */
     case VIDIOC_G_TUNER:
      {
       struct v4l2_tuner *t = arg;

dprintk(2,"card(%d) VIDIOC_G_TUNER called\n",card->cardnum);
       if (t->index != 0)
          return -EINVAL;

       down_interruptible(&card->lock);
       memset(t,0,sizeof(*t));
       strncpy(t->name, "tuner", sizeof(t->name));
       t->type       = V4L2_TUNER_ANALOG_TV;
       t->rangehigh  = 0xffffffffUL;
       if (card->audio.deviceid == TDA9850) {
         t->capability = V4L2_TUNER_CAP_NORM|V4L2_TUNER_CAP_STEREO|V4L2_TUNER_CAP_SAP;
         t->rxsubchans = V4L2_TUNER_SUB_STEREO|V4L2_TUNER_SUB_SAP;
       } else {
         t->capability = V4L2_TUNER_CAP_NORM|V4L2_TUNER_CAP_STEREO;
         t->rxsubchans = V4L2_TUNER_SUB_STEREO;
       }

       /* check if the tuner has a horizontal lock */
       if (BTREAD(card,BT829_DSTATUS)&BT829_DSTATUS_HLOC)
         t->signal = 0xffff;

       if (card->stereo) {
         t->audmode     = V4L2_TUNER_MODE_STEREO;
	 t->rxsubchans |= V4L2_TUNER_SUB_STEREO;
       }
       if (card->sap) {
         t->audmode     = V4L2_TUNER_MODE_LANG1;
	 t->rxsubchans = V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_LANG2;
       }
       up(&card->lock);

       return 0;
      }
     /*  Set tuner attributes (audio stereo/sap) */
     case VIDIOC_S_TUNER:
     {
      struct v4l2_tuner *t = arg;

dprintk(2,"card(%d) VIDIOC_S_TUNER called\n",card->cardnum);
      if (generic_tvcards[card->type].tuner == -1)
              return -EINVAL;
      if (t->index != 0)
              return -EINVAL;
      down_interruptible(&card->lock);
      if (t->audmode == V4L2_TUNER_MODE_MONO){
	card->stereo = 0;
	card->sap = 0;
      } else if (t->audmode == V4L2_TUNER_MODE_STEREO){
	card->stereo = 1;
	card->sap = 0;
      } else if (t->audmode == V4L2_TUNER_MODE_LANG1){
	card->stereo = 1;
	card->sap = 1;
      } else if (t->audmode == V4L2_TUNER_MODE_SAP){
	card->stereo = 1;
	card->sap = 1;
      } else {
        dprintk(1,"card(%d) unknown %d\n",card->cardnum,t->audmode);
      }
      up(&card->lock);
      board_setaudio(card);
      return 0;
     }
     /* get tuner freq */
     case VIDIOC_G_FREQUENCY:
     {
      struct v4l2_frequency *f = arg;

dprintk(2,"card(%d) VIDIOC_G_FREQUENCY called\n",card->cardnum);
      memset(f,0,sizeof(*f));
      f->type = V4L2_TUNER_ANALOG_TV;
      f->frequency = card->freq;
      return 0;
     }
     /* set tuner freq */
     case VIDIOC_S_FREQUENCY:
     {
      struct v4l2_frequency *f = arg;

      if (unlikely(f->tuner != 0))
              return -EINVAL;
      if (unlikely(f->type != V4L2_TUNER_ANALOG_TV))
              return -EINVAL;
      down_interruptible(&card->lock);
      card->freq = f->frequency;
dprintk(2,"card(%d) VIDIOC_S_FREQUENCY set to %ld\n",card->cardnum, card->freq);
      fi12xx_tune(card);
      up(&card->lock);
      return 0;
     }
     default:
       return -ENOIOCTLCMD;
  } 
  return 0;
}

/* called by pci_module_init, and is passed any cards listed in
our table generic_pci_driver */
int __devinit generic_probe(struct pci_dev *dev,
                                const struct pci_device_id *pci_id)
{
  GENERIC_CARD *card;
  unsigned char lat;
  u32 romaddr;
  u8 *romtable, *ptr;
  unsigned char *biosptr;
  u32 val;
  int refclock;
  struct proc_dir_entry *proc_handler;
  char buf[10];
  int bufsize;
  unsigned int flags;

  /* limit the number of cards initialized to MAX_CARDS */
  if (num_cards_detected == MAX_CARDS)
    return -ENOMEM;

  /* print out the name of the device */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
  printk(KERN_INFO "genericv4l: %s found card #%d.\n", dev->name, num_cards_detected);
#else
  printk(KERN_INFO "genericv4l: %s found card #%d.\n", pci_name(dev), num_cards_detected);
#endif
  /* point card to empty slot in cards array */
  card=&cards[num_cards_detected];

  memset(card,0,sizeof(*card)); /* set all values to 0 */
  refclock = 0;
  card->cardnum = num_cards_detected;
  card->dev = dev; /* keep a pointer to the pci data */

  /* initialize locks */
  init_MUTEX(&card->lock);
  init_MUTEX(&card->lockvbi);
  init_MUTEX(&card->lockcap);
  spin_lock_init(&card->s_lock);

  /* the pci_device_id table lets us store some extra data which we can use to specify the type of board we found 
  */ 
  card->driver_data = pci_id->driver_data;

  if (pci_enable_device(dev) != 0) {
    printk(KERN_WARNING "genericv4l(%d): Can't enable device.\n", num_cards_detected);
    return -EIO;
  }

  /* tell the card we can do DMA transfers with any memory */
  if (!disabledma) {
    if (pci_set_dma_mask(dev, 0xffffffff) != 0) {
      printk(KERN_WARNING "genericv4l(%d): No suitable DMA available.\n", num_cards_detected);
      return -EIO;
    }
  }

  /* reserve the cards i/o resources, tag is just a name to associate with this lock */
  if (!request_mem_region(pci_resource_start(dev,2),
  			  pci_resource_len(dev,2),
  			  tag)) {
    printk(KERN_WARNING "genericv4l(%d): can't lock register aperture.\n", num_cards_detected);
    /* keep going it still might work! */
  }

  /* request end memory region 720x480x2 and 2 frames (so x2) 
  if (!request_mem_region(pci_resource_start(dev,0) + 
			  pci_resource_len(dev,0) - 1382400,
  			  1382400,
  			  tag)) {
    printk(KERN_WARNING "genericv4l(%d): can't lock video memory.\n", num_cards_detected);
   keep going it still might work! 
  } */

  /* enable bus mastering for this card */
  pci_set_master(dev);

  /* save a pointer in dev back to our generic_card struct */
  pci_set_drvdata(dev,card);

  /* save this cards revision number into our generic_card struct */
  pci_read_config_byte(dev, PCI_CLASS_REVISION, &card->revision);

  /* display info about the card we found */
  pci_read_config_byte(dev, PCI_LATENCY_TIMER, &lat);
  printk(KERN_INFO "genericv4l(%d): rev %d at %s, irq: %d, latency: %d, atifb: 0x%lx\n", num_cards_detected, card->revision, dev->slot_name, dev->irq, lat, pci_resource_start(dev,0));

  printk(KERN_INFO "IO at 0x%08lx 0x%08lx\n", pci_resource_start(dev, 1), pci_resource_end(dev, 1));
  printk(KERN_INFO "mmr at 0x%08lx 0x%08lx\n", pci_resource_start(dev, 2), pci_resource_end(dev, 2));
  romaddr = pci_resource_start(dev, PCI_ROM_RESOURCE);
  if (romaddr == 0){
    romaddr = 0x000C0000; //primary card
  }
  printk(KERN_INFO "bios at 0x%08x 0x%08lx\n", romaddr, pci_resource_end(dev, PCI_ROM_RESOURCE));

  /* map the cards framebuffer/capturebuffer/bios to our generic_card struct
  pci_resource_start(device,number) will give you the starting address of 
  one of the cards resources (change the number to get to others) */ 
  /* since we are doing dma its we dont really need to map atifb... */
  card->atifb=ioremap(pci_resource_start(dev, 0),pci_resource_len(dev, 0)); 

  if (pci_resource_start(dev, 2) == 0 || pci_resource_len(dev, 2) == 0) {
    card->MMR = (u8*)(card->atifb + 0x007FF800);
    printk(KERN_INFO "Using default value for mmr at atifb+0x007FF800\n");
  } else {
    card->MMR=ioremap(pci_resource_start(dev, 2),pci_resource_len(dev, 2));
  }

  card->MEM_0 = ((u32*)card->MMR)+256;  
  card->MEM_1 = ((u32*)card->MMR);  

  /* map rom into normal ram so we can access it */
  pci_resource_flags(dev, PCI_ROM_RESOURCE) |= PCI_ROM_ADDRESS_ENABLE;
  pci_read_config_dword(dev, PCI_ROM_ADDRESS, &val);
  val |= PCI_ROM_ADDRESS_ENABLE;
  pci_write_config_dword(dev, PCI_ROM_ADDRESS, val);

  biosptr=ioremap(romaddr,pci_resource_len(dev, PCI_ROM_RESOURCE)); 
  if (card->driver_data & RAGE128CHIP) {
    /* force the rage128 to let us read the bios...
	not sure why this works but it does (found by trial and error) */
    if (*(biosptr) == 0x0 || *(biosptr) == 0xFF) {
      printk (KERN_INFO "ERROR reading from bios, attempting to fix!\n");
      R128_I2C_CNTL_1 = 0x0;
      iounmap(card->atifb);
      card->atifb=ioremap(pci_resource_start(dev, 0),pci_resource_len(dev, 0)); 
    } else {
printk (KERN_INFO "reading from bios, worked! %d \n", *(biosptr));
    }
  }
  /* now read in some values from the rom */
  if (card->driver_data & MACH64CHIP){
    ptr = biosptr + 0x48;
    romtable = biosptr + *((u16*)ptr);
    ptr = romtable + 0x46;
    ptr = biosptr + *((u16*)ptr);
    memcpy(card->m64mminfo,ptr,5);
//  ptr = romtable + 0x10;
//  ptr = biosptr + *((u16*)ptr) + 0x08;
//  card->refclock = *((u16*)ptr);
  } else if (card->driver_data & RAGE128CHIP) {
    ptr = biosptr + 0x48;
    romtable = biosptr + *((u16*)ptr);
    ptr = romtable + 0x38;
    ptr = biosptr + *((u16*)ptr);
    if (ptr != biosptr) {
      memcpy(card->r128mminfo,ptr,12) ;
    }
    ptr = romtable + 0x30;
    ptr = biosptr + *((u16*)ptr) + 0x0E;
    /* before using divide by 100!!!! */
    card->refclock = *((u16*)ptr);
printk (KERN_INFO "refclock is %d\n", refclock);
  }

  /* now unmap the rom so we dont waste ram */
  iounmap(biosptr);
  pci_resource_flags(dev, PCI_ROM_RESOURCE) &= ~PCI_ROM_ADDRESS_ENABLE;
  pci_read_config_dword(dev, PCI_ROM_ADDRESS, &val);
  val &= ~PCI_ROM_ADDRESS_ENABLE;
  pci_write_config_dword(dev, PCI_ROM_ADDRESS, val);

/* initialize card */
  if (card->driver_data & MACH64CHIP) {
   if (m64_inita(card) != 0){
     printk(KERN_ERR "m64_init failed\n");
     iounmap(card->atifb);
     iounmap(card->MMR);
     release_mem_region(pci_resource_start(dev,2),pci_resource_len(dev,2));
     pci_set_drvdata(dev, NULL);
     return -ENODEV; 
   }
  } else if (card->driver_data & RAGE128CHIP) {
   if (r128_inita(card) != 0){
     printk(KERN_ERR "r128_init failed\n");
     iounmap(card->atifb);
     iounmap(card->MMR);
     release_mem_region(pci_resource_start(dev,2),pci_resource_len(dev,2));
     pci_set_drvdata(dev, NULL);
     return -ENODEV;
   }
  }

  board_setaudio(card);
  bt829_setmux(card); /* set the mux */

  /* set defaults for now, read in whats currently there later */
  set_brightness(card,32767);
  set_hue(card,32767);
  set_contrast(card,32767);
  set_saturation(card,32767);

  /* register functions to handle this cards irq */
  if (register_irq_handler(card) != 0){
    iounmap(card->atifb);
    iounmap(card->MMR);
    release_mem_region(pci_resource_start(dev,2),pci_resource_len(dev,2));
    pci_set_drvdata(dev, NULL);
    return -EIO;
  }

  /*setup a video4linux device for this card */
  register_video4linux(card);

  if (card->driver_data & MACH64CHIP){
    card->saved_crtc_cntl = MACH64_CRTC_INT_CNTL;
    card->saved_bus_cntl = MACH64_BUS_CNTL;

    // enable bus mastering.
    MACH64_BUS_CNTL = (card->saved_bus_cntl | MACH64_BUS_APER_REG_DIS |
  	MACH64_BUS_MSTR_RESET | MACH64_BUS_FLUSH_BUF |
	MACH64_BUS_PCI_DAC_DLY | MACH64_BUS_RD_DISCARD_EN |
	MACH64_BUS_RD_ABORT_EN) & ~MACH64_BUS_MASTER_DIS; 
  } else if (card->driver_data & RAGE128CHIP){ 
    //enable bus mastering for r128?

  }

  //allocate space for dma_tables (*150 holds enough for 640*480*2)
  //should check cards max capture and set max size to that?
  //dma table can transfer 4096 per entry, so we need 150 entries to transfer
  //640*480*2 (614400/4096 = 150) each one has 256 so we have more than enough
  if (!disabledma) {
    card->dma_table_buf0 = kmalloc(sizeof(DMA_BM_TABLE) * 256,GFP_KERNEL);
    card->dma_table_buf1 = kmalloc(sizeof(DMA_BM_TABLE) * 256,GFP_KERNEL);
    card->dma_table_buf2 = kmalloc(sizeof(DMA_BM_TABLE) * 256,GFP_KERNEL);
    card->dma_table_buf3 = kmalloc(sizeof(DMA_BM_TABLE) * 256,GFP_KERNEL);

    /* 8 should hold enough for 32k and we need two one for each field */
    card->dma_table_vbi = kmalloc(sizeof(DMA_BM_TABLE) * 15,GFP_KERNEL);

    if (card->dma_table_buf0 == NULL || card->dma_table_buf1 == NULL ||
    card->dma_table_buf2 == NULL || card->dma_table_buf3 == NULL ||
    card->dma_table_vbi == NULL){
       if (card->dma_table_buf0 != NULL) 
         kfree(card->dma_table_buf0);
       else 
         printk("Could not allocate dma table 1\n");
       if (card->dma_table_buf1 != NULL) 
         kfree(card->dma_table_buf1);
       else 
         printk("Could not allocate dma table 2\n");
       if (card->dma_table_buf2 != NULL) 
         kfree(card->dma_table_buf2);
       else 
         printk("Could not allocate dma table 3\n");
       if (card->dma_table_buf3 != NULL) 
         kfree(card->dma_table_buf3);
       else 
         printk("Could not allocate dma table 4\n");

       if (card->dma_table_vbi != NULL)
         kfree(card->dma_table_vbi);
       else
         printk("Could not allocate dma table for vbi\n");

       iounmap(card->atifb);
       iounmap(card->MMR);
       release_mem_region(pci_resource_start(dev,2),pci_resource_len(dev,2));
       pci_set_drvdata(dev, NULL);
       return -ENOMEM;
    }
  }

  /* override tunertype that we found with module param */
  if (tunertype != -1){
    card->tvnorm = tunertype;
  }
  set_tvnorm(card, card->tvnorm);

  //set default values
  card->width=generic_tvnorms[card->tvnorm].swidth;
  card->height=generic_tvnorms[card->tvnorm].sheight;
  card->frame=0;
  card->status=0;
  card->sap = 0;
  card->stereo = 1;
//  card->init.ov.w.width = 320;
//  card->init.ov.w.height = 240;
//  card->init.fmt = format_by_fourcc(V4L2_PIX_FMT_YUYV);
//  card->init.width = 320;
//  card->init.height = 240;
//  card->init.lines = 16;
  card->v4lmmaptype = 1;

  /* setup max size buffers here */
  bufsize = card->width * card->height * 2;

  /* make room for odd frame + vbi frame + even frame */
  card->buffer0 = 1024*card->videoram - bufsize - 8000;
  card->buffer1 = card->buffer0 - bufsize - 8000;
  card->vbibuffer = card->buffer1 - 32768;

  /* allocate space for two frames of max size eg 640*480*2) */
  card->fbmallocsize = generic_tvnorms[card->tvnorm].swidth * generic_tvnorms[card->tvnorm].sheight * 2;
  /* make sure its a page size multiple, because i am grabing a buffer for each frame (v4l2) instead of one large buffer (v4l1) so v4l1 programs expect an offset ... blah blah blah i will fix it :) */
  card->fbmallocsize = (card->fbmallocsize + PAGE_SIZE -1) & ~(PAGE_SIZE -1);

  if (!disabledma) {
    card->framebuffer1 = rvmalloc(card->fbmallocsize);
    card->framebuffer2 = rvmalloc(card->fbmallocsize);
    card->vbidatabuffer = rvmalloc(32768);
    if (card->framebuffer1 == NULL || card->framebuffer2 == NULL ||
        card->vbidatabuffer == NULL){
       printk("Could not allocate dma buffer\n");
       kfree(card->dma_table_buf0);
       kfree(card->dma_table_buf1);
       kfree(card->dma_table_buf2);
       kfree(card->dma_table_buf3);
       kfree(card->dma_table_vbi);
       iounmap(card->atifb);
       iounmap(card->MMR);
       release_mem_region(pci_resource_start(dev,2),pci_resource_len(dev,2));
       pci_set_drvdata(dev, NULL);
       return -ENOMEM;
    }
  } else {
    /* no dma so just point to the cards memory and the copy
     * routines will read it from there 
     * oh and make sure we capture on page boundaries :) */
     card->buffer0 = 1024*card->videoram - bufsize - 8000;
     card->buffer0 = (card->buffer0 + PAGE_SIZE -1) & ~(PAGE_SIZE -1);
     card->buffer1 = card->buffer0 - bufsize;
     card->buffer1 = (card->buffer1 + PAGE_SIZE -1) & ~(PAGE_SIZE -1);
     card->vbibuffer = card->buffer1 - 32768;
     card->vbibuffer = (card->vbibuffer + PAGE_SIZE -1) & ~(PAGE_SIZE -1);

    card->framebuffer1 = (u8*) card->atifb+card->buffer0;
    card->framebuffer2 = (u8*) card->atifb+card->buffer1;
    card->vbidatabuffer = (u8*) card->atifb+card->vbibuffer;
printk (KERN_INFO "atifb is 0x%p\n", card->atifb);
printk (KERN_INFO "framebuffer1 is 0x%p\n", card->framebuffer1);
printk (KERN_INFO "framebuffer2 is 0x%p\n", card->framebuffer2);
  }

  //set location of capture buffers
  if (card->driver_data & MACH64CHIP){
    MACH64_CAPTURE_BUF0_OFFSET = card->buffer0;
    MACH64_CAPTURE_BUF1_OFFSET = card->buffer1;
    MACH64_ONESHOT_BUF_OFFSET = card->vbibuffer; /* set it to store to vbibuffer*/
    //make sure video format is yuyv
    flags = MACH64_VIDEO_FORMAT;
    flags &= ~MACH64_VIDEO_IN; //clear video_in
    MACH64_VIDEO_FORMAT = flags | MACH64_VIDEO_VYUY422;
  } else {
    // set locations for r128 card here?
  }

  if (!disabledma) {
    // build dma tables (dma table, from_addr, to_addr, bufsize)
    build_dma_table(card->dma_table_buf0, card->buffer0,(unsigned long)card->framebuffer1, bufsize);
    build_dma_table(card->dma_table_buf1, card->buffer1,(unsigned long)card->framebuffer1, bufsize);
    build_dma_table(card->dma_table_buf2, card->buffer0,(unsigned long)card->framebuffer2, bufsize);
    build_dma_table(card->dma_table_buf3, card->buffer1,(unsigned long)card->framebuffer2, bufsize);
    // build the vbi buffers while we are at it
    build_dma_table(card->dma_table_vbi, card->vbibuffer,(unsigned long)card->vbidatabuffer, 32768);
  }

  /* add proc interface for this card */
  sprintf (buf,"%d",card->cardnum);
  proc_handler = create_proc_entry(buf, 0666, proc_dir);
  proc_handler->owner = THIS_MODULE;
  proc_handler->read_proc = proc_read;
  proc_handler->write_proc = proc_write;
  proc_handler->data = card;

  num_cards_detected++;
  return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
static struct video_device *vdev_init(GENERIC_CARD *card,
                                      struct video_device *template,
                                      char *type)
{
        struct video_device *vfd;

        vfd = video_device_alloc();
        if (vfd == NULL)
                return NULL;
        *vfd = *template;
        vfd->minor   = -1;
        vfd->dev     = &card->dev->dev;
        vfd->release = video_device_release;
        snprintf (vfd->name, sizeof(vfd->name),
  	  "genericv4l rev %d",card->revision);
        return vfd;
}
#endif

int __devinit register_video4linux(GENERIC_CARD *card)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
  /* copy defaults for this card from generic_video_template */
  memcpy(&card->video_dev, &generic_video_template, sizeof(generic_video_template));

  /* keep a pointer back to the generic_card struct in video_dev */
  card->video_dev.priv = card;

  /* set the name of the video device */
  snprintf (card->video_dev.name, sizeof(card->video_dev.name),
        "genericv4l rev %d",card->revision);

  /* register the video4linux device */
  if (video_register_device(&card->video_dev,VFL_TYPE_GRABBER, -1) < 0)
    return -1;
  printk(KERN_INFO "genericv4l(%d): registered device /dev/video%d\n",
                 card->cardnum,card->video_dev.minor & 0x1f);

  /* if the card supports closed captioning then make a device for that to */
  memcpy(&card->vbi_dev, &generic_vbi_template, sizeof(generic_vbi_template));

  /* keep a pointer back to the generic_card struct */
  card->vbi_dev.priv = card;

  if(video_register_device(&card->vbi_dev,VFL_TYPE_VBI,-1)<0) {
    video_unregister_device(&card->video_dev);
    return -1;
  }
  printk(KERN_INFO "genericv4l(%d): registered device /dev/vbi%d\n",
        card->cardnum,card->vbi_dev.minor & 0x1f);
#else
  card->video_dev = vdev_init(card, &generic_video_template, "video");
  if (card->video_dev == NULL) {
    return -1;
  }

  /* register the video4linux device */
  if (video_register_device(card->video_dev,VFL_TYPE_GRABBER, -1) < 0)
    return -1;

  printk(KERN_INFO "genericv4l(%d): registered device /dev/video%d\n",
                 card->cardnum,card->video_dev->minor & 0x1f);


  card->vbi_dev = vdev_init(card, &generic_vbi_template, "vbi");
  if (card->vbi_dev == NULL) {
    video_unregister_device(card->video_dev);
    return -1;
  }

  if(video_register_device(card->vbi_dev,VFL_TYPE_VBI,-1)<0) {
    video_unregister_device(card->video_dev);
    return -1;
  }
  printk(KERN_INFO "genericv4l(%d): registered device /dev/vbi%d\n",
	card->cardnum,card->vbi_dev->minor & 0x1f);
#endif
  return 0;
}

/* this is our generic irq handler, any time we get an interrupt this 
   function is called */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
void generic_irq_handler(int irq, void *dev_id, struct pt_regs * regs)
#else
static irqreturn_t generic_irq_handler(int irq, void *dev_id, struct pt_regs * regs)
#endif
{
  GENERIC_CARD *card = (GENERIC_CARD *)dev_id;
  int handled = 0;
  
  if (card->driver_data & MACH64CHIP){
    unsigned int flags;
    flags = MACH64_CRTC_INT_CNTL;
    if (flags & MACH64_BUSMASTER_INT_ACK){
      dprintk(3,"card(%d) busmaster interrupt\n",card->cardnum);
      //Acknowledge that we saw the interrupt
      MACH64_CRTC_INT_CNTL = flags | MACH64_BUSMASTER_INT_ACK;
      card->status |= STATUS_DMABUF_READY;
      handled = 1;
    }
    if (flags & MACH64_CAPBUF0_INT_ACK){
      dprintk(3,"card(%d) buf0 interrupt\n",card->cardnum);
      //Acknowledge that we saw the interrupt
      MACH64_CRTC_INT_CNTL = flags | MACH64_CAPBUF0_INT_ACK;
      card->field_count++;
      card->status |= STATUS_BUF0_READY | STATUS_POLL_READY;
      handled = 1;
    }
    if (flags & MACH64_CAPBUF1_INT_ACK){
      dprintk(3,"card(%d) buf1 interrupt\n",card->cardnum);
      //Acknowledge that we saw the interrupt
      MACH64_CRTC_INT_CNTL = flags | MACH64_CAPBUF1_INT_ACK;
      card->field_count++;
      card->status |= STATUS_BUF1_READY | STATUS_POLL_READY;
      handled = 1;
    }
    if (flags & MACH64_CAPONESHOT_INT_ACK){
      dprintk(3,"card(%d) oneshot interrupt\n",card->cardnum);
      //Acknowledge that we saw the interrupt
      MACH64_CRTC_INT_CNTL = flags | MACH64_CAPONESHOT_INT_ACK;
      card->status |= STATUS_VBI_READY | STATUS_POLL_READY;
      handled = 1;
    }
  } else if (card->driver_data & RAGE128CHIP){ 
    unsigned int status, mask;

    /* check if we have any frame captures ready */
    status = R128_CAP_INT_STATUS;
    mask = R128_CAP_INT_CNTL;
//printk (KERN_INFO "Cap Status is 0x%08x mask is 0x%08x\n", status, mask);

    if (status & 1){
      handled = 1;
      card->field_count++;

      /* since we are telling it to transfer the last frame captured
       * (even if this one is odd and odd if this one is even)
       * Then wait till we have another frame, or anytime
       * we change channels or capture size it will look funny */
      if (card->field_count > 2) {
        card->status |= STATUS_BUF1_READY | STATUS_POLL_READY;
      }
    }  
    if (status & 2){
      handled = 1;
      card->field_count++;
      if (card->field_count > 2) {
        card->status |= STATUS_BUF0_READY | STATUS_POLL_READY;
      }
    }
    //Acknowledge that we saw the interrupt
    R128_CAP_INT_STATUS = status & mask;

    /* check for dma interrupt now */
    status = R128_GEN_INT_STATUS;
    mask = R128_GEN_INT_CNTL;

//printk (KERN_INFO "Gen Status is 0x%08x mask is 0x%08x\n", status, mask);

    /* check if dma transfer finished */
    if (status & (1<<16)) {
      card->status |= STATUS_DMABUF_READY;
      handled=1;
    }
    //Acknowledge that we saw the interrupt
    R128_GEN_INT_STATUS = status & mask;
  }

  if (handled) {
     tasklet_schedule(&generic_tasklet);
  }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
  return IRQ_RETVAL(handled);
#endif
}

/* send all irq request for this card to generic_irq_handler */
int register_irq_handler(GENERIC_CARD *card)
{
  switch ((request_irq(card->dev->irq, generic_irq_handler, SA_SHIRQ, tag, (void *)card))){
    case -EINVAL: 
      printk(KERN_ERR "genericv4l(%d): bad irq number or handler\n",card->cardnum);
      break;
    case -EBUSY: 
      printk(KERN_ERR "genericv4l(%d): IRQ %d busy\n", card->cardnum, card->dev->irq);
      break;
    case 0: return 0;
    default: printk(KERN_ERR "genericv4l(%d): could not install irq handler\n", card->cardnum);
  }
  printk(KERN_ERR " Could not install irq handler...\n");
  printk(KERN_ERR " Perhaps you need to let your BIOS assign an IRQ to your video card\n");
  printk(KERN_ERR " Some ATI cards have a jumper that needs to be set for the card to get an IRQ\n");
  return -1;
}

static int generic_init_module(void) {
  int i,retval;

  printk(KERN_INFO "genericv4l: driver version %d.%d.%d loaded\n",
    (GENERIC_VERSION_CODE >> 16) & 0xff,
    (GENERIC_VERSION_CODE >> 8) & 0xff,
    GENERIC_VERSION_CODE & 0xff);

  //set number of cards detected to 0
  num_cards_detected = 0;

  if (disableinterlace){
    for (i = 0; i < GENERIC_TVNORMS; i++){
      generic_tvnorms[i].sheight /= 2;
    }
  }
  /* add proc interface */
  proc_dir = proc_mkdir("genericv4l", NULL);
  if (!proc_dir) {
    return -ENOMEM; 
  }
  proc_dir->owner = THIS_MODULE;

  //have the pci routines search for and initialize cards for us
  retval = pci_module_init(&generic_pci_driver);

  /* enable our irq handler */
  tasklet_enable(&generic_tasklet);

  return retval;
}

void __devexit generic_remove(struct pci_dev *pci_dev)
{
  GENERIC_CARD *card = pci_get_drvdata(pci_dev);
  char buf[10];

  printk("genericv4l(%d): unloading\n",card->cardnum);  

  if (card->driver_data & RAGE128CHIP) {
    /* must set R128_I2C_CNTL_1 to 0 or we will not beable to read from rom next time we load the module */
    R128_I2C_CNTL_1 = 0x0;
  } else if (card->driver_data & MACH64CHIP){ 
    //disable interrupts
    MACH64_CRTC_INT_CNTL &= ~(MACH64_CAPBUF0_INT_EN|MACH64_CAPBUF1_INT_EN|MACH64_CAPONESHOT_INT_EN|MACH64_BUSMASTER_INT_EN);
  }

  /* remove v4l devices */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
  if (card->video_dev.minor!=-1)
          video_unregister_device(&card->video_dev);
  if (card->vbi_dev.minor!=-1)
          video_unregister_device(&card->vbi_dev);
#else
  if (card->video_dev->minor!=-1)
          video_unregister_device(card->video_dev);
  if (card->vbi_dev->minor!=-1)
          video_unregister_device(card->vbi_dev);
#endif

  /* release the irq */
  free_irq(card->dev->irq,card);

  /* unmap the registers */
  iounmap(card->atifb);
  iounmap(card->MMR);

  /* unlock the cards memory regions */
  release_mem_region(pci_resource_start(pci_dev,2),pci_resource_len(pci_dev,2));

  /* remove pointer back to our struct */
  pci_set_drvdata(pci_dev, NULL);

  /* remove the dma table and frame memory */
  if (!disabledma) {
    rvfree(card->framebuffer1,card->fbmallocsize);
    rvfree(card->framebuffer2,card->fbmallocsize);
    rvfree(card->vbidatabuffer,32768);
    kfree(card->dma_table_buf0);
    kfree(card->dma_table_buf1);
    kfree(card->dma_table_buf2);
    kfree(card->dma_table_buf3);
    kfree(card->dma_table_vbi);
  }

  /* free proc interface for this card */
  sprintf (buf, "%d", card->cardnum);
  remove_proc_entry(buf, proc_dir);
}

static void generic_cleanup_module(void)
{
  /* disable interrupts */
  tasklet_disable(&generic_tasklet);

  /* have the pci routines search for and remove cards for us */
  pci_unregister_driver(&generic_pci_driver);

  remove_proc_entry("genericv4l", NULL);
  return;
}

module_init(generic_init_module);
module_exit(generic_cleanup_module);
