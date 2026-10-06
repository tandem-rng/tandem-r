# Throughput of 2^22 draws in GiB/s of output: the Tandem fills, base R with Tandem as the
# user-supplied generator, and base R with Mersenne-Twister. Doubles count 8 bytes, integers 4.
library(tandemrng)

n <- 2^22
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
gibs <- function(f, size) size * n / best(f) / 2^30

# R's fastest normal method on Mersenne-Twister, used for both base R columns.
kinds <- c("Inversion", "Box-Muller", "Kinderman-Ramage", "Ahrens-Dieter")
RNGkind("Mersenne-Twister")
set.seed(42)
by_kind <- vapply(kinds, function(k) {
  RNGkind(normal.kind = k)
  gibs(function() rnorm(n), 8)
}, numeric(1))
normal_kind <- kinds[which.max(by_kind)]

rng <- tandem(42)
table <- tandem_choice_table(seq_len(1000))
prob <- as.double(seq_len(1000))
base <- list(
  runif = function() runif(n),
  rnorm = function() rnorm(n),
  rexp = function() rexp(n),
  sample = function() sample.int(1000L, n, replace = TRUE),
  weighted = function() sample.int(1000L, n, replace = TRUE, prob = prob)
)
size <- c(8, 8, 8, 4, 4)
fills <- list(
  function() tandem_runif(rng, n),
  function() tandem_rnorm(rng, n),
  function() tandem_rexp(rng, n),
  function() tandem_sample_int(rng, n, 1000L),
  function() tandem_sample_int(rng, n, 1000L, table)
)
base_rates <- function(kind) {
  RNGkind(kind, normal.kind = normal_kind)
  set.seed(42)
  mapply(gibs, base, size)
}
user <- base_rates("user-supplied")
mt <- base_rates("Mersenne-Twister")
tandem_rates <- mapply(gibs, fills, size)

labels <- c("runif", sprintf("rnorm, %s", normal_kind), "rexp", "sample.int(1000)",
            "sample.int(1000), weighted")
cat(sprintf("%-34s %8s %14s %18s\n", "GiB/s", "Tandem", "user-supplied", "Mersenne-Twister"))
for (i in seq_along(labels)) {
  cat(sprintf("%-34s %8.2f %14.2f %18.2f\n", labels[i], tandem_rates[i], user[i], mt[i]))
}
cat("rnorm on Mersenne-Twister by normal.kind:\n")
for (k in kinds) cat(sprintf("  %-18s %6.2f\n", k, by_kind[[k]]))
