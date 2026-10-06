# The spec's conformance files, copies of tandem-spec f420545 conformance/*.json that CI checks
# byte for byte, and the items of its conformance/CHECKLIST.md at b31af72. This package offers no scalar
# bounded draw, no Float32 normals or exponentials and no complex draws, so below.json, the
# Float32 cases and the items on Box-Muller pairs, odd n and complex draws do not apply.

conformance <- function(name) {
  jsonlite::fromJSON(test_path("conformance", paste0(name, ".json")), simplifyVector = FALSE)
}
of_kind <- function(name, kinds) Filter(function(c) c$kind %in% kinds, conformance(name)$cases)
named <- function(cases, id) Filter(function(c) endsWith(c$id, paste0(" ", id)), cases)[[1]]
hex_num <- function(h) as.numeric(sprintf("0x%s", as.character(unlist(h))))
# Doubles from the 16 hex digits of their bit pattern, most significant first.
from_bits <- function(h) {
  h <- unlist(h)
  if (!length(h)) return(double())
  hex <- substring(paste(h, collapse = ""), seq(1, by = 2, length.out = 8 * length(h)),
                   seq(2, by = 2, length.out = 8 * length(h)))
  readBin(as.raw(strtoi(hex, 16L)), "double", length(h), endian = "big")
}
generator <- function(case) tandem_from_key(paste(unlist(case$key), collapse = ""), case$start, case$K)
end_of <- function(case) as.numeric(case$end %||% (64 * ceiling(case$start / 64) + 64 * case$n))
cuts <- function(n) unique(Filter(function(k) k > 0 && k < n, c(1, 7, 20, 21, n - 1)))

# Checks one fill case: the whole fill, pieces filled in order on one generator, and fills of one
# element in sequence, which are the scalar draws.
check_fill <- function(case, fill, want) {
  rng <- generator(case)
  expect_identical(fill(rng, case$n), want, label = case$id)
  if (!is.null(case$end)) expect_identical(tandem_position(rng), as.numeric(case$end), label = case$id)
  for (k in cuts(case$n)) {
    rng <- generator(case)
    expect_identical(c(fill(rng, k), fill(rng, case$n - k)), want, label = paste(case$id, "cut", k))
  }
  rng <- generator(case)
  expect_identical(unlist(lapply(seq_len(case$n), function(i) fill(rng, 1))) %||% want[0], want,
                   label = paste(case$id, "scalar"))
}

# tandem_below takes the width from the range, so a 64-bit case applies when its range needs 64
# bits and fits a double. An empty fill does not depend on the width.
bounded <- Filter(function(c) {
  r <- hex_num(c$range)
  c$n == 0 || c$kind == "fill_below_u32" || (r > 2^32 && r <= 2^53)
}, conformance("fill_below")$cases)

test_that("bounded fills equal the conformance cases, cut and one element at a time", {
  for (case in bounded) {
    range <- hex_num(case$range)
    want <- hex_num(case$values)
    check_fill(case, function(rng, n) as.numeric(tandem_below(rng, n, range)), want)
    check_fill(case, function(rng, n) as.numeric(tandem_sample_int(rng, n, range)), want + 1)
  }
})

test_that("a bounded fallback is keyed by the global draw index", {
  cases <- conformance("fill_below")$cases
  at <- hex_num(named(cases, "CROSS_BELOW32_AT[4]")$values)
  expect_identical(at[1:63], hex_num(named(cases, "CROSS_BELOW32[4]")$values)[2:64])
  expect_gt(named(cases, "CROSS_BELOW32_AT[4]")$rejected, 0)
})

test_that("the word width follows the range, 32 bits up to 2^32 inclusive", {
  cases <- conformance("fill_below")$cases
  rng <- generator(named(cases, "CROSS_BELOW32[3]"))
  got <- as.numeric(tandem_below(rng, 64, 1000))
  expect_identical(got, hex_num(named(cases, "CROSS_BELOW32[3]")$values))
  expect_false(identical(got, hex_num(named(cases, "CROSS_BELOW64[3]")$values)))
  # A range of 2^32 never rejects, so the value is the 32-bit word itself.
  expect_identical(tandem_below(tandem(42), 50, 4294967296), tandem_rbits(tandem(42), 50, 32))
  # 2^32 + 1 reads 64-bit words, which advance the position twice as far.
  rng <- tandem(42)
  tandem_below(rng, 50, 4294967297)
  expect_identical(tandem_position(rng), 50 * 64)
})

test_that("Float64 normals equal the conformance cases, misses included", {
  for (case in of_kind("normal", "fill_normal_f64")) {
    check_fill(case, tandem_rnorm, from_bits(case$values))
  }
  cases <- conformance("normal")$cases
  expect_identical(from_bits(named(cases, "CROSS_NORMAL[1]")$values)[1:63],
                   from_bits(named(cases, "CROSS_NORMAL[0]")$values)[2:64])
})

test_that("Float64 exponentials equal the conformance cases", {
  for (case in of_kind("exponential", "fill_exponential_f64")) {
    check_fill(case, tandem_rexp, from_bits(case$values))
  }
})

test_that("weighted choice equals the conformance tables, indices and positions", {
  for (case in conformance("choice")$cases) {
    w <- from_bits(case$weights)
    table <- tandem_choice_table(w)
    parts <- .Call(tandemrng:::R_tandem_choice_parts, table)
    expect_identical(parts[[1]], case$capacity)
    if (!is.null(case$cut)) {
      expect_identical(parts[[2]], unlist(case$cut))
      expect_identical(parts[[3]], strtoi(unlist(case$alias), 16L))
    }
    want <- as.integer(hex_num(case$values)) + 1L
    case$end <- end_of(case)
    check_fill(case, function(rng, n) tandem_sample_int(rng, n, length(w), table), want)
  }
  cases <- conformance("choice")$cases
  expect_identical(hex_num(named(cases, "CROSS_CHOICE[1]")$values)[1:63],
                   hex_num(named(cases, "CROSS_CHOICE[0]")$values)[2:64])
  for (w in list(c(1, -1), c(1, NaN), c(1, Inf), c(0, -0))) expect_error(tandem_choice_table(w))
})

test_that("the empty fills of every derived draw keep or align the position as the spec says", {
  # Bounded and exponential fills keep the position; normal and choice fills align it to 64.
  empty <- Filter(function(c) c$n == 0, c(conformance("fill_below")$cases, conformance("normal")$cases,
                                          conformance("exponential")$cases, conformance("choice")$cases))
  expect_length(empty, 7)
  draw <- list(fill_below_u32 = function(rng, c) tandem_below(rng, 0, hex_num(c$range)),
               fill_below_u64 = function(rng, c) tandem_below(rng, 0, hex_num(c$range)),
               fill_normal_f64 = function(rng, c) tandem_rnorm(rng, 0),
               fill_exponential_f64 = function(rng, c) tandem_rexp(rng, 0),
               fill_choice = function(rng, c) tandem_sample_int(rng, 0, length(c$weights),
                                                                from_bits(c$weights)))
  for (case in empty) {
    f <- draw[[case$kind]]
    if (is.null(f)) next
    rng <- generator(case)
    expect_length(f(rng, case), 0)
    expect_identical(tandem_position(rng), as.numeric(case$end), label = case$id)
  }
})

test_that("the stream dumps and long outputs have the conformance hashes", {
  h <- conformance("hashes")
  for (s in h$streams) {
    expect_identical(unname(tools::sha256sum(test_path("data", basename(s$file)))), s$sha256)
  }
  dump <- function(id) Filter(function(d) d$id == id, h$dumps)[[1]]
  normals <- dump("tools/dump_normals.c")
  bytes <- unlist(lapply(normals$starts, function(p) {
    rng <- tandem_from_key(paste(unlist(normals$key), collapse = ""), p, normals$K)
    writeBin(tandem_rnorm(rng, normals$draws[[1]]$n), raw(), endian = "little")
  }))
  expect_identical(tools::sha256sum(bytes = bytes), normals$sha256)
  # The exponential dump interleaves Float32 fills, which R lacks, so the C library hashes it.
  expect_identical(.Call(tandemrng:::R_tandem_exponential_hash), dump("tools/dump_exponentials.c")$fnv1a)
})

test_that("start positions stop below 2^63 and a draw at 2^63 - 1 passes it", {
  k <- "00000001000000020000000300000004"
  rng <- tandem_from_key(k, "9223372036854775807")
  expect_error(tandem_set_position(rng, "9223372036854775808"), "below 2\\^63")
  expect_error(tandem_set_position(rng, "18446744073709551615"), "below 2\\^63")
  expect_error(tandem_from_key(k, "9223372036854775808"), "below 2\\^63")
  expect_identical(tandem_position(rng), "9223372036854775807")
  tandem_rbits(rng, 1, 64)
  expect_identical(tandem_position(rng), "9223372036854775872")
  # A fill cannot reach 2^64 from a start below 2^63: one call moves fewer than 2^60 bits.
})
