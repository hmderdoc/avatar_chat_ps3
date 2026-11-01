#ifndef SHIM_FLAC_H
#define SHIM_FLAC_H
#include <stddef.h>
typedef int FLAC__bool; typedef unsigned char FLAC__byte; typedef int FLAC__int32; typedef unsigned long long FLAC__uint64;
typedef struct FLAC__StreamDecoder FLAC__StreamDecoder;
typedef struct { struct { unsigned blocksize, sample_rate, channels, bits_per_sample; } header; } FLAC__Frame;
typedef enum { FLAC__METADATA_TYPE_STREAMINFO = 0 } FLAC__MetadataType;
typedef struct { FLAC__MetadataType type; union { struct { unsigned sample_rate, channels, bits_per_sample; } stream_info; } data; } FLAC__StreamMetadata;
typedef enum { FLAC__STREAM_DECODER_READ_STATUS_CONTINUE, FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM, FLAC__STREAM_DECODER_READ_STATUS_ABORT } FLAC__StreamDecoderReadStatus;
typedef enum { FLAC__STREAM_DECODER_SEEK_STATUS_OK, FLAC__STREAM_DECODER_SEEK_STATUS_ERROR } FLAC__StreamDecoderSeekStatus;
typedef enum { FLAC__STREAM_DECODER_TELL_STATUS_OK } FLAC__StreamDecoderTellStatus;
typedef enum { FLAC__STREAM_DECODER_LENGTH_STATUS_OK } FLAC__StreamDecoderLengthStatus;
typedef enum { FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE, FLAC__STREAM_DECODER_WRITE_STATUS_ABORT } FLAC__StreamDecoderWriteStatus;
typedef enum { FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC } FLAC__StreamDecoderErrorStatus;
typedef enum { FLAC__STREAM_DECODER_INIT_STATUS_OK } FLAC__StreamDecoderInitStatus;
typedef enum { FLAC__STREAM_DECODER_SEARCH_FOR_METADATA, FLAC__STREAM_DECODER_END_OF_STREAM = 4 } FLAC__StreamDecoderState;
typedef FLAC__StreamDecoderReadStatus (*FLAC__StreamDecoderReadCallback)(const FLAC__StreamDecoder *, FLAC__byte[], size_t *, void *);
typedef FLAC__StreamDecoderSeekStatus (*FLAC__StreamDecoderSeekCallback)(const FLAC__StreamDecoder *, FLAC__uint64, void *);
typedef FLAC__StreamDecoderTellStatus (*FLAC__StreamDecoderTellCallback)(const FLAC__StreamDecoder *, FLAC__uint64 *, void *);
typedef FLAC__StreamDecoderLengthStatus (*FLAC__StreamDecoderLengthCallback)(const FLAC__StreamDecoder *, FLAC__uint64 *, void *);
typedef FLAC__bool (*FLAC__StreamDecoderEofCallback)(const FLAC__StreamDecoder *, void *);
typedef FLAC__StreamDecoderWriteStatus (*FLAC__StreamDecoderWriteCallback)(const FLAC__StreamDecoder *, const FLAC__Frame *, const FLAC__int32 *const[], void *);
typedef void (*FLAC__StreamDecoderMetadataCallback)(const FLAC__StreamDecoder *, const FLAC__StreamMetadata *, void *);
typedef void (*FLAC__StreamDecoderErrorCallback)(const FLAC__StreamDecoder *, FLAC__StreamDecoderErrorStatus, void *);
FLAC__StreamDecoder *FLAC__stream_decoder_new(void);
void FLAC__stream_decoder_delete(FLAC__StreamDecoder *);
FLAC__StreamDecoderInitStatus FLAC__stream_decoder_init_stream(FLAC__StreamDecoder *, FLAC__StreamDecoderReadCallback, FLAC__StreamDecoderSeekCallback, FLAC__StreamDecoderTellCallback, FLAC__StreamDecoderLengthCallback, FLAC__StreamDecoderEofCallback, FLAC__StreamDecoderWriteCallback, FLAC__StreamDecoderMetadataCallback, FLAC__StreamDecoderErrorCallback, void *);
FLAC__bool FLAC__stream_decoder_process_until_end_of_metadata(FLAC__StreamDecoder *);
FLAC__bool FLAC__stream_decoder_process_single(FLAC__StreamDecoder *);
FLAC__bool FLAC__stream_decoder_finish(FLAC__StreamDecoder *);
FLAC__StreamDecoderState FLAC__stream_decoder_get_state(const FLAC__StreamDecoder *);
#endif
