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

test_that("arguments are checked", {
  expect_error(tandem(-1), "seed must be")
  expect_error(tandem(1.5), "seed must be")
  expect_error(tandem("12x"), "decimal digits")
  expect_error(tandem("340282366920938463463374607431768211455"), NA)
  expect_error(tandem("340282366920938463463374607431768211456"), "exceeds")
  expect_error(tandem(1, K = 3), "power of two")
  expect_error(tandem_from_key("0123"), "32 digits")
  expect_error(tandem_rbits(tandem(1), 4, 64), "8, 16 or 32")
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
