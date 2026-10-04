/* R bindings for Tandem8x32 over the vendored reference implementation (tandem.c).
 *
 * Copyright 2026 Jessica Cox. Apache License 2.0, see LICENSE.
 */
#include <R.h>
#include <Rinternals.h>
#include <R_ext/Rdynload.h>
#include <R_ext/Random.h>
#include <stdio.h>
#include <string.h>

#include "tandem.h"

#define TWO53 9007199254740992.0

/* ---- Argument parsing ------------------------------------------------------------------ */

/* An unsigned 128-bit value from a double below 2^53 or a string of decimal digits. */
static void parse_u128(SEXP x, const char *what, uint64_t *lo, uint64_t *hi) {
    if ((TYPEOF(x) == REALSXP || TYPEOF(x) == INTSXP) && XLENGTH(x) == 1) {
        double d = asReal(x);
        if (!(d >= 0 && d < TWO53) || d != (double)(uint64_t)d)
            error("%s must be an integer-valued number in [0, 2^53), or a decimal string", what);
        *lo = (uint64_t)d;
        *hi = 0;
        return;
    }
    if (TYPEOF(x) == STRSXP && XLENGTH(x) == 1) {
        const char *s = CHAR(STRING_ELT(x, 0));
        *lo = *hi = 0;
        if (!*s) error("%s must not be empty", what);
        for (; *s; s++) {
            uint64_t d = (uint64_t)(*s - '0'), carry;
            if (*s < '0' || *s > '9') error("%s must contain decimal digits only", what);
            /* (hi, lo) = 10 * (hi, lo) + d, checked for overflow of 128 bits */
            if (*hi > UINT64_MAX / 10u) error("%s exceeds 2^128 - 1", what);
            carry = ((*lo >> 32) * 10u + ((((*lo & 0xffffffffu) * 10u) + d) >> 32)) >> 32;
            *lo = *lo * 10u + d;
            if (*hi * 10u + carry < *hi) error("%s exceeds 2^128 - 1", what);
            *hi = *hi * 10u + carry;
        }
        return;
    }
    error("%s must be a single number or a single string", what);
}

static uint64_t parse_u64(SEXP x, const char *what) {
    uint64_t lo, hi;
    parse_u128(x, what, &lo, &hi);
    if (hi) error("%s exceeds 2^64 - 1", what);
    return lo;
}

static uint32_t parse_K(SEXP x) {
    double d = asReal(x);
    uint32_t K = (uint32_t)d;
    if (!(d >= 1 && d <= 65536) || (double)K != d || (K & (K - 1u)))
        error("K must be a power of two in [1, 65536]");
    return K;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Four words from four integer-valued doubles or from 32 hex digits, word 0 first. */
static void parse_key(SEXP x, uint32_t key[4]) {
    if ((TYPEOF(x) == REALSXP || TYPEOF(x) == INTSXP) && XLENGTH(x) == 4) {
        for (int w = 0; w < 4; w++) {
            double d = TYPEOF(x) == REALSXP ? REAL(x)[w] : (double)INTEGER(x)[w];
            if (!(d >= 0 && d <= 4294967295.0) || d != (double)(uint32_t)d)
                error("key words must be integer-valued numbers in [0, 2^32)");
            key[w] = (uint32_t)d;
        }
        return;
    }
    if (TYPEOF(x) == STRSXP && XLENGTH(x) == 1) {
        const char *s = CHAR(STRING_ELT(x, 0));
        if (strlen(s) != 32) error("a hex key must have 32 digits");
        for (int w = 0; w < 4; w++) {
            key[w] = 0;
            for (int i = 0; i < 8; i++) {
                int d = hex_digit(s[8 * w + i]);
                if (d < 0) error("a hex key must contain hex digits only");
                key[w] = key[w] << 4 | (uint32_t)d;
            }
        }
        return;
    }
    error("key must be four numbers or one string of 32 hex digits");
}

static size_t parse_n(SEXP x) {
    double d = asReal(x);
    if (!(d >= 0 && d < TWO53) || d != (double)(size_t)d) error("n must be a nonnegative integer");
    return (size_t)d;
}

/* ---- Generator objects ------------------------------------------------------------------ */

/* saveRDS, serialize and parallel workers keep an external pointer's tag but null its address.
 * The tag holds the transport form as bytes, key then position then K, all little-endian. The
 * position is refreshed after every call that moves it, and unwrap() rebuilds a nulled
 * generator from the tag on first use. */
#define STATE_BYTES 28
#define POS_OFFSET 16

static void put_le(unsigned char *b, uint64_t v, int bytes) {
    for (int i = 0; i < bytes; i++) b[i] = (unsigned char)(v >> (8 * i));
}

static uint64_t get_le(const unsigned char *b, int bytes) {
    uint64_t v = 0;
    for (int i = 0; i < bytes; i++) v |= (uint64_t)b[i] << (8 * i);
    return v;
}

static void finalize(SEXP ptr) {
    tandem_rng *rng = R_ExternalPtrAddr(ptr);
    if (rng) {
        R_Free(rng);
        R_ClearExternalPtr(ptr);
    }
}

static void attach(SEXP ptr, tandem_rng rng) {
    tandem_rng *p = R_Calloc(1, tandem_rng);
    *p = rng;
    R_SetExternalPtrAddr(ptr, p);
    R_RegisterCFinalizerEx(ptr, finalize, TRUE);
}

static SEXP wrap(tandem_rng rng) {
    SEXP ptr, tag, cls;
    unsigned char *b;
    tag = PROTECT(allocVector(RAWSXP, STATE_BYTES));
    b = RAW(tag);
    for (int w = 0; w < 4; w++) put_le(b + 4 * w, rng.key[w], 4);
    put_le(b + POS_OFFSET, rng.pos, 8);
    put_le(b + POS_OFFSET + 8, rng.K, 4);
    ptr = PROTECT(R_MakeExternalPtr(NULL, tag, R_NilValue));
    attach(ptr, rng);
    cls = PROTECT(mkString("tandem_rng"));
    setAttrib(ptr, R_ClassSymbol, cls);
    UNPROTECT(3);
    return ptr;
}

static tandem_rng *unwrap(SEXP ptr) {
    tandem_rng *rng;
    SEXP tag;
    if (TYPEOF(ptr) != EXTPTRSXP || !inherits(ptr, "tandem_rng"))
        error("expected a tandem_rng object");
    rng = R_ExternalPtrAddr(ptr);
    if (rng) return rng;
    tag = R_ExternalPtrTag(ptr);
    if (TYPEOF(tag) != RAWSXP || XLENGTH(tag) != STATE_BYTES) error("the generator has been freed");
    {
        const unsigned char *b = RAW(tag);
        uint32_t key[4];
        for (int w = 0; w < 4; w++) key[w] = (uint32_t)get_le(b + 4 * w, 4);
        attach(ptr, tandem_from_key(key, get_le(b + POS_OFFSET, 8), (uint32_t)get_le(b + POS_OFFSET + 8, 4)));
    }
    return R_ExternalPtrAddr(ptr);
}

/* Call after every operation that moves the position. */
static void sync_position(SEXP ptr, const tandem_rng *rng) {
    put_le(RAW(R_ExternalPtrTag(ptr)) + POS_OFFSET, rng->pos, 8);
}

static int entropy(uint64_t *lo, uint64_t *hi) {
    FILE *f = fopen("/dev/urandom", "rb");
    unsigned char b[16];
    int ok = f && fread(b, 1, 16, f) == 16;
    if (f) fclose(f);
    if (!ok) return 0;
    *lo = *hi = 0;
    for (int i = 0; i < 8; i++) {
        *lo |= (uint64_t)b[i] << (8 * i);
        *hi |= (uint64_t)b[8 + i] << (8 * i);
    }
    return 1;
}

SEXP R_tandem_new(SEXP seed, SEXP K) {
    uint64_t lo, hi;
    if (isNull(seed)) {
        if (!entropy(&lo, &hi)) {
            /* No /dev/urandom: take 128 bits from R's own generator instead. */
            GetRNGstate();
            lo = (uint64_t)(unif_rand() * 4294967296.0) | (uint64_t)(unif_rand() * 4294967296.0) << 32;
            hi = (uint64_t)(unif_rand() * 4294967296.0) | (uint64_t)(unif_rand() * 4294967296.0) << 32;
            PutRNGstate();
        }
    } else {
        parse_u128(seed, "seed", &lo, &hi);
    }
    return wrap(tandem_seed(lo, hi, parse_K(K)));
}

SEXP R_tandem_from_key(SEXP key, SEXP position, SEXP K) {
    uint32_t k[4];
    parse_key(key, k);
    return wrap(tandem_from_key(k, parse_u64(position, "position"), parse_K(K)));
}

SEXP R_tandem_key(SEXP rng) {
    uint32_t key[4];
    char s[33];
    tandem_key(unwrap(rng), key);
    snprintf(s, sizeof s, "%08x%08x%08x%08x", key[0], key[1], key[2], key[3]);
    return mkString(s);
}

SEXP R_tandem_position(SEXP rng) {
    uint64_t p = tandem_position(unwrap(rng));
    char s[21];
    if (p < (uint64_t)1 << 53) return ScalarReal((double)p);
    snprintf(s, sizeof s, "%llu", (unsigned long long)p);
    return mkString(s);
}

SEXP R_tandem_set_position(SEXP rng, SEXP position) {
    tandem_rng *g = unwrap(rng);
    g->pos = parse_u64(position, "position");
    sync_position(rng, g);
    return rng;
}

SEXP R_tandem_chunk_length(SEXP rng) { return ScalarInteger((int)tandem_chunk_length(unwrap(rng))); }

/* ---- Draws ------------------------------------------------------------------------------ */

SEXP R_tandem_runif(SEXP rng, SEXP n) {
    size_t len = parse_n(n);
    SEXP out = PROTECT(allocVector(REALSXP, (R_xlen_t)len));
    tandem_rng *g = unwrap(rng);
    tandem_fill_f64(g, REAL(out), len);
    sync_position(rng, g);
    UNPROTECT(1);
    return out;
}

SEXP R_tandem_rsingle(SEXP rng, SEXP n) {
    size_t len = parse_n(n);
    SEXP out = PROTECT(allocVector(REALSXP, (R_xlen_t)len));
    double *x = REAL(out);
    /* Fill the second half of the buffer with floats, then widen in place from the front. */
    float *f = (float *)(x + len / 2 + len % 2);
    tandem_rng *g = unwrap(rng);
    tandem_fill_f32(g, f, len);
    sync_position(rng, g);
    for (size_t i = 0; i < len; i++) x[i] = (double)f[i];
    UNPROTECT(1);
    return out;
}

SEXP R_tandem_rbits(SEXP rng, SEXP n, SEXP bits) {
    size_t len = parse_n(n);
    int w = asInteger(bits);
    SEXP out = PROTECT(allocVector(REALSXP, (R_xlen_t)len));
    double *x = REAL(out);
    tandem_rng *g = unwrap(rng);
    if (w == 32) {
        /* 32-bit words fill the first half of the buffer; widen from the back. */
        uint32_t *u = (uint32_t *)x;
        tandem_fill_u32(g, u, len);
        for (size_t i = len; i-- > 0;) x[i] = (double)u[i];
    } else if (w == 16) {
        uint16_t *u = (uint16_t *)x;
        tandem_fill_u16(g, u, len);
        for (size_t i = len; i-- > 0;) x[i] = (double)u[i];
    } else if (w == 8) {
        uint8_t *u = (uint8_t *)x;
        tandem_fill_u8(g, u, len);
        for (size_t i = len; i-- > 0;) x[i] = (double)u[i];
    } else {
        error("bits must be 8, 16, 32 or 64");
    }
    sync_position(rng, g);
    UNPROTECT(1);
    return out;
}

/* Unsigned 64-bit words have no exact R type. They go out as bit64's integer64, a double
 * vector holding the two's complement bit patterns, or as 16 lowercase hex digits each. */
static SEXP u64_hex(const uint64_t *v, size_t n) {
    SEXP out = PROTECT(allocVector(STRSXP, (R_xlen_t)n));
    char s[17];
    for (size_t i = 0; i < n; i++) {
        snprintf(s, sizeof s, "%016llx", (unsigned long long)v[i]);
        SET_STRING_ELT(out, (R_xlen_t)i, mkChar(s));
    }
    UNPROTECT(1);
    return out;
}

static SEXP u64_integer64(const uint64_t *v, size_t n) {
    SEXP out = PROTECT(allocVector(REALSXP, (R_xlen_t)n)), cls = PROTECT(mkString("integer64"));
    if (n) memcpy(REAL(out), v, n * sizeof *v);
    setAttrib(out, R_ClassSymbol, cls);
    UNPROTECT(2);
    return out;
}

SEXP R_tandem_rbits64(SEXP rng, SEXP n, SEXP hex) {
    size_t len = parse_n(n);
    tandem_rng *g = unwrap(rng);
    uint64_t *v = (uint64_t *)R_alloc(len ? len : 1, sizeof *v);
    SEXP out;
    tandem_fill_u64(g, v, len);
    sync_position(rng, g);
    out = asLogical(hex) ? u64_hex(v, len) : u64_integer64(v, len);
    return out;
}

/* Random access: element i of the fill that would start at the current position, without
 * moving it. type is 0 for u32, 1 for u64, 2 for f32 and 3 for f64. */
SEXP R_tandem_at(SEXP rng, SEXP type, SEXP index, SEXP hex) {
    const tandem_rng *g = unwrap(rng);
    int t = asInteger(type);
    SEXP out;
    R_xlen_t len = TYPEOF(index) == STRSXP ? 1 : XLENGTH(index);
    uint64_t *v = (uint64_t *)R_alloc(len ? (size_t)len : 1, sizeof *v);
    if (TYPEOF(index) == STRSXP) {
        v[0] = parse_u64(index, "i");
    } else if (TYPEOF(index) == REALSXP || TYPEOF(index) == INTSXP) {
        for (R_xlen_t k = 0; k < len; k++) {
            double d = TYPEOF(index) == REALSXP ? REAL(index)[k] : (double)INTEGER(index)[k];
            if (!(d >= 0 && d < TWO53) || d != (double)(uint64_t)d)
                error("i must hold integer-valued numbers in [0, 2^53), or be a decimal string");
            v[k] = (uint64_t)d;
        }
    } else {
        error("i must be a numeric vector or a decimal string");
    }
    if (t == 1) {
        for (R_xlen_t k = 0; k < len; k++) v[k] = tandem_at_u64(g, v[k]);
        return asLogical(hex) ? u64_hex(v, (size_t)len) : u64_integer64(v, (size_t)len);
    }
    out = PROTECT(allocVector(REALSXP, len));
    for (R_xlen_t k = 0; k < len; k++) {
        double x = t == 0 ? (double)tandem_at_u32(g, v[k])
                 : t == 2 ? (double)tandem_at_f32(g, v[k])
                          : tandem_at_f64(g, v[k]);
        REAL(out)[k] = x;
    }
    UNPROTECT(1);
    return out;
}

SEXP R_tandem_rbool(SEXP rng, SEXP n) {
    size_t len = parse_n(n);
    SEXP out = PROTECT(allocVector(LGLSXP, (R_xlen_t)len));
    int *x = LOGICAL(out);
    tandem_rng *g = unwrap(rng);
    for (size_t i = 0; i < len; i++) x[i] = tandem_next_bool(g);
    sync_position(rng, g);
    UNPROTECT(1);
    return out;
}

/* ---- Bounded integers and normals ------------------------------------------------------- */

/* n draws uniform on [0, max). */
SEXP R_tandem_sample_int(SEXP rng, SEXP n, SEXP max) {
    size_t len = parse_n(n);
    uint64_t range = parse_u64(max, "max");
    tandem_rng *g;
    SEXP out;
    if (range < 1 || range > (uint64_t)1 << 53) error("max must be in [1, 2^53]");
    g = unwrap(rng);
    if (range <= (uint64_t)1 << 31) {
        /* The fill writes u32 values, which fit R integers. */
        out = PROTECT(allocVector(INTSXP, (R_xlen_t)len));
        tandem_fill_u32_below(g, (uint32_t *)INTEGER(out), len, (uint32_t)range);
    } else if (range <= UINT32_MAX) {
        uint32_t *u = (uint32_t *)R_alloc(len ? len : 1, sizeof *u);
        out = PROTECT(allocVector(REALSXP, (R_xlen_t)len));
        tandem_fill_u32_below(g, u, len, (uint32_t)range);
        for (size_t i = 0; i < len; i++) REAL(out)[i] = (double)u[i];
    } else {
        uint64_t *u = (uint64_t *)R_alloc(len ? len : 1, sizeof *u);
        out = PROTECT(allocVector(REALSXP, (R_xlen_t)len));
        tandem_fill_u64_below(g, u, len, range);
        for (size_t i = 0; i < len; i++) REAL(out)[i] = (double)u[i];
    }
    sync_position(rng, g);
    UNPROTECT(1);
    return out;
}

SEXP R_tandem_rnorm(SEXP rng, SEXP n) {
    size_t len = parse_n(n);
    SEXP out = PROTECT(allocVector(REALSXP, (R_xlen_t)len));
    tandem_rng *g = unwrap(rng);
    tandem_fill_normal_f64(g, REAL(out), len);
    sync_position(rng, g);
    UNPROTECT(1);
    return out;
}

/* ---- Derived generators ----------------------------------------------------------------- */

SEXP R_tandem_split(SEXP rng, SEXP index) {
    return wrap(tandem_split(unwrap(rng), parse_u64(index, "index")));
}

SEXP R_tandem_sub(SEXP rng, SEXP purpose) {
    return wrap(tandem_sub(unwrap(rng), parse_u64(purpose, "purpose")));
}

SEXP R_tandem_fork(SEXP rng, SEXP n) {
    size_t len = parse_n(n);
    tandem_rng *kids = R_Calloc(len ? len : 1, tandem_rng);
    SEXP out = PROTECT(allocVector(VECSXP, (R_xlen_t)len));
    tandem_rng *g = unwrap(rng);
    tandem_fork(g, kids, len);
    sync_position(rng, g);
    for (size_t i = 0; i < len; i++) SET_VECTOR_ELT(out, (R_xlen_t)i, wrap(kids[i]));
    R_Free(kids);
    UNPROTECT(1);
    return out;
}

/* ---- Base R hook: RNGkind("user-supplied") ---------------------------------------------- */

/* R asks the hook for one double at a time. Filling a buffer of rows amortises the
 * generator's per-call cost; the values and their order are those of tandem_next_f64.
 *
 * R copies the array behind user_unif_seedloc() to and from .Random.seed around every
 * call into the generator, so that array is the authoritative state:
 *   key[4], position of the first draw in the buffer (lo, hi), K, draws already used.
 * The buffer and the generator behind it are caches of that state. user_shadow is the
 * key, position and K they were built for; a mismatch means R restored another .Random.seed.
 * The count of draws used does not matter to the buffer, so the draw path does not copy it. */
#define USER_BUF 1024
#define USER_NSEED 8
static Int32 user_seed[USER_NSEED], user_shadow[7];
static int user_valid;
static double user_buf[USER_BUF];

static void user_rebuild(void) {
    uint32_t key[4], K = user_seed[6];
    uint64_t pos = (uint64_t)user_seed[4] | (uint64_t)user_seed[5] << 32;
    if (K < 1 || K > 65536 || (K & (K - 1u)) || user_seed[7] >= USER_BUF)
        error("'.Random.seed' does not hold a valid Tandem8x32 state");
    for (int w = 0; w < 4; w++) key[w] = user_seed[w];
    {
        tandem_rng g = tandem_from_key(key, pos, K);
        tandem_fill_f64(&g, user_buf, USER_BUF);
    }
    memcpy(user_shadow, user_seed, 7 * sizeof(Int32));
    user_valid = 1;
}

static int user_nseed = USER_NSEED;

int *user_unif_nseed(void) { return &user_nseed; }

int *user_unif_seedloc(void) { return (int *)user_seed; }

/* set.seed(s) hands the hook 50 rounds of s <- 69069 s + 1 (mod 2^32). Undo them, so that
 * set.seed(s) is the generator tandem(s) for every s in the Int32 range. */
void user_unif_init(Int32 seed) {
    const uint32_t inv = 0xa5e2a705u; /* 69069^-1 mod 2^32 */
    tandem_rng g;
    for (int j = 0; j < 50; j++) seed = (seed - 1u) * inv;
    g = tandem_seed(seed, 0, TANDEM_DEFAULT_K);
    for (int w = 0; w < 4; w++) user_seed[w] = g.key[w];
    user_seed[4] = user_seed[5] = user_seed[7] = 0;
    user_seed[6] = TANDEM_DEFAULT_K;
    user_valid = 0;
}

static int user_stale(void) {
    Int32 diff = 0;
    for (int i = 0; i < 7; i++) diff |= user_seed[i] ^ user_shadow[i];
    return diff != 0;
}

double *user_unif_rand(void) {
    Int32 used;
    if (!user_valid || user_stale()) user_rebuild();
    used = user_seed[7]++;
    if (user_seed[7] == USER_BUF) {
        /* The buffer is spent: move the state to the start of the next one. The next call
         * refills user_buf, so the draw returned here stays valid until then. */
        uint64_t pos = ((uint64_t)user_seed[4] | (uint64_t)user_seed[5] << 32) + 64u * USER_BUF;
        user_seed[4] = (Int32)pos;
        user_seed[5] = (Int32)(pos >> 32);
        user_seed[7] = 0;
        user_valid = 0;
    }
    return &user_buf[used];
}

/* ---- Registration ----------------------------------------------------------------------- */

static const R_CallMethodDef calls[] = {
    {"R_tandem_new", (DL_FUNC)&R_tandem_new, 2},
    {"R_tandem_from_key", (DL_FUNC)&R_tandem_from_key, 3},
    {"R_tandem_key", (DL_FUNC)&R_tandem_key, 1},
    {"R_tandem_position", (DL_FUNC)&R_tandem_position, 1},
    {"R_tandem_set_position", (DL_FUNC)&R_tandem_set_position, 2},
    {"R_tandem_chunk_length", (DL_FUNC)&R_tandem_chunk_length, 1},
    {"R_tandem_runif", (DL_FUNC)&R_tandem_runif, 2},
    {"R_tandem_rsingle", (DL_FUNC)&R_tandem_rsingle, 2},
    {"R_tandem_rbits", (DL_FUNC)&R_tandem_rbits, 3},
    {"R_tandem_rbits64", (DL_FUNC)&R_tandem_rbits64, 3},
    {"R_tandem_at", (DL_FUNC)&R_tandem_at, 4},
    {"R_tandem_rbool", (DL_FUNC)&R_tandem_rbool, 2},
    {"R_tandem_sample_int", (DL_FUNC)&R_tandem_sample_int, 3},
    {"R_tandem_rnorm", (DL_FUNC)&R_tandem_rnorm, 2},
    {"R_tandem_split", (DL_FUNC)&R_tandem_split, 2},
    {"R_tandem_sub", (DL_FUNC)&R_tandem_sub, 2},
    {"R_tandem_fork", (DL_FUNC)&R_tandem_fork, 2},
    {NULL, NULL, 0}};

/* RNGkind("user-supplied") looks these two up by name among the registered symbols. */
static const R_CMethodDef cmethods[] = {
    {"user_unif_rand", (DL_FUNC)&user_unif_rand, 0},
    {"user_unif_init", (DL_FUNC)&user_unif_init, 1},
    {"user_unif_nseed", (DL_FUNC)&user_unif_nseed, 0},
    {"user_unif_seedloc", (DL_FUNC)&user_unif_seedloc, 0},
    {NULL, NULL, 0}};

void R_init_tandemrng(DllInfo *dll) {
    R_registerRoutines(dll, cmethods, calls, NULL, NULL);
    R_useDynamicSymbols(dll, FALSE);
}
