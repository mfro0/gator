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

#ifndef GENERICV4L_HEADER
#define GENERICV4L_HEADER 1

#include <linux/version.h>

#if defined(CONFIG_MODVERSIONS) && ! defined(MODVERSIONS)
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,17)
#include <linux/modversions.h>
#else
#include <config/modversions.h>
#include <linux/poll.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17)
#include <media/v4l2-dev.h>
#endif
#define MODVERSIONS
#endif
#endif

#include <linux/pci.h>
#include <linux/videodev.h>
#define GENERIC_VERSION_CODE KERNEL_VERSION(0,0,1)
#define MAX_CARDS 10
#ifdef dprintk
# undef dprintk
#endif
#define dprintk(level,fmt, arg...)    if (debug >= level) \
        printk(KERN_DEBUG "genericv4l: " fmt, ## arg)

#define RESOURCE_OVERLAY       1
#define RESOURCE_VIDEO         2
#define RESOURCE_VBI           4

#define VBI_MAXLINES 32

#define VIDEO_MAX_FRAMES 2

#define FORMAT_FLAGS_DITHER       0x01
#define FORMAT_FLAGS_PACKED       0x02
#define FORMAT_FLAGS_PLANAR       0x04
#define FORMAT_FLAGS_RAW          0x08
#define FORMAT_FLAGS_CrCb         0x10

#define FRAME_BUFFER_TO_SYSTEM  0x1

#define XY(X,Y)         \
        ( ( (0xFFFF&((u32)(X))) << 16 ) | (0xFFFF&((u32)(Y))) )

#define HI(X)            ( ((X)>>8) & 0xFF )
#define LO(X)            ( (X) & 0xFF )


#define STATUS_CAPTURING (1 << 0)
#define STATUS_BUF0_READY (1 << 1)
#define STATUS_BUF1_READY (1 << 2)
#define STATUS_DMABUF_READY (1 << 3)
#define STATUS_VBI_CAPTURE (1 << 4)
#define STATUS_VBI_READY (1 << 5)
#define STATUS_VBI_DMABUF_READY (1 << 6)
#define STATUS_POLL_READY (1 << 7)

/* set flags for pci_id extra data here, we could store what chip
which i2c driver to use and whatever else */
#define MACH64CHIP (1 << 0)
#define RAGE128CHIP (1 << 1)
#define DACGEN_I2C (1 << 2)
#define GPIO_I2C (1 << 3)
#define LG_I2C (1 << 4)
#define IMPACTTV_I2C (1 << 5)
#define RAGEPRO_I2C (1 << 6)

struct generici2cchip {
  int   deviceid;
  int   revision;
  char* ident;
  char* name;
  u8    addr;
};
typedef struct S_GENERIC_FH GENERIC_FH;
typedef struct S_GENERIC_CARD GENERIC_CARD;
struct generic_buffer;

/* video 4 linux stuff */
enum videobuf_state {
  STATE_NEEDS_INIT = 0,
  STATE_PREPARED   = 1,
  STATE_QUEUED     = 2,
  STATE_ACTIVE     = 3,
  STATE_DONE       = 4,
  STATE_ERROR      = 5,
  STATE_IDLE       = 6,
};

struct videobuf_dmabuf {
  /* for userland buffer */
  int                 offset;
  struct page         **pages;

  /* for kernel buffers */
  void                *vmalloc;

  /* common */
  struct scatterlist  *sglist;
  int                 sglen;
  int                 nr_pages;
  int                 direction;
};

struct videobuf_buffer {
   unsigned int            i;

   /* info about the buffer */
   unsigned int            width;
   unsigned int            height;
   unsigned long           size;
   enum v4l2_field         field;
   enum videobuf_state     state;
   struct videobuf_dmabuf  dma;
   struct list_head        stream;  /* QBUF/DQBUF list */

   /* for mmap'ed buffers */
   size_t                  boff;    /* buffer offset (mmap) */
   size_t                  bsize;   /* buffer size */
   unsigned long           baddr;   /* buffer addr (userland ptr!) */
   struct videobuf_mapping *map;

   /* touched by irq handler */
   struct list_head        queue;
   wait_queue_head_t       done;
   unsigned int            field_count;
   struct timeval          ts;
};

struct btcx_riscmem {
        unsigned int   size;
        u32            *cpu;
        u32            *jmp;
        dma_addr_t     dma;
};

struct generic_buffer {
        struct videobuf_buffer     vb;

        struct btcx_riscmem        top;
        struct btcx_riscmem        bottom;
};

struct videobuf_queue_ops {
  int (*buf_setup)(struct file *file,
                   unsigned int *count, unsigned int *size);
  int (*buf_prepare)(struct file *file,struct videobuf_buffer *vb,
                     enum v4l2_field field);
  void (*buf_queue)(struct file *file,struct videobuf_buffer *vb);
  void (*buf_release)(struct file *file,struct videobuf_buffer *vb);
};

struct videobuf_queue {
  struct semaphore           lock;
  spinlock_t                 *irqlock;
  struct pci_dev             *pci;

  enum v4l2_buf_type         type;
  unsigned int               msize;
  enum v4l2_field            field;
  enum v4l2_field            last; /* for field=V4L2_FIELD_ALTERNATE */
  struct videobuf_buffer     *bufs[VIDEO_MAX_FRAME];
  struct videobuf_queue_ops  *ops;

  /* capture via mmap() + ioctl(QBUF/DQBUF) */
  unsigned int               streaming;
  struct list_head           stream;

  /* capture via read() */
  unsigned int               reading;
  unsigned int               read_off;
  struct videobuf_buffer     *read_buf;
};

struct videobuf_mapping {
        unsigned int count;
        int highmem_ok;
        unsigned long start;
        unsigned long end;
        struct videobuf_queue *q;
};

struct tuner_types {
  int type; 
  int tunertype;
  char *vendor; 
  char *ident; 
  char *system; 
};

struct generic_tvnorm {
        int   v4l2_id;
        char  *name;
        u32   Fsc;
        u16   swidth, sheight; /* scaled standard width, height */
        u16   totalwidth;
        u8    adelay, bdelay, iform;
        u32   scaledtwidth;
        u16   hdelayx1, hactivex1;
        u16   vdelay;
        u8    vbipack;
        u16   vtotal;
        int   sram;
};

#define GENERIC_IFORM_AUTO       0
#define GENERIC_IFORM_NTSC       1
#define GENERIC_IFORM_NTSC_J     2
#define GENERIC_IFORM_PAL_BDGHI  3
#define GENERIC_IFORM_PAL_M      4
#define GENERIC_IFORM_PAL_N      5
#define GENERIC_IFORM_SECAM      6
#define GENERIC_IFORM_PAL_NC     7
#define GENERIC_IFORM_NORM       1
#define GENERIC_IFORM_XT0        (1<<3)
#define GENERIC_IFORM_XT1        (2<<3)

struct generic_tvcard
{
        char *name;
        unsigned int video_inputs;
        unsigned int audio_inputs;
        unsigned int tuner;
        unsigned int svhs;
	unsigned int captmin_x, captmin_y;
};

struct generic_format {
        char *name;
        int  palette;         /* video4linux 1      */
        int  fourcc;          /* video4linux 2      */
        int  depth;           /* bit/pixel          */
        int  flags;
};

struct generic_overlay {
  int tvnorm;
  struct v4l2_rect       w;
  enum v4l2_field        field;
  struct v4l2_clip       *clips;
  int                    nclips;
};

/* for each process that opens a v4l device we assign this struct
 this way we can keep track of what process is using what resources etc */
struct S_GENERIC_FH {
  GENERIC_CARD *card;
  int resources;
  enum v4l2_buf_type type;
  struct videobuf_queue    cap;
  struct videobuf_queue    vbi;
  const struct generic_format *fmt;
  const struct generic_format *ovfmt;
  int width;
  int height;
  int lines;

  struct generic_overlay      ov;
};

typedef struct {
  u32 from_addr;
  u32 to_addr;
  u32 size;
  u32 reserved;
} DMA_BM_TABLE;

typedef struct fifo {
  int front, back, data[VIDEO_MAX_FRAMES+1];
} FIFO;

/* struct that holds information about each card we find */
struct S_GENERIC_CARD {

  /* pointer to the pci device structure */
  struct pci_dev *dev; 

  /* assigned in order of probe 0 to MAX_CARDS */
  int cardnum; 

  //flag for mmap to map in one chunk (0 for v4l) or several (1 for v4l2)
  int v4lmmaptype;
  FIFO frame_queue;
  int fbmallocsize; /* size of frame buffer stored here */

  /* cards revision number, keep this incase you need to do special stuff for different revisions of cards */
  unsigned char revision; 

  /* this should be mapped to the cards registers for easy access */ 
  unsigned char *atifb; /* ati frame buffer */
  unsigned char *MMR;
  volatile u32 *MEM_0, *MEM_1; /* Mach64 Registers? */
  struct generici2cchip audio; /* audio chip i2c address */
  struct generici2cchip fi12xx; /* tuner info */
  u8 tuner;
  int videoram;        /* Total amount of video RAM in kb */
  u32 buffer0, buffer1; /*video capture buffer offsets into atifb mem */
  u32 vbibuffer; /* vbi capture buffer offset into atifb mem */

  struct generici2cchip board;
  struct generici2cchip bt829; /* bt829 address */
  u8 boardinfo;

  /* used to figure out what tuner we have (Read from rom) */
  u8 m64mminfo[5];
  u8 r128mminfo[12];

  u8 R128_M, R128_N, R128_TIME;

  /* i2c stuff here */
//  u16 m64i2cinfo; /* used to figure out i2c mode to use */

  /* Register pointers, bitmasks and values used by lowlevel routines */
  volatile u32 *sclreg, *sdareg;
  u32 scldir, sdadir, sclset, sdaset, sdaget;
  struct i2c_funcs *i2c;
  u8 tv_i2c_cntl;
  u32 i2c_cntl_0;

  /* ATI chip refclock crystal frequency */
  unsigned long refclock;     

  int stereo, sap, mute;

  /* video4linux 1 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
   struct video_device video_dev;
   struct video_device vbi_dev;
#else
  struct video_device *video_dev;
  struct video_device *vbi_dev;
#endif

  unsigned int type; /* type of card (out of our supported card list) */
  ulong driver_data; 

  /* locking */
  /* anytime you access the card (registers) use this lock */
  struct semaphore lock;
  struct semaphore lockvbi;
  struct semaphore lockcap;
  int resources;
  spinlock_t s_lock;

  /* vbi close caption stuff */
//  char ccdata[16]; /* buffer for close caption characters */
//  int ccdata_num; /* num of characters in the buffer */

  /* video state */
  /* current input (tv, svhs, composite) */
  int width,height;
  unsigned long freq;
  unsigned long lastfreq; /* direction matters when telling the tuner to change channel */
  int tvnorm,hue,contrast,bright,saturation;
  unsigned int mux; /* Video source (Bt829 MUX) 1=Composite,2=tuner,3=S-Video */
  int cbsense;
  u16 luma, sat_u, sat_v;
  
  struct video_buffer fbuf;

  int status; /* status of card, is it capturing etc */
  GENERIC_FH init;

   u32 saved_bus_cntl,saved_crtc_cntl;

   /* dma tables and frame buffers */
   DMA_BM_TABLE *dma_table_buf0,*dma_table_buf1,*dma_table_buf2,*dma_table_buf3;
   DMA_BM_TABLE *dma_table_vbi;
   //u32 *framebuffer1,*framebuffer2, *vbidatabuffer;
   u8 *framebuffer1,*framebuffer2, *vbidatabuffer;
   dma_addr_t dmabuf1, dmabuf2, dmavbi;
   int frame; /* which frame they want to capture to */
   int whichfield; /* which field we just captured */
   unsigned int field_count; 
};

extern int debug;
extern int disableinterlace;
extern int tunertype;
extern struct generic_tvnorm generic_tvnorms[];

/* function prototypes */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
int __devinit generic_probe(struct pci_dev *dev,
   const struct pci_device_id *pci_id);
void __devexit generic_remove(struct pci_dev *pci_dev);
void generic_irq_handler(int irq, void *dev_id, struct pt_regs * regs);
#else
int generic_probe(struct pci_dev *dev,
   const struct pci_device_id *pci_id);
void generic_remove(struct pci_dev *pci_dev);
#endif
int register_irq_handler(GENERIC_CARD *card);
int register_video4linux(GENERIC_CARD *card);
ssize_t generic_read(struct file *file, char *data,
                         size_t count, loff_t *ppos);
ssize_t generic_write(struct file *file, const char *buf,
        size_t count, loff_t *ppos);
int generic_open(struct inode *inode, struct file *file);
int generic_release(struct inode *inode, struct file *file);
int generic_ioctl(struct inode *inode, struct file *file,
                      unsigned int cmd, unsigned long arg);
int generic_mmap(struct file *file, struct vm_area_struct *vma);
unsigned int generic_poll(struct file *file, poll_table *wait);
int generic_do_ioctl(struct inode *inode, struct file *file,
                         unsigned int cmd, void *arg);
void i2c_vidiocschan(GENERIC_CARD *card);
int set_tvnorm(GENERIC_CARD *card, unsigned int norm);
void generic_vbi_try_fmt(GENERIC_FH *fh, GENERIC_CARD *card, struct v4l2_format *f);
void generic_enable_vbi(GENERIC_CARD *card);
void generic_disable_vbi(GENERIC_CARD *card);
void generic_enable_capture(GENERIC_CARD *card);
void generic_disable_capture(GENERIC_CARD *card);
void generic_blockhandler(unsigned long param);
void generic_mmap_vopen(struct vm_area_struct *vma);
void generic_mmap_vclose(struct vm_area_struct *vma);
void grab_frame(GENERIC_CARD *card);
int grab_vbi(GENERIC_CARD *card);
int proc_read(char *buffer, char **start, off_t offset, int size, int *eof, void *data);
int proc_write(struct file *file, const char *buffer, unsigned long count, void *data);


#endif
