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
s->unused=NULL;
s->total=0;
s->unused_total=0;
s->threshold=0;
s->consumer_thread_running=0;
s->producer_thread_running=0;
s->stop_stream=0;
s->consume_func=NULL;
s->priv=NULL;
pthread_mutex_init(&(s->ctr_mutex), NULL);
pthread_cond_init(&(s->suspend_consumer_thread), NULL);
return s;
}

PACKET *new_generic_packet(PACKET_STREAM *s, size_t size)
{
PACKET *p;

if(s!=NULL){
	pthread_mutex_lock(&(s->ctr_mutex));
	p=s->unused;
	while(p!=NULL){
		if((size>=p->size)&&(size<2*p->size)){
			/* found good one */
			/* unlink it */
			if(p->prev!=NULL){
				p->prev->next=p->next;
				}
			if(p->next!=NULL){
				p->next->prev=p->prev;
				}
			if(p==s->unused){
				s->unused=p->next;
				}
			/* clean it up */
			p->prev=NULL;
			p->next=NULL;
			p->free=0;
			p->discard=0;
			s->unused_total-=p->size;
			pthread_mutex_unlock(&(s->ctr_mutex));
			return p;
			}
		p=p->next;
		}
	pthread_mutex_unlock(&(s->ctr_mutex));
	}

p=do_alloc(1, sizeof(PACKET));
p->next=NULL;
p->prev=NULL;
p->size=size;
p->free=0;
p->recycle=0;
if(size>0){
	p->buf=do_alloc(size,1);
	while(p->buf==NULL){
		sleep(1);
		p->buf=do_alloc(size, 1);
		}
	} else p->buf=NULL;
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
if(p->buf!=NULL)do_free(p->buf);
do_free(p);
}

void deliver_packet(PACKET_STREAM *s, PACKET *p)
{
/* put the packet into the queue */
p->next=NULL;
p->prev=s->last;
if(s->last!=NULL)s->last->next=p;
s->last=p;
if(s->first==NULL)s->first=p;
/* update total count and see if we need to spawn consumer thread*/
s->total+=p->free;
if((s->total>s->threshold) && 
	!(s->stop_stream & STOP_CONSUMER_THREAD)){
	/* start consumer thread */
	if(!s->consumer_thread_running && (s->consume_func!=NULL)) {
		if(pthread_create(&(s->consumer_thread_id), NULL, s->consume_func, s)!=0){
			fprintf(stderr, "packet_stream: cannot create thread (s=%p s->consume_func=%p s->total=%d): ",
				s, s->consume_func, s->total);
			perror("");
			} else {
			s->consumer_thread_running=1;
			}
		}
	/* wake up anything that may be waiting on us */
	pthread_cond_broadcast(&(s->suspend_consumer_thread));
	}
}

void discard_packets(PACKET_STREAM *s)
{
PACKET *p;
while((s->first!=NULL) && (s->first->discard) && (s->first->next!=NULL)){
	p=s->first;
	s->first=p->next;
	if(s->first!=NULL)s->first->prev=NULL;
	s->total-=p->free;
	if(p->recycle && !(s->stop_stream & STOP_PRODUCER_THREAD) && (s->unused_total<=(s->total+s->threshold+2*p->size)/2)){
		p->next=s->unused;
		p->prev=NULL;
		s->unused=p;
		s->unused_total+=p->size;
		} else
	if(p->free_func!=NULL)p->free_func(p);
	}
if((s->first!=NULL) && (s->first->discard) && (s->first==s->last)){
	p=s->first;
	s->first=NULL;
	s->last=NULL;
	s->total-=p->free;
	if(p->recycle && !(s->stop_stream & STOP_PRODUCER_THREAD) && (s->unused_total<=(s->total+s->threshold+2*p->size)/2)){
		p->next=s->unused;
		p->prev=NULL;
		s->unused=p;
		s->unused_total+=p->size;
		} else
	if(p->free_func!=NULL)p->free_func(p);
	}
while((s->unused_total>2*(s->total+s->threshold))
      ||((s->stop_stream & STOP_PRODUCER_THREAD) && (s->unused!=NULL))){
	if(s->unused==NULL){
		fprintf(stderr,"INTERNAL ERROR: unused_total non-zero (%d) while unused==NULL\n",
			s->unused_total);
		break;
		}
	p=s->unused;
	s->unused_total-=p->size;
	s->unused=p->next;
	if(p->next!=NULL){
		p->next->prev=NULL;
		}
	if(p->free_func!=NULL)p->free_func(p);
	}
}
