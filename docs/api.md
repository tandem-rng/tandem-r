# API

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

## Examples

```r
library(tandemrng)

rng <- tandem(42)                    # seed whitening: the spec's stream for seed 42
u <- tandem_runif(rng, 1e6)          # the specification's Float64 draws
f <- tandem_rsingle(rng, 10)         # Float32 draws, as doubles
w <- tandem_rbits(rng, 10, 32)       # unsigned 32-bit words, as doubles
w64 <- tandem_rbits(rng, 10, 64)     # 64-bit words: integer64 with bit64, else hex strings
b <- tandem_rbool(rng, 10)           # single stream bits
x <- tandem_at(rng, "f64", c(0, 5, 1e6))  # elements of the next fill, without drawing
i <- tandem_sample_int(rng, 10, 6)   # uniform on 1..6, like sample.int(6, 10, TRUE)
j <- tandem_below(rng, 10, 6)        # the same fill on 0..5, as the other ports return it
z <- tandem_rnorm(rng, 10)           # standard normals by Box-Muller, as in core.hpp
e <- tandem_rexp(rng, 10, rate = 2)    # exponentials -log(1 - u) / rate, one draw each
worker <- tandem_split(rng, 7)       # by index, from the key alone
kids <- tandem_fork(rng, 4)          # from the current block, parent moves on
tandem_key(rng); tandem_position(rng); tandem_chunk_length(rng)
s <- tandem_state(rng)               # plain list: key hex, position string, K
rng2 <- tandem_restore(s)            # same stream from the same position
saveRDS(rng, "rng.rds")              # a generator survives saveRDS and parallel workers

RNGkind("user-supplied")             # Tandem as the base R generator
set.seed(42)
runif(3)                             # equals tandem_runif(tandem(42), 3)
saved <- .Random.seed                # the hook's state is in .Random.seed
a <- runif(3)
.Random.seed <- saved
identical(runif(3), a)               # TRUE: restoring the seed repeats the draws
```

Seeds, positions, indices and purposes are doubles below 2^53, or strings of decimal digits
for the full 128-bit or 64-bit range. A key is four numbers below 2^32 or 32 hex digits with
word 0 first. `tandem()` with no seed takes 128 bits from `/dev/urandom`.

With the hook active, `.Random.seed` holds ten integers after the kind code: the key, the
position of the first draw in the hook's buffer of 1024 draws, `K`, and the number of draws
used from that buffer, and a 64-bit token that hashes the key, position and `K`. R copies them
in and out around each call, so saving and restoring
`.Random.seed` (or `set.seed()`) restores the stream exactly. A draw compares only the token
with that of its buffer. A restored state brings its own token and so rebuilds the buffer. A
state whose token does not match its words, for example one built by hand, is rebuilt and given
the right token. Editing the key, position or `K` of a live state while keeping its token goes
unnoticed until the buffer is next refilled.

`tandem_rbits()` supports 8, 16 and 32 bits, returned as doubles because 32-bit words do not
fit R integers. 64-bit words have no exact R type. With the suggested package `bit64`
installed, `bits = 64` returns a `bit64::integer64`, which holds each word as a signed two's
complement value, so words from 2^63 on read as negative. Without `bit64` it returns a
character vector of 16 lowercase hex digits per word, most significant digit first.

`tandem_at(rng, type, i)` reads element `i` of the fill that would start at the current
position, without moving the generator, for `"u32"`, `"u64"`, `"f32"` and `"f64"`. Elements
count from 0, as in the specification. `i` may be a vector.

`tandem_sample_int(rng, n, max)`, `tandem_below(rng, n, max)`,
`tandem_rnorm(rng, n, mean = 0, sd = 1)` and `tandem_rexp(rng, n, rate = 1)` draw bounded
integers, normals and exponentials from a generator, not from the base R hook. They follow Appendix A of the specification, which is not
normative, through the C library's fills, so they equal tandem-c's values bit for bit. `tandem_below()` returns the C fill,
uniform on `0..(max - 1)`, and `tandem_sample_int()` adds 1, so it is uniform on `1..max` like
`sample.int(max, n, replace = TRUE)`.
Element `i` maps stream word `i` by Lemire's multiply-and-reject method, and a rejected word
retries on a fallback generator derived by the global draw index, the aligned start position over
the word width plus `i`, so a fill uses exactly `n` words and a fill cut at any element equals
the whole fill. It reads 32-bit words, or 64-bit words for `max` above `2^32`. Normals come in
Box-Muller pairs: elements `2j` and `2j + 1` are the cosine and sine halves from the Float64
draws `2j` and `2j + 1`, and an odd count still consumes both draws of its last pair.

Exponentials follow [Appendix A](https://github.com/tandem-rng/spec/blob/main/SPEC.md) of the
specification: element `i` is `-log(1 - u) / rate` for the Float64 draw `u` number `i`, one draw
each, so a fill is random access and uses exactly `n` draws. At `rate = 1` the values are bit
identical across ports. Base R has no user-supplied hook for exponentials, so `rexp()` after
`RNGkind("user-supplied")` runs R's own algorithm on the Tandem uniforms and does not return them.

Parallel use: element `i` of a fill is draw `i`, so ranks, threads or devices that start at the
position of their first element, or draw from `split(task)`, reproduce a serial run for any
decomposition, as
[Appendix B](https://github.com/tandem-rng/spec/blob/main/SPEC.md#appendix-b-parallel-decomposition-non-normative)
of the specification shows.

## Serialization

A generator is an external pointer, and R drops the address of a pointer on `saveRDS()`,
`serialize()` and transfer to a worker. The pointer keeps a 28-byte tag with the transport
form (key, position, `K`), which serialization preserves. Each call that moves the position
writes the new position into the tag, an 8-byte store. A deserialized generator finds its
address null on first use and rebuilds itself from the tag, at the position it had when it
was serialized.

This keeps the generator an object that updates in place, as every call in the API expects,
and adds no cost to a draw beyond that store. The alternative, a plain list that holds the
state and rebuilds a pointer on demand, would need copy-on-modify semantics for the position
and a way to hand the advanced position back to the caller, which breaks `tandem_runif(rng,
n)` advancing `rng`. Copies made by serialization are independent of each other and of the
original: two workers that receive the same generator draw the same values, so give each its
own with `tandem_split()`. `tandem_state()` and `tandem_restore()` convert to and from a plain
list for use outside R's own formats.
