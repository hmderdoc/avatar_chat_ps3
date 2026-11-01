/* GIF: the first frame, composed over the logical screen, as packed RGB. */
#ifndef AC_GIF_H
#define AC_GIF_H
#include <stddef.h>

int gif_is(const unsigned char *data, size_t n);
int gif_size(const unsigned char *data, size_t n, int *w, int *h);
/* Decodes the first image into rgb (w*h*3, caller-provided). Needs scratch
 * of gif_scratch() bytes. 1 on success. */
size_t gif_scratch(void);
int gif_decode(const unsigned char *data, size_t n, unsigned char *rgb, int w, int h,
               unsigned char *scratch, size_t scratch_n, char *err, size_t errcap);

#endif
