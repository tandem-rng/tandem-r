/* Tandem8x32 reference implementation. See tandem.h and the specification. */
#include "tandem.h"

#include <math.h>
#include <string.h>
#if defined(__ARM_NEON) && defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif

#define CLOCK_WEYL 0x9e3779b9u
#define DOMAIN_STREAM 0x9e3779b9u
#define DOMAIN_SPLIT 0xbb67ae85u
#define DOMAIN_FORK 0xd2511f53u
#define DOMAIN_FOLD 0xcd9e8d57u
#define DOMAIN_SEED 0xa54ff53au
#define AUX_STREAM 0x94d049bbu

static const uint32_t RC[8] = {0xd17cc1b7u, 0xa7220a94u, 0xfe13abe8u, 0xfa9a6ee0u,
                               0xedb14accu, 0x9e21c820u, 0xff28b1d5u, 0xef5de2b0u};

static inline uint32_t rotl(uint32_t x, unsigned r) { return (x << r) | (x >> (32u - r)); }

/* The step and the seeding function as static inline bodies. The public tandem_T and tandem_F
   wrap them: GCC does not inline a public function into the scalar row loop, which left that
   loop at a fifth of its speed. */
static inline void step_T(uint32_t o[4], uint32_t h[4]) {
    uint64_t p0 = (uint64_t)o[0] * (h[0] | 1u);
    uint64_t p1 = (uint64_t)o[2] * (h[1] | 1u);
    uint32_t lo0 = (uint32_t)p0, hi0 = (uint32_t)(p0 >> 32);
    uint32_t lo1 = (uint32_t)p1, hi1 = (uint32_t)(p1 >> 32);
    uint32_t n0 = o[1] ^ hi1 ^ lo1;
    uint32_t n1 = rotl(lo1, 16) ^ h[2];
    uint32_t n2 = o[3] ^ hi0 ^ lo0;
    uint32_t n3 = rotl(lo0, 16) ^ h[3];

    h[0] ^= rotl(h[1], 7);
    h[1] ^= rotl(h[2], 13);
    h[2] ^= rotl(h[3], 22);
    h[3] ^= rotl(h[0], 3);
    h[0] = (h[0] + CLOCK_WEYL) ^ n0;

    o[0] = n0;
    o[1] = n1;
    o[2] = n2;
    o[3] = n3;
}

static inline void step_F(uint32_t o[4], uint32_t h[4]) {
    for (int r = 0; r < 8; r++) {
        uint32_t t[4];
        step_T(o, h);
        o[0] ^= RC[r];
        memcpy(t, o, sizeof t);
        memcpy(o, h, sizeof t);
        memcpy(h, t, sizeof t);
    }
}

void tandem_T(uint32_t o[4], uint32_t h[4]) { step_T(o, h); }
void tandem_F(uint32_t o[4], uint32_t h[4]) { step_F(o, h); }

void tandem_F_keyed(const uint32_t key[4], uint64_t counter, uint32_t domain, uint32_t aux,
                    uint32_t o[4], uint32_t h[4]) {
    o[0] = (uint32_t)counter;
    o[1] = (uint32_t)(counter >> 32);
    o[2] = domain;
    o[3] = aux;
    memcpy(h, key, 16);
    step_F(o, h);
}

void tandem_block(const uint32_t key[4], uint64_t c, uint32_t j, uint32_t out[4]) {
    uint32_t h[4];
    tandem_F_keyed(key, c, DOMAIN_STREAM, AUX_STREAM, out, h);
    for (uint32_t s = 0; s <= j; s++) step_T(out, h);
}

/* ---- Eight lanes ---------------------------------------------------------------------
 *
 * A row is the eight chunks of a group at one step, so the cache is word-major, o[word][lane],
 * and one T over a row is four element-wise operations per word. The `lanes` type holds that
 * state in registers inside the row loop. With GCC 12+ or clang it is built from vector
 * extensions and compiles to NEON or SSE/AVX; the scalar version below it is the same
 * loop written out, for other compilers or -DTANDEM_NO_SIMD. */

/* What the row loop writes: the raw words, or the words mapped to floats on the way out,
 * while the row is still in registers. A second pass over the buffer costs about as much as
 * the generation itself. */
typedef enum { STORE_RAW, STORE_F32, STORE_F64 } store_mode;

#if (defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 12)) && !defined(TANDEM_NO_SIMD)
typedef uint32_t u32x4 __attribute__((vector_size(16)));
typedef uint64_t u64x4 __attribute__((vector_size(32)));

typedef struct {
    u32x4 o[4], h[4]; /* four lanes of each word */
} quad;

typedef struct {
    quad q[2];
} lanes;

static inline u32x4 vrotl(u32x4 x, unsigned r) { return (x << r) | (x >> (32u - r)); }

/* Low and high words of the four 32x32 to 64-bit products. GCC does not lower the widened
   64-bit vector multiply to umull on AArch64 and scalarizes it instead, which costs more than
   half the fill speed, so NEON gets the widening multiply spelled out. */
static inline void vmul_wide(u32x4 a, u32x4 b, u32x4 *lo, u32x4 *hi) {
#if defined(__ARM_NEON) && defined(__aarch64__)
    uint32x4_t va = (uint32x4_t)a, vb = (uint32x4_t)b;
    uint32x4_t p0 = vreinterpretq_u32_u64(vmull_u32(vget_low_u32(va), vget_low_u32(vb)));
    uint32x4_t p1 = vreinterpretq_u32_u64(vmull_high_u32(va, vb));
    *lo = (u32x4)vuzp1q_u32(p0, p1);
    *hi = (u32x4)vuzp2q_u32(p0, p1);
#elif defined(__SSE2__)
    /* pmuludq multiplies lanes 0 and 2. GCC emulates the widened 64-bit multiply with three of
       them per vector instead, which costs most of the fill speed on x86. */
    __m128i va = (__m128i)a, vb = (__m128i)b;
    u32x4 p02 = (u32x4)_mm_mul_epu32(va, vb);
    u32x4 p13 = (u32x4)_mm_mul_epu32(_mm_srli_epi64(va, 32), _mm_srli_epi64(vb, 32));
    *lo = __builtin_shufflevector(p02, p13, 0, 4, 2, 6);
    *hi = __builtin_shufflevector(p02, p13, 1, 5, 3, 7);
#else
    u64x4 p = __builtin_convertvector(a, u64x4) * __builtin_convertvector(b, u64x4);
    *lo = __builtin_convertvector(p, u32x4);
    *hi = __builtin_convertvector(p >> 32, u32x4);
#endif
}

static inline void quad_T(quad *q) {
    u32x4 *o = q->o, *h = q->h;
    u32x4 lo0, hi0, lo1, hi1;
    vmul_wide(o[0], h[0] | 1u, &lo0, &hi0);
    vmul_wide(o[2], h[1] | 1u, &lo1, &hi1);
    u32x4 n0 = o[1] ^ hi1 ^ lo1;
    u32x4 n1 = vrotl(lo1, 16) ^ h[2];
    u32x4 n2 = o[3] ^ hi0 ^ lo0;
    u32x4 n3 = vrotl(lo0, 16) ^ h[3];

    h[0] ^= vrotl(h[1], 7);
    h[1] ^= vrotl(h[2], 13);
    h[2] ^= vrotl(h[3], 22);
    h[3] ^= vrotl(h[0], 3);
    h[0] = (h[0] + CLOCK_WEYL) ^ n0;

    o[0] = n0;
    o[1] = n1;
    o[2] = n2;
    o[3] = n3;
}

/* F on chunks c0 .. c0+3 at once. The rounds run on a local copy: through the pointer GCC -O2
   keeps the state in memory for all eight rounds, which costs half the fill speed at K = 32. */
static void quad_seed(quad *q, const uint32_t key[4], uint64_t c0) {
    uint32_t lo = (uint32_t)c0;
    u32x4 counter = {lo, lo + 1u, lo + 2u, lo + 3u}, zero = {0, 0, 0, 0};
    quad w;
    w.o[0] = counter;
    w.o[1] = zero + (uint32_t)(c0 >> 32);
    w.o[2] = zero + DOMAIN_STREAM;
    w.o[3] = zero + AUX_STREAM;
    w.h[0] = zero + key[0];
    w.h[1] = zero + key[1];
    w.h[2] = zero + key[2];
    w.h[3] = zero + key[3];
    for (int r = 0; r < 8; r++) {
        quad_T(&w);
        w.o[0] ^= RC[r];
        u32x4 t0 = w.o[0], t1 = w.o[1], t2 = w.o[2], t3 = w.o[3];
        w.o[0] = w.h[0], w.o[1] = w.h[1], w.o[2] = w.h[2], w.o[3] = w.h[3];
        w.h[0] = t0, w.h[1] = t1, w.h[2] = t2, w.h[3] = t3;
    }
    *q = w;
}

typedef uint64_t u64x2 __attribute__((vector_size(16)));
typedef float f32x4 __attribute__((vector_size(16)));
typedef double f64x2 __attribute__((vector_size(16)));

static inline void store_block(u32x4 b, char *dst, store_mode mode) {
    if (mode == STORE_F32) {
#if defined(__ARM_NEON) && defined(__aarch64__)
        float32x4_t f = vcvtq_n_f32_u32(vshrq_n_u32((uint32x4_t)b, 8), 24);
#else
        f32x4 f = __builtin_convertvector(b >> 8, f32x4) * 0x1p-24f;
#endif
        memcpy(dst, &f, 16);
    } else if (mode == STORE_F64) {
        u64x2 w;
        memcpy(&w, &b, 16);
#if defined(__ARM_NEON) && defined(__aarch64__)
        float64x2_t f = vcvtq_n_f64_u64(vshrq_n_u64((uint64x2_t)w, 11), 53);
#else
        f64x2 f = __builtin_convertvector(w >> 11, f64x2) * 0x1p-53;
#endif
        memcpy(dst, &f, 16);
    } else {
        memcpy(dst, &b, 16);
    }
}

/* The four blocks of a quad in stream order: a 4x4 word transpose. */
static inline void quad_store(const quad *q, char *dst, store_mode mode) {
    u32x4 t0 = __builtin_shufflevector(q->o[0], q->o[1], 0, 4, 1, 5);
    u32x4 t1 = __builtin_shufflevector(q->o[2], q->o[3], 0, 4, 1, 5);
    u32x4 t2 = __builtin_shufflevector(q->o[0], q->o[1], 2, 6, 3, 7);
    u32x4 t3 = __builtin_shufflevector(q->o[2], q->o[3], 2, 6, 3, 7);
    u32x4 b0 = __builtin_shufflevector(t0, t1, 0, 1, 4, 5);
    u32x4 b1 = __builtin_shufflevector(t0, t1, 2, 3, 6, 7);
    u32x4 b2 = __builtin_shufflevector(t2, t3, 0, 1, 4, 5);
    u32x4 b3 = __builtin_shufflevector(t2, t3, 2, 3, 6, 7);
    store_block(b0, dst, mode);
    store_block(b1, dst + 16, mode);
    store_block(b2, dst + 32, mode);
    store_block(b3, dst + 48, mode);
}

static inline void lanes_load(lanes *L, const tandem_rng *rng) {
    for (unsigned w = 0; w < 4; w++)
        for (unsigned k = 0; k < 2; k++) {
            memcpy(&L->q[k].o[w], &rng->o[w][4u * k], 16);
            memcpy(&L->q[k].h[w], &rng->h[w][4u * k], 16);
        }
}

static inline void lanes_save(const lanes *L, tandem_rng *rng) {
    for (unsigned w = 0; w < 4; w++)
        for (unsigned k = 0; k < 2; k++) {
            memcpy(&rng->o[w][4u * k], &L->q[k].o[w], 16);
            memcpy(&rng->h[w][4u * k], &L->q[k].h[w], 16);
        }
}

static inline void lanes_T(lanes *L) {
    quad_T(&L->q[0]);
    quad_T(&L->q[1]);
}

static inline void lanes_seed(lanes *L, const uint32_t key[4], uint64_t g) {
    quad_seed(&L->q[0], key, 8u * g);
    quad_seed(&L->q[1], key, 8u * g + 4u);
}

static inline void lanes_store(const lanes *L, char *dst, store_mode mode) {
    quad_store(&L->q[0], dst, mode);
    quad_store(&L->q[1], dst + 64, mode);
}
#else
typedef struct {
    uint32_t o[4][8], h[4][8];
} lanes;

static inline void lanes_load(lanes *L, const tandem_rng *rng) {
    memcpy(L->o, rng->o, sizeof L->o);
    memcpy(L->h, rng->h, sizeof L->h);
}

static inline void lanes_save(const lanes *L, tandem_rng *rng) {
    memcpy(rng->o, L->o, sizeof L->o);
    memcpy(rng->h, L->h, sizeof L->h);
}

static inline void lanes_T(lanes *L) {
    /* One straight-line body per lane, so that the loop vectorizes over the eight lanes. */
    for (unsigned l = 0; l < 8; l++) {
        uint32_t o0 = L->o[0][l], o1 = L->o[1][l], o2 = L->o[2][l], o3 = L->o[3][l];
        uint32_t h0 = L->h[0][l], h1 = L->h[1][l], h2 = L->h[2][l], h3 = L->h[3][l];
        uint64_t p0 = (uint64_t)o0 * (h0 | 1u);
        uint64_t p1 = (uint64_t)o2 * (h1 | 1u);
        uint32_t lo0 = (uint32_t)p0, hi0 = (uint32_t)(p0 >> 32);
        uint32_t lo1 = (uint32_t)p1, hi1 = (uint32_t)(p1 >> 32);
        uint32_t n0 = o1 ^ hi1 ^ lo1;
        uint32_t n1 = rotl(lo1, 16) ^ h2;
        uint32_t n2 = o3 ^ hi0 ^ lo0;
        uint32_t n3 = rotl(lo0, 16) ^ h3;
        h0 ^= rotl(h1, 7);
        h1 ^= rotl(h2, 13);
        h2 ^= rotl(h3, 22);
        h3 ^= rotl(h0, 3);
        h0 = (h0 + CLOCK_WEYL) ^ n0;
        L->o[0][l] = n0, L->o[1][l] = n1, L->o[2][l] = n2, L->o[3][l] = n3;
        L->h[0][l] = h0, L->h[1][l] = h1, L->h[2][l] = h2, L->h[3][l] = h3;
    }
}

static inline void lanes_seed(lanes *L, const uint32_t key[4], uint64_t g) {
    for (unsigned l = 0; l < 8; l++) {
        uint32_t o[4], h[4];
        tandem_F_keyed(key, 8u * g + l, DOMAIN_STREAM, AUX_STREAM, o, h);
        for (unsigned w = 0; w < 4; w++) {
            L->o[w][l] = o[w];
            L->h[w][l] = h[w];
        }
    }
}

static inline void lanes_store(const lanes *L, char *dst, store_mode mode) {
    uint32_t row[32];
    for (unsigned l = 0; l < 8; l++)
        for (unsigned w = 0; w < 4; w++) row[4u * l + w] = L->o[w][l];
    if (mode == STORE_F32) {
        float f[32];
        for (unsigned i = 0; i < 32; i++) f[i] = (float)(row[i] >> 8) * 0x1p-24f;
        memcpy(dst, f, sizeof f);
    } else if (mode == STORE_F64) {
        uint64_t raw[16];
        double f[16];
        memcpy(raw, row, sizeof raw);
        for (unsigned i = 0; i < 16; i++) f[i] = (double)(raw[i] >> 11) * 0x1p-53;
        memcpy(dst, f, sizeof f);
    } else {
        memcpy(dst, row, sizeof row);
    }
}
#endif

/* ---- Rows ------------------------------------------------------------------------------ */

static inline unsigned log2k(uint32_t K) {
    unsigned s = 0;
    while ((K >> s) > 1u) s++;
    return s;
}

/* Produce rows [row, row + nrows) in stream order, 128 bytes each, into `out`, or only move
 * the cache when out is NULL. Stepping forward inside the cached group costs one T per row;
 * any other jump reseeds the group. Afterwards the cache holds the last row produced. */
static void run_rows(tandem_rng *rng, uint64_t row, size_t nrows, char *out, store_mode mode) {
    unsigned shift = log2k(rng->K);
    uint64_t mask = rng->K - 1u, at = rng->row;
    int live = rng->cached != 0;
    lanes L;

    if (nrows == 0) return;
    lanes_load(&L, rng);
    while (nrows) {
        size_t run = rng->K - (size_t)(row & mask); /* rows left in this group */
        if (!live || at != row) {
            if (live && row > at && (row >> shift) == (at >> shift)) {
                for (uint64_t r = at; r < row; r++) lanes_T(&L);
            } else {
                lanes_seed(&L, rng->key, row >> shift);
                for (uint64_t s = 0; s <= (row & mask); s++) lanes_T(&L);
            }
            live = 1;
        }
        if (run > nrows) run = nrows;
        /* The store leads and the step trails, with no branch between them: GCC -O2 otherwise
           keeps the lanes in memory across the loop. */
        if (out) {
            size_t r = run;
            do {
                lanes_store(&L, out, mode);
                out += 128;
                if (--r == 0) break;
                lanes_T(&L);
            } while (1);
        } else {
            for (size_t r = 1; r < run; r++) lanes_T(&L);
        }
        at = row + run - 1u;
        row += run;
        nrows -= run;
    }
    lanes_save(&L, rng);
    rng->row = at;
    rng->cached = 1u;
}

static inline void load_row(tandem_rng *rng, uint64_t row) {
    if (!rng->cached || rng->row != row) run_rows(rng, row, 1, NULL, STORE_RAW);
}

/* ---- Reads ----------------------------------------------------------------------------- */

static inline uint64_t align_pos(uint64_t pos, unsigned w) {
    return (pos + w - 1u) & ~((uint64_t)w - 1u);
}

static inline uint32_t word_at(const tandem_rng *rng, uint64_t p) {
    return rng->o[(p >> 5) & 3u][(p >> 7) & 7u];
}

/* w bits (1 <= w <= 64, a power of two) at an aligned position. */
static inline uint64_t read(tandem_rng *rng, uint64_t p, unsigned w) {
    load_row(rng, p >> 10);
    if (w == 64u) return word_at(rng, p) | ((uint64_t)word_at(rng, p + 32u) << 32);
    return (word_at(rng, p) >> (p & 31u)) & (0xffffffffu >> (32u - w));
}

/* Draw w bits: align, advance, read. */
static inline uint64_t next(tandem_rng *rng, unsigned w) {
    uint64_t p = align_pos(rng->pos, w);
    rng->pos = p + w;
    return read(rng, p, w);
}

static inline double to_f64(uint64_t raw) { return (double)(raw >> 11) * 0x1p-53; }
static inline float to_f32(uint32_t raw) { return (float)(raw >> 8) * 0x1p-24f; }

/* (raw >> 5) * 2^-11 as binary16 bits. Every such value is zero or a normal half whose
 * significand is the 11-bit integer k = raw >> 5, so the encoding is exact. */
static uint16_t to_f16_bits(uint16_t raw) {
    unsigned k = raw >> 5, m = 0;
    if (k == 0) return 0;
    while ((k >> m) > 1u) m++;
    return (uint16_t)(((m + 4u) << 10) | ((k << (10u - m)) & 0x3ffu));
}

/* floor(raw * 1112064 / 2^64), then skip the surrogate range. The partial products
 * a * m and b * m are below 2^53, so the high word is (a * m + (b * m >> 32)) >> 32. */
static uint32_t to_char(uint64_t raw) {
    uint64_t a = raw >> 32, b = raw & 0xffffffffu, m = 1112064u;
    uint64_t hi = (a * m + ((b * m) >> 32)) >> 32;
    return (uint32_t)(hi < 0xd800u ? hi : hi + 0x800u);
}

/* ---- Public: construction and transport ------------------------------------------------ */

tandem_rng tandem_from_key(const uint32_t key[4], uint64_t pos, uint32_t K) {
    tandem_rng rng;
    memset(&rng, 0, sizeof rng);
    memcpy(rng.key, key, 16);
    rng.pos = pos;
    rng.K = K ? K : TANDEM_DEFAULT_K;
    return rng;
}

tandem_rng tandem_seed(uint64_t seed_lo, uint64_t seed_hi, uint32_t K) {
    uint32_t o[4] = {0, 0, DOMAIN_SEED, 0};
    uint32_t h[4] = {(uint32_t)seed_lo, (uint32_t)(seed_lo >> 32), (uint32_t)seed_hi,
                     (uint32_t)(seed_hi >> 32)};
    tandem_F(o, h);
    return tandem_from_key(o, 0, K);
}

void tandem_key(const tandem_rng *rng, uint32_t key[4]) { memcpy(key, rng->key, 16); }
uint64_t tandem_position(const tandem_rng *rng) { return rng->pos; }
uint32_t tandem_chunk_length(const tandem_rng *rng) { return rng->K; }

bool tandem_set_position(tandem_rng *rng, uint64_t pos) {
    if (pos >> 63) return false;
    rng->pos = pos;
    rng->cached = 0u;
    return true;
}

/* ---- Public: scalar draws --------------------------------------------------------------- */

bool tandem_next_bool(tandem_rng *rng) { return next(rng, 1) != 0; }
uint8_t tandem_next_u8(tandem_rng *rng) { return (uint8_t)next(rng, 8); }
uint16_t tandem_next_u16(tandem_rng *rng) { return (uint16_t)next(rng, 16); }
uint32_t tandem_next_u32(tandem_rng *rng) { return (uint32_t)next(rng, 32); }
uint64_t tandem_next_u64(tandem_rng *rng) { return next(rng, 64); }
float tandem_next_f32(tandem_rng *rng) { return to_f32((uint32_t)next(rng, 32)); }
double tandem_next_f64(tandem_rng *rng) { return to_f64(next(rng, 64)); }
uint16_t tandem_next_f16_bits(tandem_rng *rng) { return to_f16_bits((uint16_t)next(rng, 16)); }
uint32_t tandem_next_char(tandem_rng *rng) { return to_char(next(rng, 64)); }

tandem_u128 tandem_next_u128(tandem_rng *rng) {
    uint64_t p = align_pos(rng->pos, 128);
    tandem_u128 v;
    v.lo = read(rng, p, 64);
    v.hi = read(rng, p + 64u, 64);
    rng->pos = p + 128u;
    return v;
}

void tandem_next_c32(tandem_rng *rng, float out[2]) {
    out[0] = tandem_next_f32(rng);
    out[1] = tandem_next_f32(rng);
}

void tandem_next_c64(tandem_rng *rng, double out[2]) {
    out[0] = tandem_next_f64(rng);
    out[1] = tandem_next_f64(rng);
}

/* ---- Public: fills ---------------------------------------------------------------------- */

/* After alignment to its width, every integer fill is the same little-endian byte stream:
 * single bytes up to the first row boundary, whole rows from run_rows, single bytes after. */
static void fill_raw(tandem_rng *rng, void *out, size_t n, unsigned w) {
    uint64_t p = align_pos(rng->pos, w);
    size_t nbytes = n * (w / 8u);
    char *dst = out;

    rng->pos = p + n * (uint64_t)w;
    for (; nbytes && (p & 1023u); nbytes--, p += 8u) *dst++ = (char)read(rng, p, 8);
    if (nbytes >= 128u) {
        size_t nrows = nbytes / 128u;
        run_rows(rng, p >> 10, nrows, dst, STORE_RAW);
        dst += nrows * 128u;
        p += nrows * 1024u;
        nbytes -= nrows * 128u;
    }
    for (; nbytes; nbytes--, p += 8u) *dst++ = (char)read(rng, p, 8);
}

void tandem_fill_u8(tandem_rng *rng, uint8_t *out, size_t n) { fill_raw(rng, out, n, 8); }
void tandem_fill_u16(tandem_rng *rng, uint16_t *out, size_t n) { fill_raw(rng, out, n, 16); }
void tandem_fill_u32(tandem_rng *rng, uint32_t *out, size_t n) { fill_raw(rng, out, n, 32); }
void tandem_fill_u64(tandem_rng *rng, uint64_t *out, size_t n) { fill_raw(rng, out, n, 64); }
void tandem_fill_u128(tandem_rng *rng, tandem_u128 *out, size_t n) { fill_raw(rng, out, n, 128); }

void tandem_fill_bool(tandem_rng *rng, bool *out, size_t n) {
    for (size_t i = 0; i < n; i++) out[i] = tandem_next_bool(rng);
}

/* Whole rows of a float fill, mapped on the way out; returns how many elements remain. */
static size_t fill_rows(tandem_rng *rng, void *out, size_t n, unsigned w, store_mode mode) {
    uint64_t p = rng->pos; /* at a row boundary, or n is 0 */
    size_t per_row = 1024u / w, nrows = n / per_row;
    if (nrows == 0) return n;
    run_rows(rng, p >> 10, nrows, out, mode);
    rng->pos = p + nrows * 1024u;
    return n - nrows * per_row;
}

void tandem_fill_f32(tandem_rng *rng, float *out, size_t n) {
    rng->pos = align_pos(rng->pos, 32);
    for (; n && (rng->pos & 1023u); n--) *out++ = tandem_next_f32(rng);
    size_t rest = fill_rows(rng, out, n, 32, STORE_F32);
    out += n - rest;
    for (; rest; rest--) *out++ = tandem_next_f32(rng);
}

void tandem_fill_f64(tandem_rng *rng, double *out, size_t n) {
    rng->pos = align_pos(rng->pos, 64);
    for (; n && (rng->pos & 1023u); n--) *out++ = tandem_next_f64(rng);
    size_t rest = fill_rows(rng, out, n, 64, STORE_F64);
    out += n - rest;
    for (; rest; rest--) *out++ = tandem_next_f64(rng);
}

void tandem_fill_f16_bits(tandem_rng *rng, uint16_t *out, size_t n) {
    fill_raw(rng, out, n, 16);
    for (size_t i = 0; i < n; i++) out[i] = to_f16_bits(out[i]);
}

void tandem_fill_char(tandem_rng *rng, uint32_t *out, size_t n) {
    for (size_t i = 0; i < n; i++) out[i] = tandem_next_char(rng);
}

void tandem_fill_c32(tandem_rng *rng, float *out, size_t n) { tandem_fill_f32(rng, out, 2u * n); }
void tandem_fill_c64(tandem_rng *rng, double *out, size_t n) { tandem_fill_f64(rng, out, 2u * n); }

/* ---- Public: bounded integers ------------------------------------------------------------ */

/* High word of a 64 x 64-bit product from 32-bit halves, since C99 has no 128-bit type. */
static uint64_t mulhi64(uint64_t a, uint64_t b) {
    uint64_t a0 = a & 0xffffffffu, a1 = a >> 32, b0 = b & 0xffffffffu, b1 = b >> 32;
    uint64_t mid = a1 * b0 + ((a0 * b0) >> 32);
    uint64_t mid2 = a0 * b1 + (mid & 0xffffffffu);
    return a1 * b1 + (mid >> 32) + (mid2 >> 32);
}

/* The rejection threshold (2^w mod n) is computed only when the low word is below n, which
 * keeps the division off the common path. */
uint32_t tandem_u32_below(tandem_rng *rng, uint32_t n) {
    uint64_t m = (uint64_t)tandem_next_u32(rng) * n;
    if ((uint32_t)m < n) {
        uint32_t t = (0u - n) % n;
        while ((uint32_t)m < t) m = (uint64_t)tandem_next_u32(rng) * n;
    }
    return (uint32_t)(m >> 32);
}

uint64_t tandem_u64_below(tandem_rng *rng, uint64_t n) {
    uint64_t x = tandem_next_u64(rng), lo = x * n;
    if (lo < n) {
        uint64_t t = (0u - n) % n;
        while (lo < t) {
            x = tandem_next_u64(rng);
            lo = x * n;
        }
    }
    return mulhi64(x, n);
}

/* Fills cannot know how many draws earlier elements rejected, so element i takes draw i of the
 * plain fill and consumes exactly one draw. A rejected draw retries with Lemire's rule on a
 * fallback generator, split(i) of sub(PURPOSE) of the fill's generator at position 0. The
 * constants are reserved for this and match tandem-cuda. The plain fill keeps the SIMD speed
 * and the pass over its output rarely leaves the common path. */
#define PURPOSE_BELOW32 0x424c573332ull
#define PURPOSE_BELOW64 0x424c573634ull

static tandem_rng below_fallback(const uint32_t key[4], uint32_t K, uint64_t purpose, uint64_t i) {
    tandem_rng parent = tandem_from_key(key, 0, K), sub = tandem_sub(&parent, purpose);
    return tandem_split(&sub, i);
}

#if defined(__GNUC__) || defined(__clang__)
#define COLD __attribute__((noinline))
#else
#define COLD
#endif

COLD static uint32_t retry_u32(const uint32_t key[4], uint32_t K, uint32_t n, uint32_t t,
                               uint64_t i) {
    tandem_rng r = below_fallback(key, K, PURPOSE_BELOW32, i);
    uint64_t m;
    do m = (uint64_t)tandem_next_u32(&r) * n;
    while ((uint32_t)m < t);
    return (uint32_t)(m >> 32);
}

COLD static uint64_t retry_u64(const uint32_t key[4], uint32_t K, uint64_t n, uint64_t t,
                               uint64_t i) {
    tandem_rng r = below_fallback(key, K, PURPOSE_BELOW64, i);
    uint64_t x, lo;
    do {
        x = tandem_next_u64(&r);
        lo = x * n;
    } while (lo < t);
    return mulhi64(x, n);
}

void tandem_fill_u32_below(tandem_rng *rng, uint32_t *out, size_t len, uint32_t n) {
    uint32_t key[4], K = rng->K;
    if (len == 0) return; /* the plain fill would align the position */
    memcpy(key, rng->key, 16);
    tandem_fill_u32(rng, out, len);
    for (size_t i = 0; i < len; i++) {
        uint64_t m = (uint64_t)out[i] * n;
        if ((uint32_t)m < n) {
            uint32_t t = (0u - n) % n;
            if ((uint32_t)m < t) {
                out[i] = retry_u32(key, K, n, t, i);
                continue;
            }
        }
        out[i] = (uint32_t)(m >> 32);
    }
}

void tandem_fill_u64_below(tandem_rng *rng, uint64_t *out, size_t len, uint64_t n) {
    uint32_t key[4], K = rng->K;
    if (len == 0) return;
    memcpy(key, rng->key, 16);
    tandem_fill_u64(rng, out, len);
    for (size_t i = 0; i < len; i++) {
        uint64_t lo = out[i] * n;
        if (lo < n) {
            uint64_t t = (0u - n) % n;
            if (lo < t) {
                out[i] = retry_u64(key, K, n, t, i);
                continue;
            }
        }
        out[i] = mulhi64(out[i], n);
    }
}

/* ---- Public: normals --------------------------------------------------------------------- */

/* Box-Muller without libm in the loop, so that the compiler vectorizes a block of pairs. A
 * pair (a, b) gives r = sqrt(-2 ln(1 - a)) and the normals r cos(2 pi b) and r sin(2 pi b),
 * cos first.
 *
 * ln(1 - a): 1 - a is exact and in (0, 1]. Split it as m 2^e with m in [sqrt(1/2), sqrt(2)) by
 * its exponent bits, then ln m = 2 s (1 + z/3 + z^2/5 + ...) with s = (m - 1) / (m + 1) and
 * z = s^2 <= 0.0295, a short series that keeps the relative error near the last bit even for
 * a close to 0.
 *
 * cos and sin of 2 pi b: b - q/4 for the nearest quarter turn q is exact, so the angle in
 * [-pi/4, pi/4] needs no range reduction. Taylor series give cos and sin there, and the
 * quarter turn is a swap and sign change. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif

/* One rounding per fused multiply-add where the hardware has it, plain operations otherwise.
 * Contraction is off so that every build does the same arithmetic in the vector body and in
 * the scalar remainder of a loop. */
#if defined(__FP_FAST_FMA)
#define FMA(x, y, z) fma((x), (y), (z))
#else
#define FMA(x, y, z) ((x) * (y) + (z))
#endif
#if defined(__FP_FAST_FMAF)
#define FMAF(x, y, z) fmaf((x), (y), (z))
#else
#define FMAF(x, y, z) ((x) * (y) + (z))
#endif

/* Compilers may fuse or inline differently per call site. One out-of-line body for each
 * precision keeps the scalar draws and the fills bit identical. */
#if defined(__GNUC__) || defined(__clang__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

NOINLINE static void normal_block_f64(const double *restrict u, double *restrict z, size_t m) {
#if defined(__clang__)
#pragma clang loop interleave_count(8)
#endif
    for (size_t j = 0; j < m; j++) {
        double a = u[2u * j], b = u[2u * j + 1u];

        /* 1 - a = mant 2^k with mant in [sqrt(1/2), sqrt(2)) from the bits: shifting the
         * exponent field by the bits of sqrt(1/2) makes the mantissa rollover pick k. */
        double x = 1.0 - a, mant;
        uint64_t bits, ix;
        memcpy(&bits, &x, 8);
        ix = bits + 0x00095f6200000000u;
        double nk = (double)(1023 - (int64_t)(ix >> 52)); /* -k */
        ix = (ix & 0x000fffffffffffffu) + 0x3fe6a09e00000000u;
        memcpy(&mant, &ix, 8);
        double s = (mant - 1.0) / (mant + 1.0), zz = s * s;
        double p = FMA(zz, FMA(zz, FMA(zz, FMA(zz, FMA(zz, FMA(zz, 0.08312363319426472,
                   0.09070001083303751), 0.11111433317907482), 0.14285712049336274),
                   0.2000000000566491), 0.33333333333331017), 1.0);
        /* -2 ln(1 - a) = 2 nk ln 2 - 4 s p, with ln 2 split so that nk * ln2_hi is exact. */
        double r = sqrt(FMA(nk, 1.3862943607382476, (s * -4.0) * p) + nk * 3.816429394731813e-10);

        /* Nearest quarter turn q, and the angle left over in [-pi/4, pi/4]. */
        int64_t q = (int64_t)(b * 4.0 + 0.5);
        double f = FMA(-(double)q, 0.25, b), th = f * 6.283185307179586, w = th * th;
        double hs = FMA(w, FMA(w, FMA(w, FMA(w, FMA(w, 1.5914650986900946e-10,
                    -2.5051097984389413e-08), 2.755731600073921e-06), -0.00019841269836630226),
                    0.008333333333330813), -0.16666666666666669);
        double hc = FMA(w, FMA(w, FMA(w, FMA(w, FMA(w, 2.0665708703855164e-09,
                    -2.7555858522576447e-07), 2.480158263811954e-05), -0.0013888888882156126),
                    0.04166666666663108), -0.4999999999999997);
        double sn = th * FMA(w, hs, 1.0), cs = FMA(w, hc, 1.0);

        /* Rotate by q quarter turns with bit operations: odd q swaps the two, bit 1 of q
         * negates the sine, and bit 1 of q + 1 negates the cosine. */
        uint64_t qu = (uint64_t)q, sm = (uint64_t)0 - (qu & 1u), sb, cb, xb, yb;
        memcpy(&sb, &sn, 8);
        memcpy(&cb, &cs, 8);
        xb = (sb & sm) | (cb & ~sm);
        yb = (cb & sm) | (sb & ~sm);
        xb ^= ((qu + 1u) << 62) & 0x8000000000000000u;
        yb ^= (qu << 62) & 0x8000000000000000u;
        double cx, sx;
        memcpy(&cx, &xb, 8);
        memcpy(&sx, &yb, 8);
        z[2u * j] = r * cx;
        z[2u * j + 1u] = r * sx;
    }
}

NOINLINE static void normal_block_f32(const float *restrict u, float *restrict z, size_t m) {
#if defined(__clang__)
#pragma clang loop interleave_count(8)
#endif
    for (size_t j = 0; j < m; j++) {
        float a = u[2u * j], b = u[2u * j + 1u];

        float x = 1.0f - a, mant;
        uint32_t bits, ix;
        memcpy(&bits, &x, 4);
        ix = bits + 0x004afb0du;
        float nk = (float)(127 - (int32_t)(ix >> 23)); /* -k */
        ix = (ix & 0x007fffffu) + 0x3f3504f3u;
        memcpy(&mant, &ix, 4);
        float s = (mant - 1.0f) / (mant + 1.0f), zz = s * s;
        float p = FMAF(zz, FMAF(zz, FMAF(zz, 0.14275366f, 0.20000061f), 0.33333334f), 1.0f);
        float r = sqrtf(FMAF(nk, 1.38629150390625f, (s * -4.0f) * p) + nk * 2.857213530660374e-06f);

        int32_t q = (int32_t)(b * 4.0f + 0.5f);
        float f = FMAF(-(float)q, 0.25f, b);
        /* 2 pi as a float pair, so that the angle is good to the last bit of the float. */
        float th = FMAF(f, -1.7484555e-7f, f * 6.2831855f), w = th * th;
        float hs = FMAF(w, FMAF(w, FMAF(w, 2.72499e-06f, -0.00019840087f), 0.008333332f),
                        -0.16666667f);
        float hc = FMAF(w, FMAF(w, FMAF(w, 2.4463761e-05f, -0.0013887589f), 0.04166665f), -0.5f);
        float sn = th * FMAF(w, hs, 1.0f), cs = FMAF(w, hc, 1.0f);

        uint32_t qu = (uint32_t)q, sm = (uint32_t)0 - (qu & 1u), sb, cb, xb, yb;
        memcpy(&sb, &sn, 4);
        memcpy(&cb, &cs, 4);
        xb = (sb & sm) | (cb & ~sm);
        yb = (cb & sm) | (sb & ~sm);
        xb ^= ((qu + 1u) << 30) & 0x80000000u;
        yb ^= (qu << 30) & 0x80000000u;
        float cx, sx;
        memcpy(&cx, &xb, 4);
        memcpy(&sx, &yb, 4);
        z[2u * j] = r * cx;
        z[2u * j + 1u] = r * sx;
    }
}

void tandem_normal2_f64(tandem_rng *rng, double out[2]) {
    double u[2];
    u[0] = tandem_next_f64(rng);
    u[1] = tandem_next_f64(rng);
    normal_block_f64(u, out, 1);
}

void tandem_normal2_f32(tandem_rng *rng, float out[2]) {
    float u[2];
    u[0] = tandem_next_f32(rng);
    u[1] = tandem_next_f32(rng);
    normal_block_f32(u, out, 1);
}

double tandem_normal_f64(tandem_rng *rng) {
    double z[2];
    tandem_normal2_f64(rng, z);
    return z[0];
}

float tandem_normal_f32(tandem_rng *rng) {
    float z[2];
    tandem_normal2_f32(rng, z);
    return z[0];
}

/* Pair j of a fill is elements 2j and 2j + 1 from uniforms 2j and 2j + 1 of the plain float
 * fill, so an odd n keeps the cos half of its last pair and still consumes both uniforms. The
 * uniforms come in blocks, which is the same stream as scalar draws because every draw is
 * aligned to its width. */
#define NORMAL_BLOCK 256u

void tandem_fill_normal_f64(tandem_rng *rng, double *out, size_t n) {
    double u[2u * NORMAL_BLOCK];
    size_t pairs = n / 2u;
    while (pairs) {
        size_t m = pairs < NORMAL_BLOCK ? pairs : NORMAL_BLOCK;
        tandem_fill_f64(rng, u, 2u * m);
        normal_block_f64(u, out, m);
        out += 2u * m;
        pairs -= m;
    }
    if (n % 2u) *out = tandem_normal_f64(rng);
}

void tandem_fill_normal_f32(tandem_rng *rng, float *out, size_t n) {
    float u[2u * NORMAL_BLOCK];
    size_t pairs = n / 2u;
    while (pairs) {
        size_t m = pairs < NORMAL_BLOCK ? pairs : NORMAL_BLOCK;
        tandem_fill_f32(rng, u, 2u * m);
        normal_block_f32(u, out, m);
        out += 2u * m;
        pairs -= m;
    }
    if (n % 2u) *out = tandem_normal_f32(rng);
}

/* ---- Public: random access and derived generators --------------------------------------- */

/* Element i of the fill that would start here. Works on a copy, so the cache stays put. */
static uint64_t at(const tandem_rng *rng, uint64_t i, unsigned w) {
    tandem_rng tmp = *rng;
    return read(&tmp, align_pos(rng->pos, w) + i * w, w);
}

uint32_t tandem_at_u32(const tandem_rng *rng, uint64_t i) { return (uint32_t)at(rng, i, 32); }
uint64_t tandem_at_u64(const tandem_rng *rng, uint64_t i) { return at(rng, i, 64); }
float tandem_at_f32(const tandem_rng *rng, uint64_t i) { return to_f32((uint32_t)at(rng, i, 32)); }
double tandem_at_f64(const tandem_rng *rng, uint64_t i) { return to_f64(at(rng, i, 64)); }

static tandem_rng child(const tandem_rng *rng, uint64_t counter, uint32_t domain, uint32_t aux,
                        unsigned half) {
    uint32_t o[4], h[4];
    tandem_F_keyed(rng->key, counter, domain, aux, o, h);
    return tandem_from_key(half ? h : o, 0, rng->K);
}

tandem_rng tandem_split(const tandem_rng *rng, uint64_t index) {
    return child(rng, index >> 1, DOMAIN_SPLIT, 0, (unsigned)(index & 1u));
}

tandem_rng tandem_sub(const tandem_rng *rng, uint64_t purpose) {
    return child(rng, purpose, DOMAIN_FOLD, 0, 0);
}

void tandem_fork(tandem_rng *parent, tandem_rng *children, uint64_t n) {
    uint64_t b = parent->pos >> 7;
    for (uint64_t i = 0; i < n; i++)
        children[i] = child(parent, b, DOMAIN_FORK, (uint32_t)(i >> 1), (unsigned)(i & 1u));
    parent->pos = (b + 1u) << 7;
}
