/*     avview preliminary version

       (C) Vladimir Dergachev 2001
       
       GNU Public License
       
*/

#ifndef __PACKET_STREAM_H__
#define __PACKET_STREAM_H__

#include <pthread.h>
#include "global.h"

/* consumer threads should NEVER touch packet with p->next==NULL */
/* for now s->ctr_mutex protects the whole PACKET_STREAM not just the fields
   indicated in comments */

typedef struct S_PACKET{
	struct S_PACKET *next, *prev;   
	size_t size;
	size_t free;
	int discard;
	int recycle;
	int64 timestamp;
	unsigned char *buf;
	void (*free_func)(struct S_PACKET *);
	char *type;
	void *priv;
	} PACKET;

typedef struct S_PACKET_STREAM {
	struct S_PACKET *first, *last, *unused;
	size_t total;
	size_t unused_total;
	size_t threshold;
	int  consumer_thread_running; /* ctr */
	int  producer_thread_running;
	int  stop_stream;
	pthread_t consumer_thread_id;  /* cti */
	pthread_mutex_t ctr_mutex;  /* protects ctr , cti and total values */
	void (*consume_func)(struct S_PACKET_STREAM *);
	void *priv;   
	} PACKET_STREAM;
	
PACKET_STREAM *new_packet_stream(void);
PACKET *new_generic_packet(PACKET_STREAM *s, size_t size);
void free_generic_packet(PACKET *p);
void deliver_packet(PACKET_STREAM *s, PACKET *p);
void discard_packets(PACKET_STREAM *s);

#define STOP_CONSUMER_THREAD	1
#define STOP_PRODUCER_THREAD	2


#endif
