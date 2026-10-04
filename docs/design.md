# Design

## Bounded integers

Element `i` maps stream word `i` by Lemire's multiply-and-reject method, and a rejected word
retries on a fallback generator derived by the global draw index, the aligned start position over
the word width plus `i`, so a fill uses exactly `n` words and a fill cut at any element equals
the whole fill. It reads 32-bit words, or 64-bit words for `max` above `2^32`.

## Normals

Normals use the 1024-layer ziggurat: element `i` comes from the 64-bit draw `i`, a draw outside the inner
rectangles continues on a fallback generator keyed by its global draw index, and a fill cut at
any element equals the whole fill.

## Exponentials

Exponentials follow [Appendix A](https://github.com/tandem-rng/spec/blob/main/SPEC.md) of the
specification: element `i` is `-log(1 - u) / rate` for the Float64 draw `u` number `i`, one draw
each, so a fill is random access and uses exactly `n` draws. At `rate = 1` the values are bit
identical across ports. Base R has no user-supplied hook for exponentials, so `rexp()` after
`RNGkind("user-supplied")` runs R's own algorithm on the Tandem uniforms and does not return them.
