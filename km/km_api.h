#ifndef __KM_API_H__
#define __KM_API_H__

#include <linux/spinlock.h>

#define KM_API_VERSION "alpha-2.0"

#define KM_FIELD_TYPE_EOL		0      /* end of list marker */
#define KM_FIELD_TYPE_STATIC		1	/* static string */
#define KM_FIELD_TYPE_DYNAMIC_INT	2	/* pointer to integer */
#define KM_FIELD_TYPE_DYNAMIC_STRING	3	/* handle to string */
#define KM_FIELD_TYPE_PROGRAMMABLE	4	/* user specified behaviour */
#define KM_FIELD_TYPE_MEMORY_AREA       5	/* area of memory that can be mmaped */

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

typedef struct S_KM_FIELD {
	int type;
	char *name;
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
		} data;
	} KM_FIELD;

typedef struct {
	long number; /* set to the device number if active, to -1 if inactive */
	int use_count;
	spinlock_t lock;
	wait_queue_head_t wait;
	struct proc_dir_entry *control;
	struct proc_dir_entry *data;	
	KM_FIELD *fields;
	long num_fields;
	void *priv;
	} KM_DEVICE;

typedef struct {
	long num_fields;
	} KM_REQUEST;

int add_km_device(KM_FIELD *kmfl, void *priv);
int remove_km_device(int num);
void kmd_signal_state_change(int num);

#endif
