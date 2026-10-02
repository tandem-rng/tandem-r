<p align="center"><img src="assets/lockup.png" width="560" alt="tandem rng .R"></p>

# tandem-r

R package `tandemrng` for [Tandem8x32](https://github.com/tandem-rng/spec), a noncryptographic
pseudorandom number generator built to be fast on CPUs and GPUs alike. It wraps a vendored
copy of the reference C implementation and produces the stream the specification defines,
bit for bit.

## Use

```r
library(tandemrng)

rng <- tandem(42)                    # seed whitening: the spec's stream for seed 42
u <- tandem_runif(rng, 1e6)          # the specification's Float64 draws
f <- tandem_rsingle(rng, 10)         # Float32 draws, as doubles
w <- tandem_rbits(rng, 10, 32)       # unsigned 32-bit words, as doubles
b <- tandem_rbool(rng, 10)           # single stream bits
worker <- tandem_split(rng, 7)       # by index, from the key alone
kids <- tandem_fork(rng, 4)          # from the current block, parent moves on
tandem_key(rng); tandem_position(rng); tandem_chunk_length(rng)

RNGkind("user-supplied")             # Tandem as the base R generator
set.seed(42)
runif(3)                             # equals tandem_runif(tandem(42), 3)
```

Seeds, positions, indices and purposes are doubles below 2^53, or strings of decimal digits
for the full 128-bit or 64-bit range. A key is four numbers below 2^32 or 32 hex digits with
word 0 first. `tandem()` with no seed takes 128 bits from `/dev/urandom`.

`tandem_rbits()` supports 8, 16 and 32 bits, returned as doubles because 32-bit words do not
fit R integers. 64-bit words have no exact R type and are not provided.

## Install

```r
# install.packages("remotes")
remotes::install_github("tandem-rng/tandem-r")
```

The build needs a C99 compiler. For development, `pixi install` creates an environment with
R and the tooling, `pixi run document` regenerates `man/` and `NAMESPACE`, `pixi run test`
runs the tests and `pixi run check` runs `R CMD check --as-cran`.

## Tests

`tests/testthat/test-tandem.R` checks every vector of the specification
(`tests/testthat/vectors.json`, a copy of the spec repository's file), compares fills with
reference stream dumps in `tests/testthat/data`, and checks the base R hook. CI fails when
the vendored C sources in `src/` or the vectors drift from upstream. `tools/sync_c.sh` refreshes the C sources.

## Speed

Apple M4, one thread, `pixi run bench`, 2^24 doubles, minimum of seven runs, load 9:

| | GiB/s |
|---|---|
| `tandem_runif(rng, n)` | 6.25 |
| `runif(n)`, Mersenne-Twister | 1.74 |

The first row is the C fill plus R's allocation of the result.

## License

Apache License 2.0. See `LICENSE` and `NOTICE`.
