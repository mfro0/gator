#include <stdio.h>
#include <stdlib.h>


#include "packet_stream.h"
#include "global.h"

PACKET_STREAM *new_packet_stream(void)
{
PACKET_STREAM *s;

s=do_alloc(1, sizeof(PACKET_STREAM));
s->first=NULL;
s->last=NULL;
s->total=0;
s->threshhold=0;
s->consumer_thread_running=0;
s->consume_func=0;
s->priv=NULL;
pthread_mutex_init(&(s->ctr_mutex), NULL);
return s;
}

PACKET *new_generic_packet(size_t size)
{
PACKET *p;

p=do_alloc(1, sizeof(PACKET));
p->next=NULL;
p->prev=NULL;
p->size=size;
p->free=0;
if(size>0)p->buf=do_alloc(size, 1);
	else p->buf=NULL;
p->discard=0;
p->free_func=free_generic_packet;
p->type="GENERIC";
p->priv=NULL;
return p;
}

void free_generic_packet(PACKET *p)
{
p->size=0;
p->free=0;
if(p->buf!=NULL)free(p->buf);
free(p);
}

void deliver_packet(PACKET_STREAM *s, PACKET *p)
{
/* put the packet into the queue */
p->next=NULL;
p->prev=s->last;
if(s->last!=NULL)s->last->next=p;
s->last=p;
/* update total count and see if we need to spawn consumer thread*/
pthread_mutex_lock(&(s->ctr_mutex));
s->total+=p->free;
if((s->total>s->threshhold) && 
	!s->consumer_thread_running &&
	(s->consume_func!=NULL)){
	/* start consumer thread */
	if(pthread_create(&(s->consumer_thread_id), NULL, s->consume_func, s)<0){
		fprintf(stderr, "packet_stream: cannot create thread: ");
		perror("");
		} else {
		s->consumer_thread_running=0;
		}
		
	}
pthread_mutex_unlock(&(s->ctr_mutex));
}

void discard_packets(PACKET_STREAM *s)
{
PACKET *p;
pthread_mutex_lock(&(s->ctr_mutex));
while(s->first->discard){
	p=s->first;
	s->first=p->next;
	s->first->prev=NULL;
	if(p->free_func!=NULL)p->free_func(p);
	}
pthread_mutex_unlock(&(s->ctr_mutex));
}
