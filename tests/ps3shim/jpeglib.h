#ifndef SHIM_JPEGLIB_H
#define SHIM_JPEGLIB_H
#include <stddef.h>
typedef int boolean;
#define TRUE 1
typedef unsigned int JDIMENSION;
typedef unsigned char JSAMPLE, *JSAMPROW, **JSAMPARRAY;
typedef enum { JCS_RGB = 2 } J_COLOR_SPACE;
struct jpeg_common_struct { struct jpeg_error_mgr *err; };
typedef struct jpeg_common_struct *j_common_ptr;
struct jpeg_error_mgr { void (*error_exit)(j_common_ptr); void (*output_message)(j_common_ptr); };
struct jpeg_decompress_struct;
typedef struct jpeg_decompress_struct *j_decompress_ptr;
struct jpeg_source_mgr { const unsigned char *next_input_byte; size_t bytes_in_buffer; void (*init_source)(j_decompress_ptr); boolean (*fill_input_buffer)(j_decompress_ptr); void (*skip_input_data)(j_decompress_ptr, long); boolean (*resync_to_restart)(j_decompress_ptr, int); void (*term_source)(j_decompress_ptr); };
boolean jpeg_resync_to_restart(j_decompress_ptr, int);
struct jpeg_decompress_struct { struct jpeg_error_mgr *err; struct jpeg_source_mgr *src; JDIMENSION image_width, image_height, output_width, output_height, output_scanline; J_COLOR_SPACE out_color_space; unsigned int scale_num, scale_denom; };
struct jpeg_error_mgr *jpeg_std_error(struct jpeg_error_mgr *);
void jpeg_create_decompress(j_decompress_ptr);
void jpeg_destroy_decompress(j_decompress_ptr);
int jpeg_read_header(j_decompress_ptr, boolean);
boolean jpeg_start_decompress(j_decompress_ptr);
JDIMENSION jpeg_read_scanlines(j_decompress_ptr, JSAMPARRAY, JDIMENSION);
boolean jpeg_finish_decompress(j_decompress_ptr);
#endif
