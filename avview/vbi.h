#ifndef __VBI_H__
#define __VBI_H__

#include <libzvbi.h>

typedef struct {
	vbi_capture * cap;
	vbi_raw_decoder * par;
	} VBI_DATA;

#endif
