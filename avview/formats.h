#ifndef __FORMATS_H__
#define __FORMATS_H__

/* Borrowed and modified from pwc webcam driver by Nemosoft */

#define PUSH_RGB24	1
#define PUSH_BGR24	2
#define PUSH_RGB32	3
#define PUSH_BGR32	4

void vcvt_420i(int width, int height, int plus, unsigned char *src, unsigned char *dst, int push);
void vcvt_420i_rgb24(int width, int height, int plus, void *src, void *dst);
void vcvt_420i_bgr24(int width, int height, int plus, void *src, void *dst);
void vcvt_420i_rgb32(int width, int height, int plus, void *src, void *dst);
void vcvt_420i_bgr32(int width, int height, int plus, void *src, void *dst);

void vcvt_420p(int width, int height, int plus, unsigned char *src, unsigned char *dst, int push);
void vcvt_420p_rgb24(int width, int height, int plus, void *src, void *dst);
void vcvt_420p_bgr24(int width, int height, int plus, void *src, void *dst);
void vcvt_420p_rgb32(int width, int height, int plus, void *src, void *dst);
void vcvt_420p_bgr32(int width, int height, int plus, void *src, void *dst);

void vcvt_422(int width, int height, int plus, unsigned char *src, unsigned char *dst, int push);
void vcvt_422_rgb24(int width, int height, int plus, void *src, void *dst);
void vcvt_422_bgr24(int width, int height, int plus, void *src, void *dst);
void vcvt_422_rgb32(int width, int height, int plus, void *src, void *dst);
void vcvt_422_bgr32(int width, int height, int plus, void *src, void *dst);
void deinterlace_422_bob(long width, long height, long pitch, char *frame1, char *frame2, char *dest);
void deinterlace_422_weave(long width, long height, long pitch, char *frame1, char *frame2, char *dest);
void deinterlace_422_half_width(long width, long height, long pitch, char *frame1, char *dest);
void deinterlace_422_half_width_to_420p(long width, long height, long pitch, char *frame1, char *dest);
void deinterlace_422_bob_to_420p(long width, long height, long pitch, char *frame1, char *dest);
void convert_422_to_420p(long width, long height, long pitch, char *frame1, char *dest);


#endif
