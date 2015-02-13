/* pzsnap.c - fake snappy wrapper to invoke other compression APIs
 * Copyright 2014-2015 Howard Chu @ Symas Corp.
 */
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>

typedef size_t (bfunc)(size_t len);
typedef void (cfunc)(const char *in, size_t inlen, char *out, size_t *outlen);
typedef bool (ufunc)(const char *in, size_t inlen, char *out);

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

namespace snappy {
	/* We always store the original length at head of buffer */
	bool GetUncompressedLength(const char *in, size_t inlen, size_t *result) {
		if (inlen < sizeof(size_t))
			return false;
		memcpy(result, in, sizeof(size_t));
		return true;
	}
	void RawCompress(const char *in, size_t inlen, char *out, size_t *outlen) {
		memcpy(out, &inlen, sizeof(size_t));
		*outlen -= sizeof(size_t);
		codec->comp(in, inlen, out+sizeof(size_t), outlen);
		*outlen += sizeof(size_t);
	}
	bool RawUncompress(const char *in, size_t inlen, char *out) {
		return codec->uncomp(in+sizeof(size_t), inlen-sizeof(size_t), out);
	}
	size_t MaxCompressedLength(size_t len){
		return codec->bound(len) + sizeof(size_t);
	}
}

extern "C" {
	int snappy_compress(const char *in, size_t inlen, char *out, size_t *outlen) {
		snappy::RawCompress(in, inlen, out, outlen);
		return 0;
	}

	int snappy_uncompress(const char *in, size_t inlen, char *out, size_t *outlen) {
		snappy::RawUncompress(in, inlen, out);
		return 0;
	}

	size_t snappy_max_compressed_length(size_t len) {
		return snappy::MaxCompressedLength(len);
	}

	int snappy_uncompressed_length(const char *in, size_t inlen, size_t *outlen) {
		snappy::GetUncompressedLength(in, inlen, outlen);
		return 0;
	}
}

#ifndef USE_BZIP2
#define USE_BZIP2	1
#endif
#if USE_BZIP2
#include <bzlib.h>
static size_t bzip2_bfunc(size_t len) {
	return len + 16;
}

static void bzip2_cfunc(const char *in, size_t inlen, char *out, size_t *outlen) {
	unsigned int destlen = *outlen;
	BZ2_bzBuffToBuffCompress(out, &destlen, (char *)in, inlen, 9, 0, 0);
	*outlen = destlen;
}

static bool bzip2_ufunc(const char *in, size_t inlen, char *out) {
	size_t origlen;
	unsigned int destlen;
	memcpy(&origlen, in-sizeof(size_t), sizeof(size_t));
	destlen = origlen;
	return BZ2_bzBuffToBuffDecompress(out, &destlen, (char *)in, inlen, 0, 0) == BZ_OK;
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
static size_t lz4_bfunc(size_t len) {
	return LZ4_compressBound(len);
}

static void lz4_cfunc(const char *in, size_t inlen, char *out, size_t *outlen) {
	*outlen = LZ4_compress(in, out, inlen);
}

static bool lz4_ufunc(const char *in, size_t inlen, char *out) {
	size_t origlen;
	memcpy(&origlen, in-sizeof(size_t), sizeof(size_t));
	origlen = LZ4_decompress_safe(in, out, inlen, origlen);
	return origlen >= 0;
}

static const codec_t c_lz4 = {
	lz4_bfunc, lz4_cfunc, lz4_ufunc
};

#include <lz4hc.h>
static void lz4hc_cfunc(const char *in, size_t inlen, char *out, size_t *outlen) {
	*outlen = LZ4_compressHC(in, out, inlen);
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
static size_t lzma_bfunc(size_t len) {
	return lzma_stream_buffer_bound(len);
}

static void lzma_cfunc(const char *in, size_t inlen, char *out, size_t *outlen) {
	lzma_easy_buffer_encode(2, LZMA_CHECK_NONE,  NULL, (uint8_t *)in, inlen,
		(uint8_t *)out, outlen, *outlen);
}

static bool lzma_ufunc(const char *in, size_t inlen, char *out) {
	uint64_t memlimit = UINT64_MAX;
	size_t inpos = 0;
	size_t outpos = 0;
	size_t outsize;
	memcpy(&outsize, in-sizeof(size_t), sizeof(size_t));
	return lzma_stream_buffer_decode(&memlimit, 0, NULL, (uint8_t *)in, &inpos,
		inlen, (uint8_t *)out, &outpos, outsize) == 0;
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
static size_t lzo_bfunc(size_t len) {
	return len + 16;
}

static void lzo_cfunc(const char *in, size_t inlen, char *out, size_t *outlen) {
	char work[LZO1X_1_MEM_COMPRESS];
	lzo_uint dstlen;
	lzo1x_1_compress((lzo_bytep)in, inlen, (lzo_bytep)out, &dstlen, work);
	*outlen = dstlen;
}

static bool lzo_ufunc(const char *in, size_t inlen, char *out) {
	size_t outsize;
	lzo_uint dstlen;
	memcpy(&outsize, in-sizeof(size_t), sizeof(size_t));
	dstlen = outsize;
	return lzo1x_decompress((lzo_bytep)in, inlen, (lzo_bytep)out, &dstlen, NULL) == 0;
}

static const codec_t c_lzo = {
	lzo_bfunc, lzo_cfunc, lzo_ufunc
};

#endif

#ifndef USE_ZLIB
#define USE_ZLIB	1
#endif
#if USE_ZLIB
#include <zlib.h>
static size_t zlib_bfunc(size_t len) {
	return compressBound(len);
}

static void zlib_cfunc(const char *in, size_t inlen, char *out, size_t *outlen) {
	compress((Bytef *)out, outlen, (Bytef *)in, inlen);
}

static bool zlib_ufunc(const char *in, size_t inlen, char *out) {
	size_t outlen;
	memcpy(&outlen, in-sizeof(size_t), sizeof(size_t));
	return uncompress((Bytef *)out, &outlen, (Bytef *)in, inlen) == Z_OK;
}

static const codec_t c_zlib = {
	zlib_bfunc, zlib_cfunc, zlib_ufunc
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
#if USE_ZLIB
	{"zlib", &c_zlib},
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

static size_t def_bfunc(size_t len) {
	set_codec();
	return codec->bound(len);
}

static void def_cfunc(const char *in, size_t inlen, char *out, size_t *outlen) {
	set_codec();
	codec->comp(in, inlen, out, outlen);
}

static bool def_ufunc(const char *in, size_t inlen, char *out) {
	set_codec();
	return codec->uncomp(in, inlen, out);
}
