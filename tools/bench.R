# Throughput of 2^22 doubles: tandem_runif, base runif with Tandem as the user-supplied
# generator, base runif with Mersenne-Twister, tandem_rnorm, base rnorm (inversion), tandem_rexp and
# base rexp.
library(tandemrng)

n <- 2^22
bytes <- 8 * n
# Sys.time resolves microseconds. system.time resolves 1 ms, and a fill of 2^22 doubles takes a
# few, which rounded the figures.
best <- function(f, runs = 5) {
  f()
  min(vapply(seq_len(runs), function(i) {
    t0 <- Sys.time()
    f()
    as.numeric(Sys.time() - t0, units = "secs")
  }, numeric(1)))
}

rng <- tandem(42)
RNGkind("user-supplied")
set.seed(42)
user <- bytes / best(function() runif(n)) / 2^30
RNGkind("Mersenne-Twister")
set.seed(42)
rows <- c(
  "tandem_runif(rng, n)" = bytes / best(function() tandem_runif(rng, n)) / 2^30,
  "runif(n), Tandem user-supplied" = user,
  "runif(n), Mersenne-Twister" = bytes / best(function() runif(n)) / 2^30,
  "tandem_rnorm(rng, n)" = bytes / best(function() tandem_rnorm(rng, n)) / 2^30,
  "rnorm(n), Mersenne-Twister" = bytes / best(function() rnorm(n)) / 2^30,
  "tandem_rexp(rng, n)" = bytes / best(function() tandem_rexp(rng, n)) / 2^30,
  "rexp(n), Mersenne-Twister" = bytes / best(function() rexp(n)) / 2^30
)
for (name in names(rows)) cat(sprintf("%-30s %6.2f GiB/s\n", name, rows[[name]]))
