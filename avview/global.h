#ifndef __GLOBAL_H__
#define __GLOBAL_H__

#include "config.h"

typedef long long int64;

void * do_alloc(long, long);
void do_free(void *);
char *get_value(int argc, char *argv[], char *key);

#endif
