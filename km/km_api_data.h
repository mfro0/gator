/*     km preliminary version

       (C) Vladimir Dergachev 2001-2002
       
       GNU Public License
       
*/

#ifndef __KM_DATA_H__
#define __KM_DATA_H__

/* this is a simple chunk of data accessible only through kernel virtual space */

#define TYPE_VIRTUAL_BLOCK	1

typedef struct {
	long size;
	long free;
	void *ptr;
	} KM_DATA_VIRTUAL_BLOCK;

typedef struct {
	int number;
	mode_t mode;
	long use_count;
	int type;
	struct proc_dir_entry *data;	
	void *data_private;
	} KM_DATA_UNIT;

int init_km_data_units(void);
void cleanup_km_data_units(void);
int km_allocate_data_virtual_block(KM_DATA_VIRTUAL_BLOCK *, mode_t mode);
void km_deallocate_data(int data_unit);

#endif
