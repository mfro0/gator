/*     km preliminary version

       (C) Vladimir Dergachev 2001-2002
       
       GNU Public License
       
*/

#ifndef __KM_DATA_H__
#define __KM_DATA_H__

#include <linux/spinlock.h>


/* this describes an age of the buffer..
   since any number is capable of rollover
   it is best to use these  only as an indicator that
   the data in the buffer has been updated 
   
   Q: should it be unsigned ? It is so convinient to use age=-1
      to mark unused buffers.   
   
   */

typedef int KM_BUFFER_AGE;

/* the following struct provides meta information needed
   to interpret KM_DATA_VIRTUAL_BLOCK as a stream 
   Reserve one struct for each buffer
   This is updated by the provider, not consumers */


typedef struct {
	KM_BUFFER_AGE age;  /* age of the corresponding buffer */
	int next;      /* buffer that is next in chain */
	int prev;	/* buffer that is before it in chain */
	int flag;       /* flag that relates special information about
	                   this buffer, see below: */
	#define KM_STREAM_BUF_PINNED	1
	int user_flag;   /* for whatever the user has a need for */
	} KM_STREAM_BUFFER_INFO;

/* this is a sequence of data chunks */

#define KDU_TYPE_VIRTUAL_BLOCK	1

typedef struct {
	long size;   /* size of each subunit */
	long n;      /* total number of subunits */
	long *free;  /* amount of data (starting from 0) that is valid in a subunit*/
	void **ptr;  /* pointer to the data of the subunit */
	wait_queue_head_t *dataq;  /* waitqueue that can be woken up to signal that more data is available */
	KM_STREAM_BUFFER_INFO *kmsbi;
	} KM_DATA_VIRTUAL_BLOCK;

#define KDU_TYPE_GENERIC		100

typedef struct S_KM_DATA_UNIT{
	int number;
	spinlock_t lock;
	wait_queue_head_t dataq;
	mode_t mode;
	long use_count;
	int type;
	struct proc_dir_entry *data;	
	void *data_private;
	void (*free_private)(struct S_KM_DATA_UNIT *);
	int (*mmap)(struct file *file, struct vm_area_struct *vma);
	int (*read)(struct file *file, char *buf, size_t count, loff_t *ppos);
	} KM_DATA_UNIT;

typedef struct {
	KM_DATA_UNIT *kdu;
	int buffer;  /* buffer that we are reading now */
	long bytes_read; /* amount of data read in current buffer */
	KM_BUFFER_AGE age; /* age of the buffer we are reading or read last */
	} KDU_FILE_PRIVATE_DATA;

int init_km_data_units(void);
void cleanup_km_data_units(void);
int km_allocate_data_virtual_block(KM_DATA_VIRTUAL_BLOCK *, mode_t mode);
void km_deallocate_data(int data_unit);

KDU_FILE_PRIVATE_DATA* km_data_create_kdufpd(int data_unit);
void km_data_destroy_kdufpd(KDU_FILE_PRIVATE_DATA *kdufpd);
long km_data_generic_stream_read(KDU_FILE_PRIVATE_DATA *kdufpd, KM_DATA_VIRTUAL_BLOCK *dvb,
	 char *buf, unsigned long count, int nonblock, 
	 int user_flag, int user_flag_mask);
unsigned int km_data_generic_stream_poll(KDU_FILE_PRIVATE_DATA *kdufpd, KM_DATA_VIRTUAL_BLOCK *dvb,
	 struct file *file, poll_table *wait);


#endif
