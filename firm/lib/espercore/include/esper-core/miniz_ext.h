#include <rom/miniz.h>

// MiniZ extra functions that are not present in the ROM


// Flush values. For typical usage you only need MZ_NO_FLUSH and MZ_FINISH. The other values are for advanced use (refer to the zlib docs).
enum { MZ_NO_FLUSH = 0, MZ_PARTIAL_FLUSH = 1, MZ_SYNC_FLUSH = 2, MZ_FULL_FLUSH = 3, MZ_FINISH = 4, MZ_BLOCK = 5 };

// Return status codes. MZ_PARAM_ERROR is non-standard.
enum { MZ_OK = 0, MZ_STREAM_END = 1, MZ_NEED_DICT = 2, MZ_ERRNO = -1, MZ_STREAM_ERROR = -2, MZ_DATA_ERROR = -3, MZ_MEM_ERROR = -4, MZ_BUF_ERROR = -5, MZ_VERSION_ERROR = -6, MZ_PARAM_ERROR = -10000 };

// Compression levels: 0-9 are the standard zlib-style levels, 10 is best possible compression (not zlib compatible, and may be very slow), MZ_DEFAULT_COMPRESSION=MZ_DEFAULT_LEVEL.
enum { MZ_NO_COMPRESSION = 0, MZ_BEST_SPEED = 1, MZ_BEST_COMPRESSION = 9, MZ_UBER_COMPRESSION = 10, MZ_DEFAULT_LEVEL = 6, MZ_DEFAULT_COMPRESSION = -1 };

// Window bits
#define MZ_DEFAULT_WINDOW_BITS 15

// Heap allocation callbacks.
// Note that mz_alloc_func parameter types purpsosely differ from zlib's: items/size is size_t, not unsigned long.
typedef void *(*mz_alloc_func)(void *opaque, size_t items, size_t size);
typedef void (*mz_free_func)(void *opaque, void *address);
typedef void *(*mz_realloc_func)(void *opaque, void *address, size_t items, size_t size);

struct mz_internal_state;

// Compression/decompression stream struct.
typedef struct mz_stream_s {
    const unsigned char *next_in;     // pointer to next byte to read
    unsigned int avail_in;            // number of bytes available at next_in
    mz_ulong total_in;                // total number of bytes consumed so far

    unsigned char *next_out;          // pointer to next byte to write
    unsigned int avail_out;           // number of bytes that can be written to next_out
    mz_ulong total_out;               // total number of bytes produced so far

    char *msg;                        // error msg (unused)
    struct mz_internal_state *state;  // internal state, allocated by zalloc/zfree

    mz_alloc_func zalloc;             // optional heap allocation function (defaults to malloc)
    mz_free_func zfree;               // optional heap free function (defaults to free)
    void *opaque;                     // heap alloc function user pointer

    int data_type;                    // data_type (unused)
    mz_ulong adler;                   // adler32 of the source or uncompressed data
    mz_ulong reserved;                // not used
} mz_stream;

typedef mz_stream *mz_streamp;

/* Single-call decompression. */
/* Returns MZ_OK on success, or one of the error codes from mz_inflate() on failure. */
int mz_uncompress(unsigned char *pDest, mz_ulong *pDest_len, const unsigned char *pSource, mz_ulong source_len);
int mz_uncompress2(unsigned char *pDest, mz_ulong *pDest_len, const unsigned char *pSource, mz_ulong *pSource_len);
const char *mz_error(int err);

// Convenience func copied from PIS-OS
// Decompress a buffer, freeing the original
void * decompress_emplace(void * compressed_data, uint32_t src_size, uint32_t decomp_size);

int mz_deflateInit(mz_streamp pStream, int level);
int mz_deflateInit2(mz_streamp pStream, int level, int method, int window_bits, int mem_level, int strategy);
int mz_deflate(mz_streamp pStream, int flush);
int mz_deflateEnd(mz_streamp pStream);