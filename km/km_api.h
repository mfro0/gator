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

typedef struct {
	char **string;
	} KM_FIELD_DYNAMIC_STRING;
	
typedef struct {
	} KM_FIELD_PROGRAMMABLE;

/* memory area attribute values */	
#define KM_MEMORY_READABLE		1
#define KM_MEMORY_WRITABLE		2

typedef struct {
	int attribute;
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
	struct proc_dir_entry *control;
	struct proc_dir_entry *data;	
	char *buffer_read;
	long br_size;
	long br_free;
	long br_read;
	KM_FIELD *fields;
	long num_fields;
	void *priv;
	} KM_DEVICE;

typedef struct {
	long num_fields;
	} KM_REQUEST;

int add_km_device(KM_FIELD *kmfl, void *priv);
int remove_km_device(int num);

#endif
