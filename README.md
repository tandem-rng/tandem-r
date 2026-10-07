<p align="center"><img src="assets/lockup.png" width="560" alt="tandem rng .R"></p>

# tandem-r

[![CI](https://github.com/tandem-rng/tandem-r/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/tandem-rng/tandem-r/actions/workflows/ci.yml)
[![Docs](https://img.shields.io/badge/docs-tandem--rng.github.io-7fb3ee.svg)](https://tandem-rng.github.io/tandem-r/)
[![License: Apache 2.0](https://img.shields.io/badge/license-Apache_2.0-blue.svg)](LICENSE)

R package `tandemrng` for [Tandem8x32](https://github.com/tandem-rng/spec), a noncryptographic
pseudorandom number generator. It wraps a vendored tandem-c and produces the specified stream
bit for bit, and it can serve as the base R generator.

Needs a C99 compiler. The vendored C code is tandem-c at commit `ef67bd7`.

```r
# install.packages("remotes")
remotes::install_github("tandem-rng/tandem-r")
```

```r
library(tandemrng)

rng <- tandem(42)                    # the spec's stream for seed 42
u <- tandem_runif(rng, 1e6)          # Float64 draws
worker <- tandem_split(rng, 7)       # by index, from the key alone
z <- tandem_rnorm(worker, 10)        # standard normals by the ziggurat
k <- tandem_sample_int(rng, 10, 3, prob = c(1, 2, 7))  # weighted, like sample.int
RNGkind("user-supplied")             # Tandem as the base R generator
```

See [API](docs/api.md), [design](docs/design.md), [tests](docs/tests.md) and [speed](docs/speed.md).

Portions of the code were generated with the assistance of LLMs.

[Documentation](https://tandem-rng.github.io/tandem-r/) · [Apache 2.0 license](LICENSE)
