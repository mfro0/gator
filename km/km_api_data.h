/*     km preliminary version

       (C) Vladimir Dergachev 2001-2002
       
       GNU Public License
       
*/

#ifndef __KM_DATA_H__
#define __KM_DATA_H__

#include <linux/spinlock.h>

/* this is a simple chunk of data accessible only through kernel virtual space */

#define KDU_TYPE_VIRTUAL_BLOCK	1

typedef struct {
	long size;
	long free;
	void *ptr;
	} KM_DATA_VIRTUAL_BLOCK;

#define KDU_TYPE_GENERIC		100

typedef struct S_KM_DATA_UNIT{
	int number;
	spinlock_t lock;
	mode_t mode;
	long use_count;
	int type;
	struct proc_dir_entry *data;	
	void *data_private;
	void (*free_private)(struct S_KM_DATA_UNIT *);
	} KM_DATA_UNIT;

typedef struct {
	KM_DATA_UNIT *kdu;
	} KDU_FILE_PRIVATE_DATA;

int init_km_data_units(void);
void cleanup_km_data_units(void);
int km_allocate_data_virtual_block(KM_DATA_VIRTUAL_BLOCK *, mode_t mode);
void km_deallocate_data(long data_unit);

#endif
