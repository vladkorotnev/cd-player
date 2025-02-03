#include <esper-core/miniz_ext.h>
#include <string.h>
#include <algorithm>
#include <esp32-hal-log.h>

static char LOG_TAG[] = "MINIZ";

typedef struct
{
    tinfl_decompressor m_decomp;
    mz_uint m_dict_ofs, m_dict_avail, m_first_call, m_has_flushed;
    int m_window_bits;
    mz_uint8 m_dict[TINFL_LZ_DICT_SIZE];
    tinfl_status m_last_status;
} inflate_state;


void mz_free(void *p)
{
    free(p);
}

void *miniz_def_alloc_func(void *opaque, size_t items, size_t size)
{
    (void)opaque, (void)items, (void)size;  
    return malloc(items * size);
}
void miniz_def_free_func(void *opaque, void *address)
{
    (void)opaque, (void)address;
    free(address);
}
void *miniz_def_realloc_func(void *opaque, void *address, size_t items, size_t size)
{
    (void)opaque, (void)address, (void)items, (void)size;
    return realloc(address, items * size);
}

int mz_inflateInit2(mz_streamp pStream, int window_bits)
{
    inflate_state *pDecomp;
    if (!pStream)
        return MZ_STREAM_ERROR;
    if ((window_bits != MZ_DEFAULT_WINDOW_BITS) && (-window_bits != MZ_DEFAULT_WINDOW_BITS))
        return MZ_PARAM_ERROR;

    pStream->data_type = 0;
    pStream->adler = 0;
    pStream->msg = NULL;
    pStream->total_in = 0;
    pStream->total_out = 0;
    pStream->reserved = 0;
    if (!pStream->zalloc)
        pStream->zalloc = miniz_def_alloc_func;
    if (!pStream->zfree)
        pStream->zfree = miniz_def_free_func;

    pDecomp = (inflate_state *)pStream->zalloc(pStream->opaque, 1, sizeof(inflate_state));
    if (!pDecomp)
        return MZ_MEM_ERROR;

    pStream->state = (struct mz_internal_state *)pDecomp;

    tinfl_init(&pDecomp->m_decomp);
    pDecomp->m_dict_ofs = 0;
    pDecomp->m_dict_avail = 0;
    pDecomp->m_last_status = TINFL_STATUS_NEEDS_MORE_INPUT;
    pDecomp->m_first_call = 1;
    pDecomp->m_has_flushed = 0;
    pDecomp->m_window_bits = window_bits;

    return MZ_OK;
}

int mz_inflateInit(mz_streamp pStream)
{
    return mz_inflateInit2(pStream, -MZ_DEFAULT_WINDOW_BITS);
}

int mz_inflateReset(mz_streamp pStream)
{
    inflate_state *pDecomp;
    if (!pStream)
        return MZ_STREAM_ERROR;

    pStream->data_type = 0;
    pStream->adler = 0;
    pStream->msg = NULL;
    pStream->total_in = 0;
    pStream->total_out = 0;
    pStream->reserved = 0;

    pDecomp = (inflate_state *)pStream->state;

    tinfl_init(&pDecomp->m_decomp);
    pDecomp->m_dict_ofs = 0;
    pDecomp->m_dict_avail = 0;
    pDecomp->m_last_status = TINFL_STATUS_NEEDS_MORE_INPUT;
    pDecomp->m_first_call = 1;
    pDecomp->m_has_flushed = 0;
    /* pDecomp->m_window_bits = window_bits */;

    return MZ_OK;
}

int mz_inflate(mz_streamp pStream, int flush)
{
    inflate_state *pState;
    mz_uint n, first_call, decomp_flags = TINFL_FLAG_COMPUTE_ADLER32;
    size_t in_bytes, out_bytes, orig_avail_in;
    tinfl_status status;

    if ((!pStream) || (!pStream->state))
        return MZ_STREAM_ERROR;
    if (flush == MZ_PARTIAL_FLUSH)
        flush = MZ_SYNC_FLUSH;
    if ((flush) && (flush != MZ_SYNC_FLUSH) && (flush != MZ_FINISH))
        return MZ_STREAM_ERROR;

    pState = (inflate_state *)pStream->state;
    if (pState->m_window_bits > 0)
        decomp_flags |= TINFL_FLAG_PARSE_ZLIB_HEADER;
    orig_avail_in = pStream->avail_in;

    first_call = pState->m_first_call;
    pState->m_first_call = 0;
    if (pState->m_last_status < 0)
        return MZ_DATA_ERROR;

    if (pState->m_has_flushed && (flush != MZ_FINISH))
        return MZ_STREAM_ERROR;
    pState->m_has_flushed |= (flush == MZ_FINISH);

    if ((flush == MZ_FINISH) && (first_call))
    {
        /* MZ_FINISH on the first call implies that the input and output buffers are large enough to hold the entire compressed/decompressed file. */
        decomp_flags |= TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF;
        in_bytes = pStream->avail_in;
        out_bytes = pStream->avail_out;
        status = tinfl_decompress(&pState->m_decomp, pStream->next_in, &in_bytes, pStream->next_out, pStream->next_out, &out_bytes, decomp_flags);
        pState->m_last_status = status;
        pStream->next_in += (mz_uint)in_bytes;
        pStream->avail_in -= (mz_uint)in_bytes;
        pStream->total_in += (mz_uint)in_bytes;
        pStream->adler = tinfl_get_adler32(&pState->m_decomp);
        pStream->next_out += (mz_uint)out_bytes;
        pStream->avail_out -= (mz_uint)out_bytes;
        pStream->total_out += (mz_uint)out_bytes;

        if (status < 0)
            return MZ_DATA_ERROR;
        else if (status != TINFL_STATUS_DONE)
        {
            pState->m_last_status = TINFL_STATUS_FAILED;
            return MZ_BUF_ERROR;
        }
        return MZ_STREAM_END;
    }
    /* flush != MZ_FINISH then we must assume there's more input. */
    if (flush != MZ_FINISH)
        decomp_flags |= TINFL_FLAG_HAS_MORE_INPUT;

    if (pState->m_dict_avail)
    {
        n = std::min(pState->m_dict_avail, pStream->avail_out);
        memcpy(pStream->next_out, pState->m_dict + pState->m_dict_ofs, n);
        pStream->next_out += n;
        pStream->avail_out -= n;
        pStream->total_out += n;
        pState->m_dict_avail -= n;
        pState->m_dict_ofs = (pState->m_dict_ofs + n) & (TINFL_LZ_DICT_SIZE - 1);
        return ((pState->m_last_status == TINFL_STATUS_DONE) && (!pState->m_dict_avail)) ? MZ_STREAM_END : MZ_OK;
    }

    for (;;)
    {
        in_bytes = pStream->avail_in;
        out_bytes = TINFL_LZ_DICT_SIZE - pState->m_dict_ofs;

        status = tinfl_decompress(&pState->m_decomp, pStream->next_in, &in_bytes, pState->m_dict, pState->m_dict + pState->m_dict_ofs, &out_bytes, decomp_flags);
        pState->m_last_status = status;

        pStream->next_in += (mz_uint)in_bytes;
        pStream->avail_in -= (mz_uint)in_bytes;
        pStream->total_in += (mz_uint)in_bytes;
        pStream->adler = tinfl_get_adler32(&pState->m_decomp);

        pState->m_dict_avail = (mz_uint)out_bytes;

        n = std::min(pState->m_dict_avail, pStream->avail_out);
        memcpy(pStream->next_out, pState->m_dict + pState->m_dict_ofs, n);
        pStream->next_out += n;
        pStream->avail_out -= n;
        pStream->total_out += n;
        pState->m_dict_avail -= n;
        pState->m_dict_ofs = (pState->m_dict_ofs + n) & (TINFL_LZ_DICT_SIZE - 1);

        if (status < 0)
            return MZ_DATA_ERROR; /* Stream is corrupted (there could be some uncompressed data left in the output dictionary - oh well). */
        else if ((status == TINFL_STATUS_NEEDS_MORE_INPUT) && (!orig_avail_in))
            return MZ_BUF_ERROR; /* Signal caller that we can't make forward progress without supplying more input or by setting flush to MZ_FINISH. */
        else if (flush == MZ_FINISH)
        {
            /* The output buffer MUST be large to hold the remaining uncompressed data when flush==MZ_FINISH. */
            if (status == TINFL_STATUS_DONE)
                return pState->m_dict_avail ? MZ_BUF_ERROR : MZ_STREAM_END;
            /* status here must be TINFL_STATUS_HAS_MORE_OUTPUT, which means there's at least 1 more byte on the way. If there's no more room left in the output buffer then something is wrong. */
            else if (!pStream->avail_out)
                return MZ_BUF_ERROR;
        }
        else if ((status == TINFL_STATUS_DONE) || (!pStream->avail_in) || (!pStream->avail_out) || (pState->m_dict_avail))
            break;
    }

    return ((status == TINFL_STATUS_DONE) && (!pState->m_dict_avail)) ? MZ_STREAM_END : MZ_OK;
}

int mz_inflateEnd(mz_streamp pStream)
{
    if (!pStream)
        return MZ_STREAM_ERROR;
    if (pStream->state)
    {
        pStream->zfree(pStream->opaque, pStream->state);
        pStream->state = NULL;
    }
    return MZ_OK;
}
int mz_uncompress2(unsigned char *pDest, mz_ulong *pDest_len, const unsigned char *pSource, mz_ulong *pSource_len)
{
    mz_stream stream;
    int status;
    memset(&stream, 0, sizeof(stream));

    /* In case mz_ulong is 64-bits (argh I hate longs). */
    if ((*pSource_len | *pDest_len) > 0xFFFFFFFFU)
        return MZ_PARAM_ERROR;

    stream.next_in = pSource;
    stream.avail_in = (mz_uint32)*pSource_len;
    stream.next_out = pDest;
    stream.avail_out = (mz_uint32)*pDest_len;

    status = mz_inflateInit(&stream);
    if (status != MZ_OK)
        return status;

    status = mz_inflate(&stream, MZ_FINISH);
    *pSource_len = *pSource_len - stream.avail_in;
    if (status != MZ_STREAM_END)
    {
        mz_inflateEnd(&stream);
        return ((status == MZ_BUF_ERROR) && (!stream.avail_in)) ? MZ_DATA_ERROR : status;
    }
    *pDest_len = stream.total_out;

    return mz_inflateEnd(&stream);
}

int mz_uncompress(unsigned char *pDest, mz_ulong *pDest_len, const unsigned char *pSource, mz_ulong source_len)
{
    return mz_uncompress2(pDest, pDest_len, pSource, &source_len);
}

const char *mz_error(int err)
{
    static struct
    {
        int m_err;
        const char *m_pDesc;
    } s_error_descs[] =
        {
          { MZ_OK, "" }, { MZ_STREAM_END, "stream end" }, { MZ_NEED_DICT, "need dictionary" }, { MZ_ERRNO, "file error" }, { MZ_STREAM_ERROR, "stream error" }, { MZ_DATA_ERROR, "data error" }, { MZ_MEM_ERROR, "out of memory" }, { MZ_BUF_ERROR, "buf error" }, { MZ_VERSION_ERROR, "version error" }, { MZ_PARAM_ERROR, "parameter error" }
        };
    mz_uint i;
    for (i = 0; i < sizeof(s_error_descs) / sizeof(s_error_descs[0]); ++i)
        if (s_error_descs[i].m_err == err)
            return s_error_descs[i].m_pDesc;
    return NULL;
}

static const mz_uint s_tdefl_num_probes[11] = { 0, 1, 6, 32, 16, 32, 128, 256, 512, 768, 1500 };
/* level may actually range from [0,10] (10 is a "hidden" max level, where we want a bit more compression and it's fine if throughput to fall off a cliff on some files). */
mz_uint tdefl_create_comp_flags_from_zip_params(int level, int window_bits, int strategy)
{
    mz_uint comp_flags = s_tdefl_num_probes[(level >= 0) ? std::min(10, level) : MZ_DEFAULT_LEVEL] | ((level <= 3) ? TDEFL_GREEDY_PARSING_FLAG : 0);
    if (window_bits > 0)
        comp_flags |= TDEFL_WRITE_ZLIB_HEADER;

    if (!level)
        comp_flags |= TDEFL_FORCE_ALL_RAW_BLOCKS;
    else if (strategy == MZ_FILTERED)
        comp_flags |= TDEFL_FILTER_MATCHES;
    else if (strategy == MZ_HUFFMAN_ONLY)
        comp_flags &= ~TDEFL_MAX_PROBES_MASK;
    else if (strategy == MZ_FIXED)
        comp_flags |= TDEFL_FORCE_ALL_STATIC_BLOCKS;
    else if (strategy == MZ_RLE)
        comp_flags |= TDEFL_RLE_MATCHES;

    return comp_flags;
}

int mz_deflateEnd(mz_streamp pStream)
{
    if (!pStream)
        return MZ_STREAM_ERROR;
    if (pStream->state)
    {
        pStream->zfree(pStream->opaque, pStream->state);
        pStream->state = NULL;
    }
    return MZ_OK;
}

int mz_deflateInit2(mz_streamp pStream, int level, int method, int window_bits, int mem_level, int strategy)
{
    tdefl_compressor *pComp;
    mz_uint comp_flags = TDEFL_COMPUTE_ADLER32 | tdefl_create_comp_flags_from_zip_params(level, window_bits, strategy);

    if (!pStream)
        return MZ_STREAM_ERROR;
    if ((method != MZ_DEFLATED) || ((mem_level < 1) || (mem_level > 9)) || ((window_bits != MZ_DEFAULT_WINDOW_BITS) && (-window_bits != MZ_DEFAULT_WINDOW_BITS)))
        return MZ_PARAM_ERROR;

    pStream->data_type = 0;
    pStream->adler = MZ_ADLER32_INIT;
    pStream->msg = NULL;
    pStream->reserved = 0;
    pStream->total_in = 0;
    pStream->total_out = 0;
    if (!pStream->zalloc)
        pStream->zalloc = miniz_def_alloc_func;
    if (!pStream->zfree)
        pStream->zfree = miniz_def_free_func;

    pComp = (tdefl_compressor *)pStream->zalloc(pStream->opaque, 1, sizeof(tdefl_compressor));
    if (!pComp)
        return MZ_MEM_ERROR;

    pStream->state = (struct mz_internal_state *)pComp;

    if (tdefl_init(pComp, NULL, NULL, comp_flags) != TDEFL_STATUS_OKAY)
    {
        mz_deflateEnd(pStream);
        return MZ_PARAM_ERROR;
    }

    return MZ_OK;
}

int mz_deflateInit(mz_streamp pStream, int level)
{
    return mz_deflateInit2(pStream, level, MZ_DEFLATED, MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY);
}


int mz_deflateReset(mz_streamp pStream)
{
    if ((!pStream) || (!pStream->state) || (!pStream->zalloc) || (!pStream->zfree))
        return MZ_STREAM_ERROR;
    pStream->total_in = pStream->total_out = 0;
    tdefl_init((tdefl_compressor *)pStream->state, NULL, NULL, ((tdefl_compressor *)pStream->state)->m_flags);
    return MZ_OK;
}

int mz_deflate(mz_streamp pStream, int flush)
{
    size_t in_bytes, out_bytes;
    mz_ulong orig_total_in, orig_total_out;
    int mz_status = MZ_OK;

    if ((!pStream) || (!pStream->state) || (flush < 0) || (flush > MZ_FINISH) || (!pStream->next_out))
        return MZ_STREAM_ERROR;
    if (!pStream->avail_out)
        return MZ_BUF_ERROR;

    if (flush == MZ_PARTIAL_FLUSH)
        flush = MZ_SYNC_FLUSH;

    if (((tdefl_compressor *)pStream->state)->m_prev_return_status == TDEFL_STATUS_DONE)
        return (flush == MZ_FINISH) ? MZ_STREAM_END : MZ_BUF_ERROR;

    orig_total_in = pStream->total_in;
    orig_total_out = pStream->total_out;
    for (;;)
    {
        tdefl_status defl_status;
        in_bytes = pStream->avail_in;
        out_bytes = pStream->avail_out;

        defl_status = tdefl_compress((tdefl_compressor *)pStream->state, pStream->next_in, &in_bytes, pStream->next_out, &out_bytes, (tdefl_flush)flush);
        pStream->next_in += (mz_uint)in_bytes;
        pStream->avail_in -= (mz_uint)in_bytes;
        pStream->total_in += (mz_uint)in_bytes;
        pStream->adler = tdefl_get_adler32((tdefl_compressor *)pStream->state);

        pStream->next_out += (mz_uint)out_bytes;
        pStream->avail_out -= (mz_uint)out_bytes;
        pStream->total_out += (mz_uint)out_bytes;

        if (defl_status < 0)
        {
            mz_status = MZ_STREAM_ERROR;
            break;
        }
        else if (defl_status == TDEFL_STATUS_DONE)
        {
            mz_status = MZ_STREAM_END;
            break;
        }
        else if (!pStream->avail_out)
            break;
        else if ((!pStream->avail_in) && (flush != MZ_FINISH))
        {
            if ((flush) || (pStream->total_in != orig_total_in) || (pStream->total_out != orig_total_out))
                break;
            return MZ_BUF_ERROR; /* Can't make forward progress without some input.
                                    */
        }
    }
    return mz_status;
}

mz_ulong mz_deflateBound(mz_streamp pStream, mz_ulong source_len)
{
    (void)pStream;
    /* This is really over conservative. (And lame, but it's actually pretty tricky to compute a true upper bound given the way tdefl's blocking works.) */
    return std::max(128 + (source_len * 110) / 100, 128 + source_len + ((source_len / (31 * 1024)) + 1) * 5);
}

int mz_compress2(unsigned char *pDest, mz_ulong *pDest_len, const unsigned char *pSource, mz_ulong source_len, int level)
{
    int status;
    mz_stream stream;
    memset(&stream, 0, sizeof(stream));

    /* In case mz_ulong is 64-bits (argh I hate longs). */
    if ((mz_uint64)(source_len | *pDest_len) > 0xFFFFFFFFU)
        return MZ_PARAM_ERROR;

    stream.next_in = pSource;
    stream.avail_in = (mz_uint32)source_len;
    stream.next_out = pDest;
    stream.avail_out = (mz_uint32)*pDest_len;

    status = mz_deflateInit(&stream, level);
    if (status != MZ_OK)
        return status;

    status = mz_deflate(&stream, MZ_FINISH);
    if (status != MZ_STREAM_END)
    {
        mz_deflateEnd(&stream);
        return (status == MZ_OK) ? MZ_BUF_ERROR : status;
    }

    *pDest_len = stream.total_out;
    return mz_deflateEnd(&stream);
}

int mz_compress(unsigned char *pDest, mz_ulong *pDest_len, const unsigned char *pSource, mz_ulong source_len)
{
    return mz_compress2(pDest, pDest_len, pSource, source_len, MZ_DEFAULT_COMPRESSION);
}

mz_ulong mz_compressBound(mz_ulong source_len)
{
    return mz_deflateBound(NULL, source_len);
}

void * decompress_emplace(void * compressed_data, uint32_t src_size, uint32_t decomp_size) {
    unsigned long dst_sz = decomp_size;
    void * dest = malloc(dst_sz);
    if(dest == nullptr) {
        ESP_LOGE(LOG_TAG, "OOM allocating decompression buffer of %i bytes", dst_sz);
        free(compressed_data);
        return nullptr;
    }

    int rslt = mz_uncompress((unsigned char*)dest, &dst_sz, (unsigned char*) compressed_data, src_size);
    
    free(compressed_data);

    if(rslt != MZ_OK) {
        free(dest);
        ESP_LOGE(LOG_TAG, "Decompress error %i: %s", rslt, mz_error(rslt));
        return nullptr;
    }

    return dest; 
}

void * compress_emplace(void * src_data, uint32_t src_size, uint32_t * comp_size) {
    // Compress src_data with miniZ
    mz_ulong dest_size = src_size; // gotta fit?
    void * dest = malloc(dest_size);
    if(dest == nullptr) {
        ESP_LOGE(LOG_TAG, "OOM allocating compression buffer of %i bytes", dest_size);
        free(src_data);
        return nullptr;
    }

    mz_stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.next_in = (unsigned char*)src_data;
    stream.avail_in = src_size;
    stream.next_out = (unsigned char*)dest;
    stream.avail_out = dest_size;

    int rslt = mz_deflateInit(&stream, MZ_BEST_COMPRESSION);
    if(rslt != MZ_OK) {
        ESP_LOGE(LOG_TAG, "DeflateInit error %i", rslt);
        free(dest);
        free(src_data);
        return nullptr;
    }

    rslt = mz_deflate(&stream, MZ_FINISH);
    if(rslt != MZ_STREAM_END) {
        ESP_LOGE(LOG_TAG, "Deflate error %i", rslt);
        mz_deflateEnd(&stream);
        free(dest);
        free(src_data);
        return nullptr;
    }

    *comp_size = stream.total_out;
    mz_deflateEnd(&stream);
    free(src_data);
    return dest;
}