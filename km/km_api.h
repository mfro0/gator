/*     km preliminary version

       (C) Vladimir Dergachev 2001-2003
       
       GNU Public License
       
*/

#ifndef __KM_API_H__
#define __KM_API_H__

#include <linux/spinlock.h>

#define KM_API_VERSION "alpha-3.0"

#define KM_FIELD_TYPE_EOL		0      /* end of list marker */
#define KM_FIELD_TYPE_STATIC		1	/* static string */
#define KM_FIELD_TYPE_DYNAMIC_INT	2	/* pointer to integer */
#define KM_FIELD_TYPE_DYNAMIC_STRING	3	/* handle to string */
#define KM_FIELD_TYPE_PROGRAMMABLE	4	/* user specified behaviour */
#define KM_FIELD_TYPE_MEMORY_AREA       5	/* area of memory that can be mmaped */
#define KM_FIELD_TYPE_LEVEL_TRIGGER	6	/* 1 if any client requested to be 1,
						   0 if no clients requested to be 0 */

typedef struct {
	char *string;
	} KM_FIELD_STATIC;
	
typedef struct {
	u32 *field;
	} KM_FIELD_DYNAMIC_INT;

/*       **************** NOTE ********************

          DYNAMIC_STRING variables are *NOT* meant for transfer of arbitrary string 
	  data to userspace. Rather, they are meant as a kind of "enumerated" type, with
	  string values tied to their addresses.
	  
	  In particular the following restrictions should be followed:
	  
	  1. The sizes of all possible string values should be known to the developer at the
	     design time. This makes sure that userspace programs cannot trick kernel into
	     gobbling up enormous chunks of memory.
	  2. Different values should *imply* different addresses they are located at.
	     (static constants like "YES" "NO" "MAYBE" satisfy this)
	    
*/	    

typedef struct {
	char **string;
	} KM_FIELD_DYNAMIC_STRING;
	
typedef struct {
	} KM_FIELD_PROGRAMMABLE;

/* memory area attribute values */	
#define KM_MEMORY_READABLE		1
#define KM_MEMORY_WRITABLE		2
#define KM_MEMORY_ATTACHED		3

typedef struct {
	int attribute;
	int updated;
	int use_count;
	void *address;
	long size;
	} KM_FIELD_MEMORY_AREA;

typedef struct {
	u32 count;
	void (*zero2one)(struct S_KM_FIELD *kmf);
	void (*one2zero)(struct S_KM_FIELD *kmf);
	} KM_FIELD_LEVEL_TRIGGER;

typedef struct S_KM_FIELD {
	int type;
	char *name;
	int length;
	int changed;
	spinlock_t *lock;
	void *priv;
	void (*read_complete)(struct S_KM_FIELD *kmf);
	union {
		KM_FIELD_STATIC 	c;
		KM_FIELD_DYNAMIC_INT 	i;
		KM_FIELD_DYNAMIC_STRING s;
		KM_FIELD_PROGRAMMABLE	p;
		KM_FIELD_MEMORY_AREA    m;
		KM_FIELD_LEVEL_TRIGGER  t;
		} data;
	int next_command;
	} KM_FIELD;

typedef struct {
	long number; /* set to the device number if active, to -1 if inactive */
	int use_count;
	spinlock_t lock;
	wait_queue_head_t wait;
	struct proc_dir_entry *control;
	KM_FIELD *fields;
	long num_fields;
	void *priv;
	int *command_hash;
	unsigned status_hash, report_hash;
	} KM_DEVICE;

typedef union {
	/* union members correspond to members of KM_FIELD */
	struct {
		u32 old_value;
		} i;
	struct {
		char *old_string; /* pointer only, no access to data */
		} s;
	struct {
		u32 old_count;
		int requested;
		} t;
	} KM_FIELD_DATA;

#define KM_STATUS_REQUESTED	1
#define KM_FIELD_UPDATE_REQUESTED  1

typedef struct {
	KM_DEVICE *kmd;
	spinlock_t lock;
	int request_flags;
	int *field_flags;
	char *buffer_read;
	long br_size;
	long br_free;
	long br_read;
	KM_FIELD_DATA *kfd;
	} KM_FILE_PRIVATE_DATA;

int add_km_device(KM_FIELD *kmfl, void *priv);
int remove_km_device(int num);
void kmd_signal_state_change(int num);

/* functions for in-kernel access to km devices */
int num_km_devices(void);
KM_FILE_PRIVATE_DATA *open_km_device(int number);
void close_km_device(KM_FILE_PRIVATE_DATA *kmfpd);
void km_fo_control_perform_command(KM_FILE_PRIVATE_DATA *kmfpd, const char *command, size_t count);


#if 1
#define KM_CHECKPOINT printk("**CKPT %s %s %d\n", __FILE__, __FUNCTION__, __LINE__);
#define KM_API_DEBUG  printk
#else
#define KM_CHECKPOINT
#define KM_API_DEBUG  if(0)printk
#endif

#endif
