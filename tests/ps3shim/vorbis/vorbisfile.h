#ifndef SHIM_VORBISFILE_H
#define SHIM_VORBISFILE_H
#include <stddef.h>
typedef long long ogg_int64_t;
typedef struct { size_t (*read_func)(void *, size_t, size_t, void *); int (*seek_func)(void *, ogg_int64_t, int); int (*close_func)(void *); long (*tell_func)(void *); } ov_callbacks;
typedef struct { int version; int channels; long rate; } vorbis_info;
typedef struct { int dummy; } OggVorbis_File;
#define OV_HOLE (-3)
int ov_open_callbacks(void *, OggVorbis_File *, const char *, long, ov_callbacks);
int ov_clear(OggVorbis_File *);
vorbis_info *ov_info(OggVorbis_File *, int);
long ov_read(OggVorbis_File *, char *, int, int, int, int, int *);
#endif
