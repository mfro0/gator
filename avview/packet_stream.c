/*     avview preliminary version

       (C) Vladimir Dergachev 2001-2004
       
       GNU Public License
       
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "packet_stream.h"
#include "global.h"

typedef void *(*pthread_start_fn)(void *);

PACKET_STREAM *new_packet_stream(void)
{
PACKET_STREAM *s;
int i;

s=do_alloc(1, sizeof(PACKET_STREAM));
for(i=0;i<2;i++){
	s->size[i]=100;
	s->free[i]=0;
	s->bottom[i]=0;
	s->stack[i]=do_alloc(s->size[i], sizeof((*s->stack[i])));
	}
s->read_from=0;
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

p=do_alloc(1, sizeof(PACKET));
p->size=size;
p->free=0;
p->use_count=0;
if(size>0){
	p->buf=do_alloc(size,1);
	while(p->buf==NULL){
		sleep(1);
		p->buf=do_alloc(size, 1);
		}
	} else p->buf=NULL;
p->free_func=free_generic_packet;
p->type="GENERIC";
p->priv=NULL;
return p;
}

void free_generic_packet(PACKET *p)
{
p->use_count--;
if(p->use_count>0)return;
if(p->use_count<0){
	fprintf(stderr,"Freeing packet with negative use count\n");
	}
p->size=0;
p->free=0;
if(p->buf!=NULL)do_free(p->buf);
do_free(p);
}

void start_consumer_thread(PACKET_STREAM *s)
{
if(!s->consumer_thread_running && (s->consume_func!=NULL)) {
	if(pthread_create(&(s->consumer_thread_id), NULL, (pthread_start_fn)s->consume_func, s)!=0){
		fprintf(stderr, "packet_stream: cannot create thread (s=%p s->consume_func=%p s->total=%d): ",
			s, s->consume_func, s->total);
		perror("");
		} else {
		s->consumer_thread_running=1;
		}
	} else {
	pthread_cond_broadcast(&(s->suspend_consumer_thread));
	}
}

void deliver_packet(PACKET_STREAM *s, PACKET *p)
{
long i;
/* put the packet into the queue */
i=!s->read_from;
if(s->free[i]>=s->size[i]){
	PACKET **pp;
	s->size[i]=s->size[i]*2+10;
	pp=do_alloc(s->size[i], sizeof(*p));
	if(s->free[i]>0)memcpy(pp, s->stack[i], s->free[i]*sizeof(*pp));
	free(s->stack[i]);
	s->stack[i]=pp;
	}
s->stack[i][s->free[i]]=p;
s->free[i]++;
/* update total count and see if we need to spawn consumer thread*/
s->total+=p->free;
if((s->total>s->threshold) && 
	!(s->stop_stream & STOP_CONSUMER_THREAD)){
	/* start consumer thread */
	start_consumer_thread(s);
	/* wake up anything that may be waiting on us */
	pthread_cond_broadcast(&(s->suspend_consumer_thread));
	}
}

int packet_count(PACKET_STREAM *s)
{
return (s->free[0]-s->bottom[0]+s->free[1]-s->bottom[1]);
}

PACKET * get_packet(PACKET_STREAM *s)
{
int i;
PACKET *p;
i=s->read_from;
if(s->bottom[i]==s->free[i]){
	s->bottom[i]=0;
	s->free[i]=0;
	i=!i;	
	s->read_from=i;
	}
if(s->bottom[i]==s->free[i])return NULL;
p=s->stack[i][s->bottom[i]];
s->bottom[i]++;
s->total-=p->free;
return p;
}

void trim_excess_consumer(PACKET_STREAM *s)
{
PACKET *f;
pthread_mutex_lock(&(s->ctr_mutex));
f=get_packet(s);
while((f!=NULL)&&(s->total>(s->threshold+f->free))){
	f->free_func(f);
	f=get_packet(s);
	}
s->consumer_thread_running=0;
pthread_mutex_unlock(&(s->ctr_mutex));
pthread_exit(NULL);
}
