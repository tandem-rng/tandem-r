<p align="center"><img src="assets/lockup.png" width="560" alt="tandem rng .R"></p>

# tandem-r

R package `tandemrng` for [Tandem8x32](https://github.com/tandem-rng/spec), a noncryptographic
pseudorandom number generator. It wraps a vendored copy of the reference C implementation and
produces the stream the specification defines, bit for bit, fast on CPUs.

## Install

```r
# install.packages("remotes")
remotes::install_github("tandem-rng/tandem-r")
```

Needs a C99 compiler. The vendored C code is tandem-c at commit `b049384`.
For development, `pixi install`, then `pixi run test`.

## Use

```r
library(tandemrng)

rng <- tandem(42)                    # the spec's stream for seed 42
u <- tandem_runif(rng, 1e6)          # Float64 draws
worker <- tandem_split(rng, 7)       # by index, from the key alone
kids <- tandem_fork(rng, 4)          # from the current block, parent moves on
z <- tandem_rnorm(rng, 10)           # standard normals by Box-Muller
j <- tandem_below(rng, 10, 6)        # bounded integers on 0..5
```

```r
RNGkind("user-supplied")             # Tandem as the base R generator
set.seed(42)
runif(3)                             # equals tandem_runif(tandem(42), 3)
```

## What it provides

- `tandem(seed)`, `tandem_from_key()`: a generator. Seeds, positions, indices, and purposes are
  doubles below 2^53, or decimal strings for the full range. `tandem()` with no seed reads
  `/dev/urandom`.
- `tandem_runif`, `tandem_rsingle`, `tandem_rbool`: Float64, Float32, and bit draws.
- `tandem_rbits(rng, n, bits)`: 8, 16, 32, or 64-bit words. 64-bit words are `bit64::integer64`
  if `bit64` is installed, else 16-digit hex strings.
- `tandem_below`, `tandem_sample_int`: bounded integers on `0..max-1` and `1..max`.
- `tandem_rnorm`, `tandem_rexp`: Box-Muller normals and `-log(1 - u) / rate` exponentials.
- `tandem_split`, `tandem_fork`, `tandem_sub`: child streams.
- `tandem_at`: elements of the next fill, without drawing, for `"u32"`, `"u64"`, `"f32"`, `"f64"`.
- `tandem_key`, `tandem_position`, `tandem_set_position`, `tandem_chunk_length`, `tandem_state`,
  `tandem_restore`: transport form.
- `RNGkind("user-supplied")`: Tandem as the base R generator. `.Random.seed` holds its state, so
  `set.seed()` and restoring the seed repeat the draws. `rexp()` still uses R's algorithm.
- Serialization: a generator survives `saveRDS()`, `serialize()`, and parallel workers at its
  position. Copies are independent, so give each worker its own `tandem_split()`.
- Parallel use: element `i` of a fill is draw `i`, so ranks or workers that start at their first
  element, or draw from `split(task)`, reproduce a serial run. See
  [Appendix B](https://github.com/tandem-rng/spec/blob/main/SPEC.md#appendix-b-parallel-decomposition-non-normative).

## Tests

`pixi run test` runs `tests/testthat/test-tandem.R`. It checks:

- Every specification vector (`vectors.json`) and the stream dumps in `tests/testthat/data`.
- Bounded integers, normals, and exponentials against tandem-c fixtures, built by
  `tools/gen_cross_fixtures.R`, and against its recorded hashes.
- The base R hook, serialization, `callr`, and forked `parallel::mclapply()` workers.
- Fills cut at any element equal the whole fill.

## Speed

Apple M4, one thread, `pixi run bench`, 2^22 doubles, minimum of five runs. Normal and
exponential rows count 8 bytes per value.

| | GiB/s |
|---|---|
| `tandem_runif(rng, n)` | 15.6 |
| `runif(n)` with Tandem as the user-supplied generator | 2.2 |
| `runif(n)`, Mersenne-Twister | 2.4 |
| `tandem_rnorm(rng, n)` | 5.2 |
| `rnorm(n)`, Mersenne-Twister with inversion | 0.5 |
| `tandem_rexp(rng, n)` | 6.2 |
| `rexp(n)`, Mersenne-Twister | 0.4 |

Longer notes on use, install, tests, and speed are in [docs/notes.md](docs/notes.md).

## AI assistance

This port was written with the help of large language models under human
direction. The design and the specification are human work, as is much of the
Julia implementation. The code is tested bit for bit against every vector of
the specification and against long stream dumps from the Julia implementation,
and every value must match. The output does not depend on who or what wrote the
code.

## License

Apache License 2.0. See `LICENSE` and `NOTICE`.
