/* Test the actual wrapper with a canary immediately after its requested output extent. */
#include <R.h>
#include <Rinternals.h>
#include <stdint.h>
#include <string.h>

static const uint64_t guard = UINT64_C(0x6a09e667f3bcc909);
static SEXP guarded_allocVector(SEXPTYPE type, R_xlen_t n) {
    SEXP out = Rf_allocVector(type, n + (type == REALSXP));
    if (type == REALSXP) memcpy(REAL(out) + n, &guard, sizeof guard);
    return out;
}
#undef allocVector
#define allocVector guarded_allocVector
#include "../src/tandem_r.c"
#undef allocVector

SEXP check_rsingle_bounds(SEXP n) {
    SEXP seed = PROTECT(ScalarReal(42)), K = PROTECT(ScalarReal(32));
    SEXP rng = PROTECT(R_tandem_new(seed, K));
    SEXP out = PROTECT(R_tandem_rsingle(rng, n));
    uint64_t actual;
    memcpy(&actual, REAL(out) + (size_t)asReal(n), sizeof actual);
    UNPROTECT(4);
    return ScalarLogical(actual == guard);
}
