#ifndef __GLOBAL_H__
#define __GLOBAL_H__

#include "config.h"

void * do_alloc(long, long);
void do_free(void *);
char *get_value(int argc, char *argv[], char *key);

#endif
