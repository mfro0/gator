#include "global.h"
#include "formats.h"

/* Borrowed and modified from pwc webcam driver by Nemosoft */

/**
  \brief convert YUV 4:2:0 data into RGB, BGR, RGBa or BGRa
  \param width Width of yuv data, in pixels
  \param height Height of yuv data, in pixels
  \param plus Width of viewport, in pixels
  \param src beginning of YUV data
  \param dst beginning of RGB data, \b including the initial offset into the viewport
  \param push The requested RGB format 

  \e push can be any of PUSH_RGB24, PUSH_BGR24, PUSH_RGB32 or PUSH_BGR32
  
 This is a really simplistic approach. Speedups are welcomed. 
*/
void vcvt_420i(int width, int height, int plus, unsigned char *src, unsigned char *dst, int push)
{
	int line, col, linewidth;
	int y, u, v, yy, vr = 0, ug = 0, vg = 0, ub = 0;
	int r, g, b;
	unsigned char *sy, *su, *sv;

	linewidth = width + (width >> 1);
	sy = src;
	su = sy + 4;
	sv = su + linewidth;

	/* The biggest problem is the interlaced data, and the fact that odd
	   add even lines have V and U data, resp. 
	 */
	for (line = 0; line < height; line++) {
		for (col = 0; col < width; col++) {
			y = *sy++;
			yy = y << 8;
			if ((col & 1) == 0) {
				/* only at even colums we update the u/v data */
				u = *su - 128;
				ug =   88 * u;
				ub =  454 * u;
				v = *sv - 128;
				vg =  183 * v;
				vr =  359 * v;

				su++;
				sv++;
			}
			if ((col & 3) == 3) {
				sy += 2; /* skip u/v */
				su += 4; /* skip y */
				sv += 4; /* skip y */
			}

			r = (yy +      vr) >> 8;
			g = (yy - ug - vg) >> 8;
			b = (yy + ub     ) >> 8;
			/* At moments like this, you crave for MMX instructions with saturation */
			if (r <   0) r =   0;
			if (r > 255) r = 255;
			if (g <   0) g =   0;
			if (g > 255) g = 255;
			if (b <   0) b =   0;
			if (b > 255) b = 255;
			
			switch(push) {
			case PUSH_RGB24:
				*dst++ = r;
				*dst++ = g;
				*dst++ = b;
				break;

			case PUSH_BGR24:
				*dst++ = b;
				*dst++ = g;
				*dst++ = r;
				break;
			
			case PUSH_RGB32:
				*dst++ = r;
				*dst++ = g;
				*dst++ = b;
				*dst++ = 0;
				break;

			case PUSH_BGR32:
				*dst++ = b;
				*dst++ = g;
				*dst++ = r;
				*dst++ = 0;
				break;
			}
		} /* ..for col */
		if (line & 1) { // odd line: go to next band
			su += linewidth;
			sv += linewidth;
		}
		else { // rewind u/v pointers
			su -= linewidth;
			sv -= linewidth;
		}
		/* Adjust destination pointer, using viewport. We have just
		   filled one line worth of data, so only skip the difference
		   between the view width and the image width.
		 */
		if (push == PUSH_RGB24 || push == PUSH_BGR24)
			dst += ((plus - width) * 3);
		else
			dst += ((plus - width) * 4);
	} /* ..for line */
}

void vcvt_420i_rgb24(int width, int height, int plus, void *src, void *dst)
{
	vcvt_420i(width, height, plus, (unsigned char *)src, (unsigned char *)dst, PUSH_RGB24);
}

void vcvt_420i_bgr24(int width, int height, int plus, void *src, void *dst)
{
	vcvt_420i(width, height, plus, (unsigned char *)src, (unsigned char *)dst, PUSH_BGR24);
}

void vcvt_420i_rgb32(int width, int height, int plus, void *src, void *dst)
{
	vcvt_420i(width, height, plus, (unsigned char *)src, (unsigned char *)dst, PUSH_RGB32);
}

void vcvt_420i_bgr32(int width, int height, int plus, void *src, void *dst)
{
	vcvt_420i(width, height, plus, (unsigned char *)src, (unsigned char *)dst, PUSH_BGR32);
}

/**
  \brief convert YUV 4:2:0 data into RGB, BGR, RGBa or BGRa
  \param width Width of yuv data, in pixels
  \param height Height of yuv data, in pixels
  \param plus Width of viewport, in pixels
  \param src beginning of YUV data
  \param dst beginning of RGB data, \b including the initial offset into the viewport
  \param push The requested RGB format 

  \e push can be any of PUSH_RGB24, PUSH_BGR24, PUSH_RGB32 or PUSH_BGR32
  
 This is a really simplistic approach. Speedups are welcomed. 
*/
void vcvt_420p(int width, int height, int plus, unsigned char *src, unsigned char *dst, int push)
{
	int line, col, uv_linewidth;
	int y, u, v, yy, vr = 0, ug = 0, vg = 0, ub = 0;
	int r, g, b;
	unsigned char *sy, *su, *sv;

	uv_linewidth = (width >> 1);
	sy = src;
	su = sy + ((width*height));
	sv = su + ((width*height)>>2);

	/* The biggest problem is the interlaced data, and the fact that odd
	   add even lines have V and U data, resp. 
	 */
	for (line = 0; line < height; line++) {
		for (col = 0; col < width; col++) {
			y = *sy++;
			yy = y << 8;
			if ((col & 1) == 0) {
				/* only at even colums we update the u/v data */
				u = *su - 128;
				ug =   88 * u;
				ub =  454 * u;
				v = *sv - 128;
				vg =  183 * v;
				vr =  359 * v;

				su++;
				sv++;
			}
			#if 0
			if ((col & 3) == 3) {
				sy += 2; /* skip u/v */
				su += 4; /* skip y */
				sv += 4; /* skip y */
			}
			#endif
			r = (yy +      vr) >> 8;
			g = (yy - ug - vg) >> 8;
			b = (yy + ub     ) >> 8;
			/* At moments like this, you crave for MMX instructions with saturation */
			if (r <   0) r =   0;
			if (r > 255) r = 255;
			if (g <   0) g =   0;
			if (g > 255) g = 255;
			if (b <   0) b =   0;
			if (b > 255) b = 255;
			
			switch(push) {
			case PUSH_RGB24:
				*dst++ = r;
				*dst++ = g;
				*dst++ = b;
				break;

			case PUSH_BGR24:
				*dst++ = b;
				*dst++ = g;
				*dst++ = r;
				break;
			
			case PUSH_RGB32:
				*dst++ = r;
				*dst++ = g;
				*dst++ = b;
				*dst++ = 0;
				break;

			case PUSH_BGR32:
				*dst++ = b;
				*dst++ = g;
				*dst++ = r;
				*dst++ = 0;
				break;
			}
		} /* ..for col */
		if (line & 1) { // odd line: go to next band
		}
		else { // rewind u/v pointers
			su -= uv_linewidth;
			sv -= uv_linewidth;
		}
		/* Adjust destination pointer, using viewport. We have just
		   filled one line worth of data, so only skip the difference
		   between the view width and the image width.
		 */
		if (push == PUSH_RGB24 || push == PUSH_BGR24)
			dst += ((plus - width) * 3);
		else
			dst += ((plus - width) * 4);
	} /* ..for line */
}

void vcvt_420p_rgb24(int width, int height, int plus, void *src, void *dst)
{
	vcvt_420p(width, height, plus, (unsigned char *)src, (unsigned char *)dst, PUSH_RGB24);
}

void vcvt_420p_bgr24(int width, int height, int plus, void *src, void *dst)
{
	vcvt_420p(width, height, plus, (unsigned char *)src, (unsigned char *)dst, PUSH_BGR24);
}

void vcvt_420p_rgb32(int width, int height, int plus, void *src, void *dst)
{
	vcvt_420p(width, height, plus, (unsigned char *)src, (unsigned char *)dst, PUSH_RGB32);
}

void vcvt_420p_bgr32(int width, int height, int plus, void *src, void *dst)
{
	vcvt_420p(width, height, plus, (unsigned char *)src, (unsigned char *)dst, PUSH_BGR32);
}

/**
  \brief convert YUV 4:2:2 data into RGB, BGR, RGBa or BGRa
  \param width Width of yuv data, in pixels
  \param height Height of yuv data, in pixels
  \param plus Width of viewport, in pixels
  \param src beginning of YUV data
  \param dst beginning of RGB data, \b including the initial offset into the viewport
  \param push The requested RGB format 

  \e push can be any of PUSH_RGB24, PUSH_BGR24, PUSH_RGB32 or PUSH_BGR32
  
 This is a really simplistic approach. Speedups are welcomed. 
*/
void vcvt_422(int width, int height, int plus, unsigned char *src, unsigned char *dst, int push)
{
	int line, col, pitch;
	int y, u, v, yy, vr = 0, ug = 0, vg = 0, ub = 0;
	int r, g, b;
	unsigned char *sy, *su, *sv;

	pitch=width*2;

	for (line = 0; line < height; line++) {
		sy = src+line*pitch;
		su = sy + 1;
		sv = sy + 3;
		for (col = 0; col < width; col++) {
			yy = (*sy) << 8;
			sy+=2;
			if ((col & 1) == 0) {
				/* only at even colums we update the u/v data */
				u = *su - 128;
				ug =   88 * u;
				ub =  454 * u;
				v = *sv - 128;
				vg =  183 * v;
				vr =  359 * v;

				su+=4;
				sv+=4;
			}
			#if 0
			if ((col & 3) == 3) {
				sy += 2; /* skip u/v */
				su += 4; /* skip y */
				sv += 4; /* skip y */
			}
			#endif
			r = (yy +      vr) >> 8;
			g = (yy - ug - vg) >> 8;
			b = (yy + ub     ) >> 8;
			/* At moments like this, you crave for MMX instructions with saturation */
			if (r <   0) r =   0;
			if (r > 255) r = 255;
			if (g <   0) g =   0;
			if (g > 255) g = 255;
			if (b <   0) b =   0;
			if (b > 255) b = 255;
			
			switch(push) {
			case PUSH_RGB24:
				*dst++ = r;
				*dst++ = g;
				*dst++ = b;
				break;

			case PUSH_BGR24:
				*dst++ = b;
				*dst++ = g;
				*dst++ = r;
				break;
			
			case PUSH_RGB32:
				*dst++ = r;
				*dst++ = g;
				*dst++ = b;
				*dst++ = 0;
				break;

			case PUSH_BGR32:
				*dst++ = b;
				*dst++ = g;
				*dst++ = r;
				*dst++ = 0;
				break;
			}
		} /* ..for col */
		/* Adjust destination pointer, using viewport. We have just
		   filled one line worth of data, so only skip the difference
		   between the view width and the image width.
		 */
		if ((push == PUSH_RGB24) || (push == PUSH_BGR24))
			dst += ((plus - width) * 3);
		else
			dst += ((plus - width) * 4);
	} /* ..for line */
}

void vcvt_422_rgb24(int width, int height, int plus, void *src, void *dst)
{
	vcvt_422(width, height, plus, (unsigned char *)src, (unsigned char *)dst, PUSH_RGB24);
}

void vcvt_422_bgr24(int width, int height, int plus, void *src, void *dst)
{
	vcvt_422(width, height, plus, (unsigned char *)src, (unsigned char *)dst, PUSH_BGR24);
}

void vcvt_422_rgb32(int width, int height, int plus, void *src, void *dst)
{
	vcvt_422(width, height, plus, (unsigned char *)src, (unsigned char *)dst, PUSH_RGB32);
}

void vcvt_422_bgr32(int width, int height, int plus, void *src, void *dst)
{
	vcvt_422(width, height, plus, (unsigned char *)src, (unsigned char *)dst, PUSH_BGR32);
}

/* a rather simplistic deinterlacing.. for now */
void deinterlace_422_bob(long width, long height, long pitch, char *frame1, char *frame2, char *dest)
{
long line;
long dst_pitch;
char *t1,*t2;
char *s1,*s2;
dst_pitch=width*2;
for(line=height-1;line>=0;line--){
	s1=frame1+line*pitch;
	s2=frame2+line*pitch;
	t1=dest+line*dst_pitch*2;
	t2=t1+dst_pitch;
	memcpy(t1,s1,pitch);
	memcpy(t2,s1,pitch);
	}
}

/* a rather simplistic deinterlacing.. for now */
void deinterlace_422_weave(long width, long height, long pitch, char *frame1, char *frame2, char *dest)
{
long line;
long dst_pitch;
char *t1,*t2;
char *s1,*s2;
dst_pitch=width*2;
for(line=height-1;line>=0;line--){
	s1=frame1+line*pitch;
	s2=frame2+line*pitch;
	t2=dest+line*dst_pitch*2;
	t1=t2+dst_pitch;
	memcpy(t1,s1,pitch);
	memcpy(t2,s2,pitch);
	}
}

void deinterlace_422_half_width(long width, long height, long pitch, char *frame1, char *dest)
{
long line;
long pixel;
long dst_pitch;
unsigned char *t1;
unsigned char *s1;
int y1,y2,y3,y4,v1,v2,u1,u2;
dst_pitch=width & (~3);
for(line=height-1;line>=0;line--){
	s1=frame1+line*pitch;
	t1=dest+line*dst_pitch;
	for(pixel=0;pixel<width;pixel+=4){
		y1=(*s1++);
		u1=(*s1++);
		y2=(*s1++);
		v1=(*s1++);
		y3=(*s1++);
		u2=(*s1++);
		y4=(*s1++);
		v2=(*s1++);
		(*t1++)=(y1+y2)>>1;
		(*t1++)=(u1+u2)>>1;
		(*t1++)=(y3+y4)>>1;
		(*t1++)=(v1+v2)>>1;
		}
	}
}

/* this replicates Y */
void deinterlace_422_bob_to_420p(long width, long height, long pitch, char *frame1, char *dest)
{
long line;
long pixel;
long dst_pitch;
unsigned char *t1,*t2,*t3;
unsigned char *s1;
char y1,y2,v1,u1;
t1=dest;
t2=dest+(width*height*2);
t3=t2+(width*height*2)/4;
for(line=0;line<(height*2);line++){
	s1=frame1+(line/2)*pitch;
	for(pixel=0;pixel<width;pixel+=2){
		y1=(*s1++);
		u1=(*s1++);
		y2=(*s1++);
		v1=(*s1++);
		(*t1++)=y1;
		(*t1++)=y2;
		if(line & 1){
			(*t3++)=v1;
			} else {
			(*t2++)=u1;
			}
		}
	}
}


void deinterlace_422_half_width_to_420p(long width, long height, long pitch, char *frame1, char *dest)
{
long line;
long pixel;
long dst_pitch;
unsigned char *t1,*t2,*t3;
unsigned char *s1;
int y1,y2,y3,y4,v1,v2,u1,u2;
t1=dest;
t2=dest+((width/2)*height);
t3=t2+((width/2)*height)/4;
for(line=0;line<height;line++){
	s1=frame1+line*pitch;
	for(pixel=0;pixel<width;pixel+=4){
		y1=(*s1++);
		u1=(*s1++);
		y2=(*s1++);
		v1=(*s1++);
		y3=(*s1++);
		u2=(*s1++);
		y4=(*s1++);
		v2=(*s1++);
		(*t1++)=(y1+y2)>>1;
		(*t1++)=(y3+y4)>>1;
		if(line & 1){
			(*t3++)=(v1+v2)>>1;
			} else {
			(*t2++)=(u1+u2)>>1;
			}
		}
	}
}

/* this replicates discard U and V alternatively */
void convert_422_to_420p(long width, long height, long pitch, char *frame1, char *dest)
{
long line;
long pixel;
long dst_pitch;
unsigned char *t1,*t2,*t3;
unsigned char *s1;
char y1,y2,v1,u1;
t1=dest;
t2=dest+(width*height);
t3=t2+(width*height)/4;
for(line=0;line<height;line++){
	s1=frame1+line*pitch;
	for(pixel=0;pixel<width;pixel+=2){
		y1=(*s1++);
		u1=(*s1++);
		y2=(*s1++);
		v1=(*s1++);
		(*t1++)=y1;
		(*t1++)=y2;
		if(line & 1){
			(*t3++)=v1;
			} else {
			(*t2++)=u1;
			}
		}
	}
}
