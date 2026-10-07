# tandem-r

R package `tandemrng` for Tandem8x32. It wraps a vendored tandem-c and produces the stream of
the [specification](https://github.com/tandem-rng/spec/blob/main/SPEC.md) bit for bit, and it
can serve as the base R generator.

- [API](api.md): the functions, the base R hook, serialization and parallel use.
- [Design](design.md): the bounded integer, normal and exponential contracts.
- [Tests](tests.md): what the suite checks.
- [Speed](speed.md): fill and base R figures on the Apple M4.

## Install

```r
# install.packages("remotes")
remotes::install_github("tandem-rng/tandem-r")
```

The vendored C code is tandem-c at commit `c8d96a0`. The build needs a C99 compiler. For development, `pixi install` creates an environment with
R and the tooling, `pixi run document` regenerates `man/` and `NAMESPACE`, `pixi run test`
runs the tests and `pixi run check` runs `R CMD check --as-cran`.

`tools/sync_c.sh` refreshes the vendored C sources.

## AI assistance

This port was written with the help of large language models under human
direction. The design and the specification are human work, as is much of the
Julia implementation. The code is tested bit for bit against every vector of
the specification and against long stream dumps from the Julia implementation,
and every value must match. The output does not depend on who or what wrote the
code.
