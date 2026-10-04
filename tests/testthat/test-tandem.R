vectors <- jsonlite::fromJSON(test_path("vectors.json"), simplifyVector = FALSE)
hex_words <- function(ws) paste(unlist(ws), collapse = "")
hex_num <- function(h) as.numeric(paste0("0x", h))
key <- hex_words(vectors$key)
K <- vectors$K

dump <- function(name, what, size) {
  path <- test_path("data", name)
  readBin(path, what, n = file.size(path) %/% size, size = size, endian = "little")
}

test_that("stream words and draws match the specification", {
  rng <- tandem_from_key(key, 0, K)
  words <- tandem_rbits(rng, 36, 32)
  for (s in vectors$stream_words) {
    want <- hex_num(unlist(s$words))
    expect_equal(words[s$first_word + 1:4], want)
  }
  d <- vectors$draws_from_position_0
  u <- tandem_runif(tandem_from_key(key, 0, K), 17)
  for (i in names(d$Float64)) expect_identical(u[as.integer(i) + 1], d$Float64[[i]])
  f <- tandem_rsingle(tandem_from_key(key, 0, K), 3)
  expect_equal(f[3], d$Float32[["2"]], tolerance = 1e-7)
  b <- tandem_rbool(tandem_from_key(key, 0, K), 129)
  for (i in names(d$Bool)) expect_identical(b[as.integer(i) + 1], d$Bool[[i]] == 1)
})

test_that("derived keys match the specification", {
  rng <- tandem_from_key(key, 0, K)
  k <- vectors$derived_keys
  expect_identical(tandem_key(tandem_split(rng, 0)), hex_words(k$split_child_0))
  expect_identical(tandem_key(tandem_split(rng, 1)), hex_words(k$split_child_1))
  expect_identical(tandem_key(tandem_sub(rng, 7)), hex_words(k$purpose_7))
  kids <- tandem_fork(rng, 2)
  expect_length(kids, 2)
  expect_identical(tandem_key(kids[[1]]), hex_words(k$fork_child_0_at_block_0))
  expect_identical(tandem_position(kids[[1]]), 0)
  expect_identical(tandem_chunk_length(kids[[1]]), K)
  expect_identical(tandem_position(rng), 128)
  expect_identical(tandem_position(tandem_split(rng, 0)), 0)
})

test_that("seed whitening matches the specification", {
  s <- vectors$seed_whitening
  rng <- tandem(s$seed, K)
  expect_identical(tandem_key(rng), hex_words(s$key))
  expect_identical(tandem_key(tandem(as.character(s$seed), K)), hex_words(s$key))
  u <- tandem_runif(rng, 17)
  for (i in names(s$Float64)) expect_identical(u[as.integer(i) + 1], s$Float64[[i]])
  w <- tandem_rbits(tandem(s$seed, K), 1, 32)
  expect_identical(w, hex_num(s$UInt32[["0"]]))
})

test_that("fills agree with the Julia dumps", {
  k1234 <- "00000001000000020000000300000004"
  expect_identical(tandem_runif(tandem(42), 4096), dump("seed42_K32_f64.bin", "double", 8))
  f32 <- dump("seed42_K32_f32.bin", "double", 4)
  expect_identical(tandem_rsingle(tandem(42), 4096), f32)
  u32 <- dump("k1234_K32_u32.bin", "integer", 4)
  expect_identical(tandem_rbits(tandem_from_key(k1234, 0, 32), 65536, 32), u32 %% 2^32)
  u32k8 <- dump("k1234_K8_u32.bin", "integer", 4)
  expect_identical(tandem_rbits(tandem_from_key(k1234, 0, 8), 16384, 32), u32k8 %% 2^32)
  u8 <- dump("seed42_K32_u8.bin", "integer", 1)
  expect_identical(tandem_rbits(tandem(42), 8192, 8), u8 %% 256)
  u16 <- dump("seed42_K32_f16bits.bin", "integer", 2) %% 2^16
  r <- tandem_rbits(tandem(42), 8, 16)
  expect_length(r, 8)
  expect_identical(tandem_rbool(tandem(42), 4096), dump("seed42_K32_bool.bin", "integer", 1) == 1)
})

test_that("positions align to the width and fills can start anywhere", {
  rng <- tandem(42)
  tandem_rbits(rng, 1, 8)
  expect_identical(tandem_position(rng), 8)
  x <- tandem_runif(rng, 1)
  expect_identical(tandem_position(rng), 128)
  fresh <- tandem(42)
  tandem_set_position(fresh, 64)
  expect_identical(tandem_runif(fresh, 1), x)
  whole <- tandem_runif(tandem(7), 3000)
  for (start in c(0, 1, 5, 15, 16, 17, 500, 1024)) {
    r <- tandem(7)
    tandem_set_position(r, 64 * start)
    expect_identical(tandem_runif(r, 3000 - start), whole[(start + 1):3000])
  }
  big <- tandem_from_key(key, "18446744073709551000", K)
  expect_identical(tandem_position(big), "18446744073709551000")
})

test_that("state round-trips through a plain list", {
  rng <- tandem(42, 8)
  tandem_runif(rng, 5)
  s <- tandem_state(rng)
  expect_identical(s, list(key = tandem_key(rng), position = "320", K = 8L))
  expect_identical(tandem_runif(tandem_restore(s), 4), tandem_runif(rng, 4))
  big <- tandem_from_key(key, "18446744073709551000", K)
  expect_identical(tandem_state(big)$position, "18446744073709551000")
  expect_identical(tandem_position(tandem_restore(tandem_state(big))), "18446744073709551000")
})

test_that("a generator survives serialization at its current position", {
  rng <- tandem(42)
  tandem_runif(rng, 5)
  path <- tempfile(fileext = ".rds")
  on.exit(unlink(path), add = TRUE)
  saveRDS(rng, path)
  tandem_runif(rng, 1)
  back <- readRDS(path)
  expect_s3_class(back, "tandem_rng")
  expect_identical(tandem_position(back), 320)
  expect_identical(tandem_runif(back, 3), tandem_runif(tandem_from_key(tandem_key(rng), 320), 3))
  # The saved copy and the live generator are independent.
  expect_identical(tandem_position(rng), 384)
  expect_identical(tandem_position(back), 512)
  both <- unserialize(serialize(list(a = rng, b = rng), NULL))
  tandem_runif(both$a, 2)
  expect_identical(tandem_position(both$b), 512)
})

test_that("generators cross process boundaries", {
  skip_if_not_installed("callr")
  skip_if_not(nzchar(system.file(package = "tandemrng")), "tandemrng is not installed")
  rng <- tandem(42)
  tandem_runif(rng, 5)
  got <- callr::r(function(g) tandemrng::tandem_runif(g, 3), args = list(g = rng))
  expect_identical(got, tandem_runif(tandem_from_key(tandem_key(rng), 320), 3))
})

test_that("generators come back from forked workers with their advanced position", {
  skip_on_os("windows")
  skip_if_not_installed("parallel")
  rng <- tandem(7)
  kids <- parallel::mclapply(0:1, function(i) {
    g <- tandem_split(rng, i)
    tandem_runif(g, 2)
    g
  }, mc.cores = 2)
  for (i in 0:1) {
    expect_identical(tandem_position(kids[[i + 1]]), 128)
    expect_identical(tandem_runif(kids[[i + 1]], 1),
                     tandem_runif(tandem_split(rng, i), 3)[3])
  }
})

test_that("arguments are checked", {
  expect_error(tandem(-1), "seed must be")
  expect_error(tandem(1.5), "seed must be")
  expect_error(tandem("12x"), "decimal digits")
  expect_error(tandem("340282366920938463463374607431768211455"), NA)
  expect_error(tandem("340282366920938463463374607431768211456"), "exceeds")
  expect_error(tandem(1, K = 3), "power of two")
  expect_error(tandem_from_key("0123"), "32 digits")
  expect_error(tandem_rbits(tandem(1), 4, 7), "8, 16, 32 or 64")
  expect_error(tandem_key(1), "tandem_rng")
  expect_s3_class(tandem(), "tandem_rng")
  expect_false(identical(tandem_key(tandem()), tandem_key(tandem())))
  expect_output(print(tandem(42)), "Tandem8x32-K32 key 421d21eb")
})

test_that("the base R hook draws from tandem(seed)", {
  old <- RNGkind("user-supplied")
  on.exit(RNGkind(old[1]), add = TRUE)
  set.seed(42)
  expect_identical(runif(5), tandem_runif(tandem(42), 5))
  set.seed(2147483647)
  expect_identical(runif(2), tandem_runif(tandem(2147483647), 2))
})

test_that("the base R hook state lives in .Random.seed", {
  old <- RNGkind("user-supplied")
  on.exit(RNGkind(old[1]), add = TRUE)
  seed <- function() get(".Random.seed", globalenv())
  restore <- function(s) assign(".Random.seed", s, globalenv())
  set.seed(42)
  # Key, position, K and the draws used: 8 words after the kind code.
  expect_length(seed(), 9)
  words <- hex_num(substring(tandem_key(tandem(42)), c(1, 9, 17, 25), c(8, 16, 24, 32)))
  expect_identical(seed()[2:5] %% 2^32, words)
  # Restoring repeats the draws, including across the 1024-draw buffer edge.
  for (used in c(0, 3, 1000, 1023, 1500)) {
    set.seed(42)
    runif(used)
    s <- seed()
    a <- runif(2500)
    restore(s)
    expect_identical(runif(2500), a)
  }
  # A restored state equals the unbroken stream.
  set.seed(42)
  whole <- runif(3000)
  set.seed(42)
  runif(1500)
  s <- seed()
  runif(10)
  restore(s)
  expect_identical(runif(1500), whole[1501:3000])
  # A saved state restores after set.seed has replaced it.
  set.seed(1)
  s <- seed()
  a <- runif(5)
  set.seed(7)
  restore(s)
  expect_identical(runif(5), a)
})

k1234 <- "00000001000000020000000300000004"
u64_bytes <- function() {
  path <- test_path("data", "k1234_K32_u64.bin")
  readBin(path, "raw", file.size(path))
}

test_that("64-bit words match the Julia dump as integer64", {
  skip_if_not_installed("bit64")
  x <- tandem_rbits(tandem_from_key(k1234, 0, 32), 2048, 64)
  expect_s3_class(x, "integer64")
  bits <- x
  attributes(bits) <- NULL
  expect_identical(writeBin(bits, raw(), endian = "little"), u64_bytes())
  rng <- tandem_from_key(k1234, 0, 32)
  tandem_rbits(rng, 3, 64)
  expect_identical(tandem_position(rng), 192)
  expect_length(tandem_rbits(rng, 0, 64), 0)
})

test_that("64-bit words match the Julia dump as hex without bit64", {
  testthat::local_mocked_bindings(bit64_available = function() FALSE)
  x <- tandem_rbits(tandem_from_key(k1234, 0, 32), 2048, 64)
  expect_type(x, "character")
  bytes <- matrix(as.character(u64_bytes()), nrow = 8)
  want <- apply(bytes[8:1, ], 2, paste, collapse = "")
  expect_identical(x, want)
  expect_identical(tandem_rbits(tandem(1), 0, 64), character())
})

test_that("a 64-bit word is two consecutive 32-bit words", {
  testthat::local_mocked_bindings(bit64_available = function() FALSE)
  w32 <- tandem_rbits(tandem_from_key(k1234, 0, 32), 6, 32)
  w64 <- tandem_rbits(tandem_from_key(k1234, 0, 32), 3, 64)
  hex32 <- function(w) sprintf("%04x%04x", as.integer(w %/% 65536), as.integer(w %% 65536))
  expect_identical(w64, paste0(hex32(w32[c(2, 4, 6)]), hex32(w32[c(1, 3, 5)])))
})

test_that("random access equals the matching fill, anywhere in the stream", {
  for (K in c(8, 32)) {
    rng <- tandem_from_key(k1234, 0, K)
    tandem_set_position(rng, 64 * 37)
    idx <- c(0, 1, 2, 31, 32, 33, 1000, 2047)
    at_pos <- tandem_position(rng)
    fill <- function(f, ...) f(tandem_from_key(k1234, at_pos, K), 2048, ...)
    expect_identical(tandem_at(rng, "f64", idx), fill(tandem_runif)[idx + 1])
    expect_identical(tandem_at(rng, "f32", idx), fill(tandem_rsingle)[idx + 1])
    expect_identical(tandem_at(rng, "u32", idx), fill(tandem_rbits, 32)[idx + 1])
    expect_identical(tandem_position(rng), at_pos)
  }
  expect_identical(tandem_at(tandem(42), "f64", 3), tandem_runif(tandem(42), 4)[4])
  expect_identical(tandem_at(tandem(42), "f64", "3"), tandem_at(tandem(42), "f64", 3))
  expect_length(tandem_at(tandem(1), "u32", numeric()), 0)
})

test_that("random access of 64-bit words matches the Julia dump", {
  rng <- tandem_from_key(k1234, 0, 32)
  idx <- c(0, 1, 7, 100, 2047)
  w <- tandem_at(rng, "u64", idx)
  want <- tandem_rbits(tandem_from_key(k1234, 0, 32), 2048, 64)[idx + 1]
  expect_identical(w, want)
  testthat::local_mocked_bindings(bit64_available = function() FALSE)
  hex <- tandem_at(rng, "u64", idx)
  expect_type(hex, "character")
  expect_identical(hex, tandem_rbits(tandem_from_key(k1234, 0, 32), 2048, 64)[idx + 1])
})


# Fixtures from tandem-c's tests/cross_fill_below.h and cross_normal.h, which tandem-c generates
# from the CUDA port's core.hpp (tools/gen_cross_fixtures.R converts them). Every case starts
# from tandem(42) after one bit draw, which leaves the position unaligned. The bounded ranges
# above 2^31 and 2^63 reject about half of the draws, so they cover the fallback stream.
cross <- jsonlite::fromJSON(test_path("data", "cross_bounded.json"), simplifyVector = FALSE)
after_bit <- function() {
  rng <- tandem(42)
  tandem_rbool(rng, 1)
  rng
}

test_that("bounded fills match the CUDA core", {
  # tandem_sample_int draws 64-bit words only for ranges above 2^32 - 1, and the values must fit
  # doubles, so of the 64-bit cases only 10^12 applies.
  for (cases in list(cross$fill_u32, cross$fill_u64[3])) {
    for (case in cases) {
      rng <- after_bit()
      got <- tandem_sample_int(rng, 64, case$n)
      expect_equal(as.numeric(got), as.numeric(unlist(case$want)))
      expect_identical(tandem_position(rng), as.numeric(case$end_pos))
    }
  }
})

test_that("normals match the CUDA core", {
  rng <- after_bit()
  expect_equal(tandem_rnorm(rng, 128), unlist(cross$normal), tolerance = 1e-12)
  expect_identical(tandem_position(rng), as.numeric(cross$normal_end_pos))
})

test_that("an odd count of normals still consumes whole pairs", {
  rng <- after_bit()
  expect_equal(tandem_rnorm(rng, 127), unlist(cross$normal)[1:127], tolerance = 1e-12)
  expect_identical(tandem_position(rng), as.numeric(cross$normal_end_pos))
})

test_that("pair j of normals is Box-Muller of uniform draws 2j and 2j + 1", {
  u <- tandem_runif(tandem(42), 20)
  a <- u[c(TRUE, FALSE)]
  b <- u[c(FALSE, TRUE)]
  r <- sqrt(-2 * log(1 - a))
  z <- as.vector(rbind(r * cos(2 * pi * b), r * sin(2 * pi * b)))
  expect_equal(tandem_rnorm(tandem(42), 20), z, tolerance = 1e-12)
})
