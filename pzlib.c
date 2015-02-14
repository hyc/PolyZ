/* pzlib.c - fake zlib wrapper to invoke other compression APIs
 * Copyright 2014-2015 Howard Chu @ Symas Corp.
 */
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef uLong (bfunc)(uLong len);
typedef int (cfunc)(Bytef *out, uLongf *outlen, const Bytef *in, uLong inlen, int level);
typedef int (ufunc)(Bytef *out, uLongf *outlen, const Bytef *in, uLong inlen);

typedef struct codec_t {
	bfunc *bound;
	cfunc *comp;
	ufunc *uncomp;
} codec_t;

typedef struct ctable_t {
	const char *name;
	const codec_t *cptr;
} ctable_t;

static bfunc def_bfunc;
static cfunc def_cfunc;
static ufunc def_ufunc;

static const codec_t def_codec = { def_bfunc, def_cfunc, def_ufunc };
static codec_t *codec = (codec_t *)&def_codec;

uLong compressBound(uLong len){
	return codec->bound(len)+1;	/* 1 for fake zlib header */
}

int compress2(Bytef *out, uLongf *outlen, const Bytef *in, uLong inlen, int level) {
	int rc;
	*out = 8;	/* fake zlib header byte */
	(*outlen)--;
	rc = codec->comp(out+1, outlen, in, inlen, level);
	(*outlen)++;
	return rc;
}

int compress(Bytef *out, uLongf *outlen, const Bytef *in, uLong inlen) {
	return compress2(out, outlen, in, inlen, Z_DEFAULT_COMPRESSION);
}

int uncompress(Bytef *out, uLongf *outlen, const Bytef *in, uLong inlen) {
	return codec->uncomp(out, outlen, in+1, inlen-1);
}

#ifndef USE_BZIP2
#define USE_BZIP2	1
#endif
#if USE_BZIP2
#include <bzlib.h>

static uLong bzip2_bfunc(uLong len) {
	return len + 32;
}

static int bzip2_err(int err) {
	switch(err) {
	case BZ_OK:	return Z_OK;
	case BZ_MEM_ERROR:	return Z_MEM_ERROR;
	case BZ_DATA_ERROR:	return Z_DATA_ERROR;
	case BZ_OUTBUFF_FULL:	return Z_BUF_ERROR;
	default:	return Z_VERSION_ERROR;
	}
}

static int bzip2_cfunc(Bytef *out, uLongf *outlen, const Bytef *in, uLong inlen, int level) {
	unsigned int dstlen;
	int rc;
	dstlen = *outlen;
	rc = BZ2_bzBuffToBuffCompress(out, &dstlen, (char *)in, inlen, level, 0, 0);
	*outlen = dstlen;
	return bzip2_err(rc);
}

static int bzip2_ufunc(Bytef *out, uLongf *outlen, const Bytef *in, uLong inlen) {
	unsigned int destlen = *outlen;
	int rc = BZ2_bzBuffToBuffDecompress(out, &destlen, (char *)in, inlen, 0, 0);
	*outlen = destlen;
	return bzip2_err(rc);
}

static const codec_t c_bzip2 = {
	bzip2_bfunc, bzip2_cfunc, bzip2_ufunc
};
#endif

#ifndef USE_LZ4
#define USE_LZ4	1
#endif
#if USE_LZ4
#include <lz4.h>

static uLong lz4_bfunc(uLong len) {
	return LZ4_compressBound(len);
}

static int lz4_cfunc(Bytef *out, uLongf *outlen, const Bytef *in, uLong inlen, int level) {
	*outlen = LZ4_compress(in, out, inlen);
	return *outlen ? 0 : Z_DATA_ERROR;
}

static int lz4_ufunc(Bytef *out, uLongf *outlen, const Bytef *in, uLong inlen) {
	int rc =  LZ4_decompress_safe(in, out, inlen, *outlen);
	if (rc < 0)
		return Z_DATA_ERROR;
	*outlen = rc;
	return 0;
}

static const codec_t c_lz4 = {
	lz4_bfunc, lz4_cfunc, lz4_ufunc
};

#include <lz4hc.h>
static int lz4hc_cfunc(Bytef *out, uLongf *outlen, const Bytef *in, uLong inlen, int level) {
	*outlen = LZ4_compressHC(in, out, inlen);
	return *outlen ? 0 : Z_DATA_ERROR;
}

static const codec_t c_lz4hc = {
	lz4_bfunc, lz4hc_cfunc, lz4_ufunc
};

#endif

#ifndef USE_LZMA
#define USE_LZMA	1
#endif
#if USE_LZMA
#include <lzma.h>
static uLong lzma_bfunc(uLong len) {
	return lzma_stream_buffer_bound(len);
}

static int lzma_err(int err) {
	switch(err) {
	case LZMA_OK: return Z_OK;
	case LZMA_STREAM_END:	return Z_STREAM_END;
	case LZMA_MEM_ERROR:	return Z_MEM_ERROR;
	case LZMA_FORMAT_ERROR:	return Z_DATA_ERROR;
	case LZMA_DATA_ERROR:	return Z_DATA_ERROR;
	case LZMA_BUF_ERROR:	return Z_BUF_ERROR;
	default: return Z_VERSION_ERROR;
	}
}
static int lzma_cfunc(Bytef *out, uLongf *outlen, const Bytef *in, uLong inlen, int level) {
	size_t outpos = 0;
	int rc = lzma_easy_buffer_encode(level, LZMA_CHECK_NONE,  NULL, in, inlen,
		out, &outpos, *outlen);
	*outlen = outpos;
	return lzma_err(rc);
}

static int lzma_ufunc(Bytef *out, uLongf *outlen, const Bytef *in, uLong inlen) {
	uint64_t memlimit = UINT64_MAX;
	size_t inpos = 0;
	size_t outpos = 0;
	int rc = lzma_stream_buffer_decode(&memlimit, 0, NULL, in, &inpos,
		inlen, out, &outpos, *outlen);
	*outlen = outpos;
	return lzma_err(rc);
}


static const codec_t c_lzma = {
	lzma_bfunc, lzma_cfunc, lzma_ufunc
};

#endif

#ifndef USE_LZO
#define USE_LZO	1
#endif
#if USE_LZO
#include <lzo/lzo1x.h>
static uLong lzo_bfunc(uLong len) {
	return len + 16;
}

static int lzo_err(int err) {
	switch(err) {
	case LZO_E_OK: return Z_OK;
	case LZO_E_ERROR:	return Z_ERRNO;
	case LZO_E_OUT_OF_MEMORY:	return Z_MEM_ERROR;
	case LZO_E_NOT_COMPRESSIBLE:	return Z_DATA_ERROR;
	case LZO_E_INPUT_OVERRUN:	return Z_DATA_ERROR;
	case LZO_E_OUTPUT_OVERRUN:	return Z_BUF_ERROR;
	case LZO_E_EOF_NOT_FOUND:	return Z_DATA_ERROR;
	case LZO_E_INPUT_NOT_CONSUMED:	return Z_DATA_ERROR;
	case LZO_E_NOT_YET_IMPLEMENTED:	return Z_VERSION_ERROR;
	case LZO_E_INVALID_ARGUMENT:	return Z_DATA_ERROR;
	default: return Z_VERSION_ERROR;
	}
}

static int lzo_cfunc(Bytef *out, uLongf *outlen, const Bytef *in, uLong inlen, int level) {
	char work[LZO1X_1_MEM_COMPRESS];
	return lzo_err(lzo1x_1_compress(in, inlen, out, outlen, work));
}

static int lzo_ufunc(Bytef *out, uLongf *outlen, const Bytef *in, uLong inlen) {
	return lzo_err(lzo1x_decompress(in, inlen, out, outlen, NULL));
}

static const codec_t c_lzo = {
	lzo_bfunc, lzo_cfunc, lzo_ufunc
};

#endif

#ifndef USE_SNAPPY
#define USE_SNAPPY	1
#endif
#if USE_SNAPPY
#include <snappy-c.h>

static const int snappy_errs[] = {
	Z_OK,
	Z_DATA_ERROR,
	Z_BUF_ERROR
};

static uLong snappy_bfunc(uLong len) {
	return snappy_max_compressed_length(len);
}

static int snappy_cfunc(Bytef *out, uLongf *outlen, const Bytef *in, uLong inlen, int level) {
	return snappy_errs[snappy_compress(in, inlen, out, outlen)];
}

static int snappy_ufunc(Bytef *out, uLongf *outlen, const Bytef *in, uLong inlen) {
	return snappy_errs[snappy_uncompress(in, inlen, out, outlen)];
}

static const codec_t c_snappy = {
	snappy_bfunc, snappy_cfunc, snappy_ufunc
};

#endif

static const ctable_t ctable[] = {
#if USE_BZIP2
	{"bzip2", &c_bzip2},
#endif
#if USE_LZ4
	{"lz4", &c_lz4},
	{"lz4hc", &c_lz4hc},
#endif
#if USE_LZMA
	{"lzma", &c_lzma},
#endif
#if USE_LZO
	{"lzo", &c_lzo},
#endif
#if USE_SNAPPY
	{"snappy", &c_snappy},
#endif
	{NULL, NULL}
};

static void set_codec() {
	char *ptr = getenv("POLYZ");
	int i;
	if (!ptr) exit(1);
	for (i=0; ctable[i].name; i++) {
		if (!strcmp(ptr, ctable[i].name)) {
			codec = (codec_t *)ctable[i].cptr;
			return;
		}
	}
	exit(1);
}

static uLong def_bfunc(uLong len) {
	set_codec();
	return codec->bound(len);
}

static int def_cfunc(Bytef *out, uLongf *outlen, const Bytef *in, uLong inlen, int level) {
	set_codec();
	return codec->comp(out, outlen, in, inlen, level);
}

static int def_ufunc(Bytef *out, uLongf *outlen, const Bytef *in, uLong inlen) {
	set_codec();
	return codec->uncomp(out, outlen, in, inlen);
}

