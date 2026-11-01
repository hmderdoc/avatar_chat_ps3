/* A PNG decoder on top of inflate: 8-bit and 16-bit grey, grey+alpha, RGB,
 * RGBA and palette images, non-interlaced and Adam7. Output is packed
 * 8-bit RGB, alpha composited over black. */
#ifndef AC_PNG_H
#define AC_PNG_H
#include <stddef.h>

int png_is(const unsigned char *data, size_t n);
/* Reads the header only: 1 and the size, or 0. */
int png_size(const unsigned char *data, size_t n, int *w, int *h);
/* Decodes into rgb (w*h*3 bytes, caller-provided, see png_size). Returns 1
 * on success; err gets a short reason otherwise. Needs scratch of at
 * least png_scratch(w, h) bytes. */
size_t png_scratch(int w, int h);
int png_decode(const unsigned char *data, size_t n, unsigned char *rgb, int w, int h,
               unsigned char *scratch, size_t scratch_n, char *err, size_t errcap);

#endif
