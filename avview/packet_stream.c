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
p->buf=do_alloc(size, 1);
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
free(p->buf);
free(p);
}

