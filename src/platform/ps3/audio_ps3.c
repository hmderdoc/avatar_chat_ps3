/* PS3 audio: decoders from the toolchain's portlibs (libmad for MP3,
 * vorbisfile for Ogg Vorbis, libFLAC) plus the built-in WAV reader, and
 * output through the system audio port (48 kHz, stereo, float), with
 * linear resampling from the track's rate. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ppu-types.h>
#include <audio/audio.h>
#include <sysmodule/sysmodule.h>
#include <mad.h>
#include <vorbis/vorbisfile.h>
#include <FLAC/stream_decoder.h>

#include "../../platform.h"
#include "../../core/wav.h"

enum { DEC_WAV, DEC_MP3, DEC_OGG, DEC_FLAC };

struct adec {
    int kind;
    const unsigned char *data;
    size_t n, pos;
    int rate, channels;
    /* WAV */
    wav_reader wav;
    /* MP3 */
    struct mad_stream stream;
    struct mad_frame frame;
    struct mad_synth synth;
    int mad_ready, mad_i;   /* samples left in synth.pcm from mad_i */
    int mad_done;
    /* Vorbis */
    OggVorbis_File vf;
    int vf_open;
    /* FLAC */
    FLAC__StreamDecoder *flac;
    short *flac_buf;        /* the last block, interleaved */
    int flac_n, flac_i, flac_eof;
};

/* ---- memory callbacks for vorbisfile ---- */
static size_t ov_mem_read(void *ptr, size_t size, size_t nmemb, void *src) {
    adec *d = src;
    size_t want = size * nmemb, left = d->n - d->pos;
    if (want > left) want = left;
    memcpy(ptr, d->data + d->pos, want);
    d->pos += want;
    return size ? want / size : 0;
}
static int ov_mem_seek(void *src, ogg_int64_t off, int whence) {
    adec *d = src;
    ogg_int64_t to = whence == SEEK_SET ? off : whence == SEEK_CUR ? (ogg_int64_t)d->pos + off : (ogg_int64_t)d->n + off;
    if (to < 0 || to > (ogg_int64_t)d->n) return -1;
    d->pos = (size_t)to;
    return 0;
}
static int ov_mem_close(void *src) { (void)src; return 0; }
static long ov_mem_tell(void *src) { return (long)((adec *)src)->pos; }

/* ---- FLAC callbacks ---- */
static FLAC__StreamDecoderReadStatus fl_read(const FLAC__StreamDecoder *dec, FLAC__byte buffer[], size_t *bytes, void *cd) {
    adec *d = cd;
    size_t left = d->n - d->pos;
    (void)dec;
    if (*bytes > left) *bytes = left;
    if (*bytes == 0) { d->flac_eof = 1; return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM; }
    memcpy(buffer, d->data + d->pos, *bytes);
    d->pos += *bytes;
    return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}
static FLAC__StreamDecoderSeekStatus fl_seek(const FLAC__StreamDecoder *dec, FLAC__uint64 off, void *cd) {
    adec *d = cd; (void)dec;
    if (off > d->n) return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
    d->pos = (size_t)off;
    return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
}
static FLAC__StreamDecoderTellStatus fl_tell(const FLAC__StreamDecoder *dec, FLAC__uint64 *off, void *cd) { (void)dec; *off = ((adec *)cd)->pos; return FLAC__STREAM_DECODER_TELL_STATUS_OK; }
static FLAC__StreamDecoderLengthStatus fl_length(const FLAC__StreamDecoder *dec, FLAC__uint64 *len, void *cd) { (void)dec; *len = ((adec *)cd)->n; return FLAC__STREAM_DECODER_LENGTH_STATUS_OK; }
static FLAC__bool fl_eof(const FLAC__StreamDecoder *dec, void *cd) { (void)dec; return ((adec *)cd)->pos >= ((adec *)cd)->n; }
static FLAC__StreamDecoderWriteStatus fl_write(const FLAC__StreamDecoder *dec, const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *cd) {
    adec *d = cd;
    unsigned i, c, bs = frame->header.blocksize, ch = frame->header.channels, bps = frame->header.bits_per_sample;
    int shift = (int)bps - 16;
    (void)dec;
    if (ch > 2) ch = 2;
    if (!d->flac_buf) d->flac_buf = malloc(65536 * 2 * sizeof(short));
    if (!d->flac_buf || bs > 65536) return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    d->rate = (int)frame->header.sample_rate;
    d->channels = (int)ch;
    for (i = 0; i < bs; i++)
        for (c = 0; c < ch; c++) {
            FLAC__int32 v = buffer[c][i];
            v = shift >= 0 ? v >> shift : v << -shift;
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            d->flac_buf[i * ch + c] = (short)v;
        }
    d->flac_n = (int)bs;
    d->flac_i = 0;
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}
static void fl_meta(const FLAC__StreamDecoder *dec, const FLAC__StreamMetadata *m, void *cd) {
    adec *d = cd; (void)dec;
    if (m->type == FLAC__METADATA_TYPE_STREAMINFO) { d->rate = (int)m->data.stream_info.sample_rate; d->channels = (int)m->data.stream_info.channels; if (d->channels > 2) d->channels = 2; }
}
static void fl_error(const FLAC__StreamDecoder *dec, FLAC__StreamDecoderErrorStatus st, void *cd) { (void)dec; (void)st; (void)cd; }

/* ---- MP3 helpers ---- */
static short mad_to_short(mad_fixed_t s) {
    s += 1L << (MAD_F_FRACBITS - 16);
    if (s >= MAD_F_ONE) s = MAD_F_ONE - 1;
    if (s < -MAD_F_ONE) s = -MAD_F_ONE;
    return (short)(s >> (MAD_F_FRACBITS + 1 - 16));
}

/* Decode frames until one yields PCM; 0 when the stream is over. */
static int mad_next(adec *d) {
    while (!d->mad_done) {
        if (mad_frame_decode(&d->frame, &d->stream) == -1) {
            if (MAD_RECOVERABLE(d->stream.error)) continue;
            d->mad_done = 1;   /* MAD_ERROR_BUFLEN: the end */
            return 0;
        }
        mad_synth_frame(&d->synth, &d->frame);
        d->rate = (int)d->synth.pcm.samplerate;
        d->channels = d->synth.pcm.channels > 1 ? 2 : 1;
        d->mad_i = 0;
        d->mad_ready = 1;
        return 1;
    }
    return 0;
}

static int ieq_ext(const char *name, const char *ext) {
    size_t nl = name ? strlen(name) : 0, el = strlen(ext);
    size_t i;
    if (nl < el) return 0;
    for (i = 0; i < el; i++) { char a = name[nl - el + i], b = ext[i]; if (a >= 'A' && a <= 'Z') a = (char)(a + 32); if (a != b) return 0; }
    return 1;
}

adec *adec_open(const unsigned char *data, size_t n, const char *name, char *err, size_t errcap) {
    adec *d = calloc(1, sizeof *d);
    if (!d) { snprintf(err, errcap, "no memory"); return NULL; }
    d->data = data; d->n = n;
    if (wav_is(data, n)) {
        d->kind = DEC_WAV;
        if (!wav_open(&d->wav, data, n, err, errcap)) { free(d); return NULL; }
        d->rate = d->wav.rate; d->channels = d->wav.channels;
        return d;
    }
    if (n > 4 && memcmp(data, "OggS", 4) == 0) {
        ov_callbacks cb;
        cb.read_func = ov_mem_read; cb.seek_func = ov_mem_seek; cb.close_func = ov_mem_close; cb.tell_func = ov_mem_tell;
        d->kind = DEC_OGG;
        if (ov_open_callbacks(d, &d->vf, NULL, 0, cb) < 0) { snprintf(err, errcap, "not Ogg Vorbis"); free(d); return NULL; }
        d->vf_open = 1;
        {
            vorbis_info *vi = ov_info(&d->vf, -1);
            if (!vi) { ov_clear(&d->vf); free(d); snprintf(err, errcap, "bad Vorbis"); return NULL; }
            d->rate = (int)vi->rate; d->channels = vi->channels > 1 ? 2 : 1;
        }
        return d;
    }
    if (n > 4 && memcmp(data, "fLaC", 4) == 0) {
        d->kind = DEC_FLAC;
        d->flac = FLAC__stream_decoder_new();
        if (!d->flac || FLAC__stream_decoder_init_stream(d->flac, fl_read, fl_seek, fl_tell, fl_length, fl_eof, fl_write, fl_meta, fl_error, d) != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
            if (d->flac) FLAC__stream_decoder_delete(d->flac);
            free(d);
            snprintf(err, errcap, "bad FLAC");
            return NULL;
        }
        FLAC__stream_decoder_process_until_end_of_metadata(d->flac);
        if (d->rate == 0) { FLAC__stream_decoder_delete(d->flac); free(d); snprintf(err, errcap, "bad FLAC header"); return NULL; }
        return d;
    }
    if ((n > 2 && data[0] == 0xFF && (data[1] & 0xE0) == 0xE0) || (n > 3 && memcmp(data, "ID3", 3) == 0) || ieq_ext(name, ".mp3")) {
        d->kind = DEC_MP3;
        mad_stream_init(&d->stream);
        mad_frame_init(&d->frame);
        mad_synth_init(&d->synth);
        mad_stream_buffer(&d->stream, data, (unsigned long)n);
        if (!mad_next(d)) { mad_synth_finish(&d->synth); mad_frame_finish(&d->frame); mad_stream_finish(&d->stream); free(d); snprintf(err, errcap, "not an MP3"); return NULL; }
        return d;
    }
    free(d);
    snprintf(err, errcap, "unknown audio format");
    return NULL;
}

void adec_info(adec *d, int *rate, int *channels) { *rate = d->rate; *channels = d->channels; }

int adec_pull(adec *d, short *out, int max) {
    switch (d->kind) {
    case DEC_WAV: return wav_pull(&d->wav, out, max);
    case DEC_OGG: {
        int bs = 0;
        long got = ov_read(&d->vf, (char *)out, max * d->channels * 2, 1 /* big-endian: the PPU */, 2, 1, &bs);
        if (got < 0) return got == OV_HOLE ? 0 : -1;
        return (int)(got / (d->channels * 2));
    }
    case DEC_FLAC: {
        int n = 0;
        while (n < max) {
            if (d->flac_i >= d->flac_n) {
                if (d->flac_eof || FLAC__stream_decoder_get_state(d->flac) == FLAC__STREAM_DECODER_END_OF_STREAM) break;
                if (!FLAC__stream_decoder_process_single(d->flac)) return n ? n : -1;
                if (d->flac_i >= d->flac_n) { if (FLAC__stream_decoder_get_state(d->flac) == FLAC__STREAM_DECODER_END_OF_STREAM) break; continue; }
            }
            {
                int take = d->flac_n - d->flac_i;
                if (take > max - n) take = max - n;
                memcpy(out + n * d->channels, d->flac_buf + d->flac_i * d->channels, (size_t)take * d->channels * sizeof(short));
                d->flac_i += take;
                n += take;
            }
        }
        return n;
    }
    case DEC_MP3: {
        int n = 0;
        while (n < max) {
            if (!d->mad_ready || d->mad_i >= (int)d->synth.pcm.length) {
                d->mad_ready = 0;
                if (!mad_next(d)) break;
            }
            while (n < max && d->mad_i < (int)d->synth.pcm.length) {
                out[n * d->channels] = mad_to_short(d->synth.pcm.samples[0][d->mad_i]);
                if (d->channels == 2) out[n * 2 + 1] = mad_to_short(d->synth.pcm.samples[d->synth.pcm.channels > 1 ? 1 : 0][d->mad_i]);
                d->mad_i++;
                n++;
            }
        }
        return n;
    }
    default: return -1;
    }
}

void adec_close(adec *d) {
    if (!d) return;
    switch (d->kind) {
    case DEC_OGG: if (d->vf_open) ov_clear(&d->vf); break;
    case DEC_FLAC: if (d->flac) { FLAC__stream_decoder_finish(d->flac); FLAC__stream_decoder_delete(d->flac); } free(d->flac_buf); break;
    case DEC_MP3: mad_synth_finish(&d->synth); mad_frame_finish(&d->frame); mad_stream_finish(&d->stream); break;
    default: break;
    }
    free(d);
}

/* ---- output: the system audio port ---- */

#define OUT_RATE 48000
static u32 g_port;
static audioPortConfig g_cfg;
static int g_audio_open, g_src_rate, g_src_ch;
static u64 g_next_block;        /* the block we fill next */
static float g_last_l, g_last_r;
static double g_phase;          /* position in source frames, fractional */
static short g_pending[2];      /* the last source frame, for interpolation */
static int g_have_pending;

static volatile u64 *read_index(void) { return (volatile u64 *)(u64)g_cfg.readIndex; }

int plat_audio_open(int rate, int channels) {
    audioPortParam param;
    static int inited;
    if (!inited) {
        sysModuleLoad(SYSMODULE_AUDIO);
        if (audioInit() != 0) { plat_log("audioInit failed"); return -1; }
        inited = 1;
    }
    if (g_audio_open) plat_audio_close();
    memset(&param, 0, sizeof param);
    param.numChannels = AUDIO_PORT_2CH;
    param.numBlocks = AUDIO_BLOCK_16;
    param.attrib = AUDIO_PORT_INITLEVEL;
    param.level = 1.0f;
    if (audioPortOpen(&param, &g_port) != 0) { plat_log("audioPortOpen failed"); return -1; }
    if (audioGetPortConfig(g_port, &g_cfg) != 0) { plat_log("audioGetPortConfig failed"); audioPortClose(g_port); return -1; }
    memset((void *)(u64)g_cfg.audioDataStart, 0, g_cfg.portSize);
    if (audioPortStart(g_port) != 0) { plat_log("audioPortStart failed"); audioPortClose(g_port); return -1; }
    g_src_rate = rate; g_src_ch = channels;
    g_next_block = (*read_index() + 1) % g_cfg.numBlocks;
    g_phase = 0; g_have_pending = 0; g_last_l = g_last_r = 0;
    g_audio_open = 1;
    plat_log("audio port %u: %u blocks, data at 0x%x", (unsigned)g_port, (unsigned)g_cfg.numBlocks, (unsigned)g_cfg.audioDataStart);
    return 0;
}

/* Blocks we may fill: everything between the next block and the one the
 * hardware is reading. */
static int free_blocks(void) {
    u64 cur = *read_index();
    u64 nb = g_cfg.numBlocks;
    u64 ahead = (g_next_block + nb - cur) % nb;   /* blocks already queued */
    return (int)(nb - 1 - ahead);
}

int plat_audio_space(void) {
    int blocks;
    if (!g_audio_open) return 0;
    blocks = free_blocks();
    if (blocks <= 0) return 0;
    /* Source frames that fill those blocks at the output rate. */
    return (int)((long long)blocks * AUDIO_BLOCK_SAMPLES * g_src_rate / OUT_RATE);
}

int plat_audio_write(const short *pcm, int frames) {
    int used = 0;
    if (!g_audio_open || frames <= 0) return 0;
    while (free_blocks() > 0) {
        float *blk = (float *)(u64)(g_cfg.audioDataStart + (u32)(g_next_block * AUDIO_BLOCK_SAMPLES * 2 * sizeof(float)));
        int i;
        for (i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
            /* Linear interpolation between source frames at g_phase. */
            int idx = (int)g_phase;
            float frac = (float)(g_phase - idx), l, r;
            short a_l, a_r, b_l, b_r;
            if (idx >= frames) { /* out of input for this block: pad with the last sample */
                if (used < frames) used = frames;
                blk[i * 2] = g_last_l; blk[i * 2 + 1] = g_last_r;
                continue;
            }
            a_l = pcm[idx * g_src_ch]; a_r = pcm[idx * g_src_ch + (g_src_ch == 2 ? 1 : 0)];
            if (idx + 1 < frames) { b_l = pcm[(idx + 1) * g_src_ch]; b_r = pcm[(idx + 1) * g_src_ch + (g_src_ch == 2 ? 1 : 0)]; }
            else { b_l = a_l; b_r = a_r; }
            l = (a_l + (b_l - a_l) * frac) / 32768.0f;
            r = (a_r + (b_r - a_r) * frac) / 32768.0f;
            blk[i * 2] = l; blk[i * 2 + 1] = r;
            g_last_l = l; g_last_r = r;
            g_phase += (double)g_src_rate / OUT_RATE;
            used = idx + 1;
        }
        g_next_block = (g_next_block + 1) % g_cfg.numBlocks;
        if (used >= frames) break;
    }
    /* Keep the fractional position relative to the frames consumed. */
    g_phase -= used;
    if (g_phase < 0) g_phase = 0;
    return used;
}

void plat_audio_close(void) {
    if (!g_audio_open) return;
    audioPortStop(g_port);
    audioPortClose(g_port);
    g_audio_open = 0;
}
