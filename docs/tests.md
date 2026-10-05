# Tests

```sh
pixi run test     # tests/testthat/test-tandem.R
```

## Suite

`tests/testthat/test-tandem.R` checks:

- Every specification vector (`vectors.json`) and the stream dumps in `tests/testthat/data`.
- Bounded integers, weighted choice, normals, and exponentials against tandem-c fixtures, built by
  `tools/gen_cross_fixtures.R`, and against its recorded hashes.
- The base R hook, serialization, `callr`, and forked `parallel::mclapply()` workers.
- Fills cut at any element equal the whole fill.

`tests/testthat/test-tandem.R` checks every vector of the specification
(`tests/testthat/vectors.json`, a copy of the spec repository's file), compares fills with
reference stream dumps in `tests/testthat/data`, and checks the base R hook, including that
saving `.Random.seed`, drawing, restoring and redrawing repeats, across the buffer edge. It also checks
that generators survive `saveRDS()`/`readRDS()`, `serialize()`, a `callr` child process and
forked `parallel::mclapply()` workers at their current position, and compares 64-bit words
with `k1234_K32_u64.bin` in both result types. Random access is checked against the
matching fill at several positions and chunk lengths. Bounded integers and normals are
compared with tandem-c's fixtures, converted to
`tests/testthat/data/cross_bounded.json` by `tools/gen_cross_fixtures.R`: integer ranges that reject
about a quarter of the draws, the stream position after them, and 64 normals from each of six
starts, with wedge and tail draws among them. A bounded or normal fill cut at
an arbitrary element equals the whole fill at an unaligned start with rejections, the word width
changes at `max = 2^32 + 1`, and a hash of 5 x 10^6 normals matches tandem-c's
recorded value, which pins the bits on every compiler CI builds with. An empty normal fill aligns
the position to 64, and 10^7 normals have the first four moments and the Kolmogorov-Smirnov
statistic of N(0, 1). Exponentials are compared
bit for bit with tandem-c's fixture, also from `core.hpp`, at five start positions, unaligned ones
included, and with tandem-c's recorded hash of 10^6 doubles and 10^6 floats from each of those
starts. A cut fill equals the whole fill, an empty fill leaves the position alone, and 10^7
exponentials have the first four moments and the Kolmogorov-Smirnov statistic of Exp(1).
