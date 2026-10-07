# Tests

```sh
pixi run test     # tests/testthat/test-*.R
```

## Suite

`tests/testthat/test-tandem.R` checks:

- Every specification vector (`vectors.json`) and the stream dumps in `tests/testthat/data`.
- The base R hook, serialization, `callr`, and forked `parallel::mclapply()` workers.
- The moments and distributions of normals, exponentials and weighted choice.

`tests/testthat/test-conformance.R` checks the spec's conformance files and the items of its
`conformance/CHECKLIST.md` at b31af72 that the package's draws reach.

`tests/testthat/test-tandem.R` checks every vector of the specification
(`tests/testthat/vectors.json`, a copy of the spec repository's file), compares fills with
reference stream dumps in `tests/testthat/data`, and checks the base R hook, including that
saving `.Random.seed`, drawing, restoring and redrawing repeats, across the buffer edge. It also checks
that generators survive `saveRDS()`/`readRDS()`, `serialize()`, a `callr` child process and
forked `parallel::mclapply()` workers at their current position, and compares 64-bit words
with `k1234_K32_u64.bin` in both result types. Random access is checked against the
matching fill at several positions and chunk lengths. 10^7 normals and exponentials have the
first four moments and the Kolmogorov-Smirnov statistic of N(0, 1) and Exp(1), and 10^7 weighted
choices pass a chi-square test against their weights.

`tests/testthat/conformance` holds copies of the spec's `conformance/*.json` at commit `2a4bd08`,
and CI checks them byte for byte. `tests/testthat/test-conformance.R` compares the bounded fills,
Float64 normals, Float64 exponentials and weighted choice with every case they reach, values and
end positions. It fills each case whole, in two pieces cut at elements 1, 7, 20, 21 and `n - 1`,
and one element at a time. It checks the choice tables, the fallback index of rejected draws and
missed normals, the word width that follows the range, and the empty fills of every derived draw.
It checks the SHA-256 of the stream dumps, of 5 x 10^6 normals from the dump recipe, and the
FNV-1a hash of the exponential dump, which the C library computes because R has no Float32
exponentials. Start positions stop below 2^63.

The package has no scalar bounded draw, no Float32 normals or exponentials and no complex draws,
so `below.json`, the Float32 cases and the checklist items on Box-Muller pairs, odd `n` and
complex draws do not apply.
