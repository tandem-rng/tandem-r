#' Tandem8x32 generators
#'
#' A generator is its transport form: a 128-bit key, a 64-bit stream bit position, and a
#' chunk length `K`. `tandem()` builds one from a seed through the specification's seed
#' whitening, so `tandem(42)` produces the stream of Julia `Tandem8x32(42)` and C
#' `tandem_seed(42, 0, 32)`. `tandem_from_key()` takes the transport form directly.
#'
#' Positions, seeds, indices and purposes are doubles when they fit below 2^53 and strings
#' of decimal digits otherwise. A key is four doubles below 2^32 or a string of 32 hex
#' digits with word 0 first.
#'
#' @param seed An integer-valued double in `[0, 2^53)`, a string of decimal digits for a seed
#'   up to `2^128 - 1`, or `NULL` for 128 bits of operating system entropy.
#' @param K The chunk length, a power of two in `[1, 65536]`.
#' @param key Four doubles or a string of 32 hex digits.
#' @param position The stream bit position of the next draw.
#' @param rng A `tandem_rng` object.
#' @return `tandem()` and `tandem_from_key()` return a `tandem_rng` object. The accessors
#'   return the key as a hex string, the position as a double (a string from `2^53`), and
#'   `K` as an integer.
#' @examples
#' rng <- tandem(42)
#' tandem_runif(rng, 3)
#' tandem_key(rng)
#' tandem_position(rng)
#' @name tandem
NULL

#' @rdname tandem
#' @export
tandem <- function(seed = NULL, K = 32) .Call(R_tandem_new, seed, as.double(K))

#' @rdname tandem
#' @export
tandem_from_key <- function(key, position = 0, K = 32) {
  .Call(R_tandem_from_key, key, position, as.double(K))
}

#' @rdname tandem
#' @export
tandem_key <- function(rng) .Call(R_tandem_key, rng)

#' @rdname tandem
#' @export
tandem_position <- function(rng) .Call(R_tandem_position, rng)

#' @rdname tandem
#' @export
tandem_set_position <- function(rng, position) invisible(.Call(R_tandem_set_position, rng, position))

#' @rdname tandem
#' @export
tandem_chunk_length <- function(rng) .Call(R_tandem_chunk_length, rng)

#' Generator state
#'
#' A generator is an external pointer, which a plain `saveRDS()`, `serialize()` or a transfer
#' to a parallel worker would lose. The pointer carries its transport form as a tag that
#' serialization keeps, so a generator that went through `saveRDS()`/`readRDS()` or reached a
#' worker rebuilds itself on first use and continues at the position it had when it was
#' serialized. Every copy made this way is independent of the others.
#'
#' `tandem_state()` returns the transport form as a plain list, for formats other than R's
#' own: `key` as 32 hex digits, `position` as a string of decimal digits, and `K`.
#' `tandem_restore()` builds a generator from such a list.
#'
#' @param rng A `tandem_rng` object.
#' @param state A list with `key`, `position` and `K`, as `tandem_state()` returns.
#' @return `tandem_state()` returns a list, `tandem_restore()` a `tandem_rng` object.
#' @examples
#' rng <- tandem(42)
#' tandem_runif(rng, 3)
#' copy <- tandem_restore(tandem_state(rng))
#' identical(tandem_runif(copy, 2), tandem_runif(rng, 2))
#' @name state
NULL

#' @rdname state
#' @export
tandem_state <- function(rng) {
  pos <- tandem_position(rng)
  if (!is.character(pos)) pos <- sprintf("%.0f", pos)
  list(key = tandem_key(rng), position = pos, K = tandem_chunk_length(rng))
}

#' @rdname state
#' @export
tandem_restore <- function(state) tandem_from_key(state$key, state$position, state$K)

#' @export
print.tandem_rng <-function(x, ...) {
  cat(sprintf("Tandem8x32-K%d key %s position %s\n", tandem_chunk_length(x), tandem_key(x),
              format(tandem_position(x), scientific = FALSE)))
  invisible(x)
}

#' Draws
#'
#' Every draw aligns the position to the width of the value, reads, and advances past it,
#' as the specification requires. A fill of `n` values equals `n` scalar draws.
#'
#' `tandem_runif()` returns the specification's Float64 mapping, `(raw >> 11) * 2^-53`, and
#' `tandem_rsingle()` the Float32 mapping `(raw >> 8) * 2^-24` as doubles. `tandem_rbits()`
#' returns unsigned words of 8, 16 or 32 bits as doubles, since 32-bit words do not fit R
#' integers. With `bits = 64` it returns `bit64::integer64` when the suggested package bit64
#' is installed, and otherwise a character vector of 16 lowercase hex digits per word. An
#' `integer64` holds the word as a signed two's complement value, so words from `2^63` on
#' read as negative. `tandem_rbool()` returns single stream bits.
#'
#' @param rng A `tandem_rng` object.
#' @param n The number of values.
#' @param bits The word width: 8, 16, 32 or 64.
#' @return A double vector, a logical vector for `tandem_rbool()`, and an `integer64` or
#'   character vector for 64-bit words.
#' @examples
#' rng <- tandem(42)
#' u <- tandem_runif(rng, 5)
#' w <- tandem_rbits(rng, 5, 32)
#' @name draws
NULL

#' @rdname draws
#' @export
tandem_runif <- function(rng, n) .Call(R_tandem_runif, rng, as.double(n))

#' @rdname draws
#' @export
tandem_rsingle <- function(rng, n) .Call(R_tandem_rsingle, rng, as.double(n))

#' @rdname draws
#' @export
tandem_rbits <- function(rng, n, bits = 32) {
  if (identical(as.integer(bits), 64L)) {
    return(.Call(R_tandem_rbits64, rng, as.double(n), !bit64_available()))
  }
  .Call(R_tandem_rbits, rng, as.double(n), as.integer(bits))
}

# A function so that tests can exercise both result types.
bit64_available <- function() requireNamespace("bit64", quietly = TRUE)

#' @rdname draws
#' @export
tandem_rbool <- function(rng, n) .Call(R_tandem_rbool, rng, as.double(n))

#' Random access
#'
#' `tandem_at(rng, type, i)` returns element `i` of the fill that would start at the
#' generator's current position, without moving the generator. Elements count from 0, as in
#' the specification, so `tandem_at(rng, "f64", i)` equals `tandem_runif(rng, n)[i + 1]` for
#' every `i < n`. The cost does not depend on `i`.
#'
#' @param rng A `tandem_rng` object.
#' @param type One of `"u32"`, `"u64"`, `"f32"` or `"f64"`: unsigned words of 32 or 64 bits and
#'   the Float32 and Float64 mappings of `tandem_rsingle()` and `tandem_runif()`.
#' @param i Element indices, a numeric vector of integer values in `[0, 2^53)` or one string of
#'   decimal digits for an index up to `2^64 - 1`.
#' @return A double vector, as the matching draw returns. `"u64"` gives `bit64::integer64` or
#'   hex strings, as `tandem_rbits(rng, n, 64)` does.
#' @examples
#' rng <- tandem(42)
#' tandem_at(rng, "f64", c(0, 5, 1e6))
#' identical(tandem_at(rng, "f64", 2), tandem_runif(rng, 3)[3])
#' @export
tandem_at <- function(rng, type, i) {
  types <- c("u32", "u64", "f32", "f64")
  type <- match(match.arg(type, types), types) - 1L
  .Call(R_tandem_at, rng, type, i, !bit64_available())
}

#' Derived generators
#'
#' Children start at position 0 with the parent's `K`. `tandem_split()` derives child
#' `index` from the key alone, so the same index gives the same child whatever the parent's
#' position. `tandem_sub()` derives a generator for a named purpose, also from the key alone.
#' `tandem_fork()` derives `n` children from the block the parent is in and moves the parent
#' past that block, so successive forks give fresh children.
#'
#' @param rng A `tandem_rng` object.
#' @param index,purpose An unsigned 64-bit value as a double or a decimal string.
#' @param n The number of children.
#' @return A `tandem_rng` object, or a list of them for `tandem_fork()`.
#' @examples
#' rng <- tandem(42)
#' worker <- tandem_split(rng, 7)
#' kids <- tandem_fork(rng, 4)
#' @name derived
NULL

#' @rdname derived
#' @export
tandem_split <- function(rng, index) .Call(R_tandem_split, rng, index)

#' @rdname derived
#' @export
tandem_sub <- function(rng, purpose) .Call(R_tandem_sub, rng, purpose)

#' @rdname derived
#' @export
tandem_fork <- function(rng, n) .Call(R_tandem_fork, rng, as.double(n))

#' Tandem as the base R generator
#'
#' After `RNGkind("user-supplied")`, `runif()` and every function built on it draw from a
#' Tandem8x32 generator held by the package. `set.seed(s)` then gives the generator
#' `tandem(s)`, so `runif(n)` returns the specification's Float64 draws of `tandem(s)` from
#' position 0. The state of the generator is in `.Random.seed`: the key, the position of the
#' first draw in the hook's buffer of 1024 draws, `K` and the number of draws used. Saving and
#' restoring `.Random.seed` therefore repeats the stream.
#'
#' @examples
#' old <- RNGkind("user-supplied")
#' set.seed(42)
#' identical(runif(3), tandem_runif(tandem(42), 3))
#' RNGkind(old[1])
#' @name user-supplied
NULL

#' @useDynLib tandemrng, .registration = TRUE
#' @keywords internal
"_PACKAGE"
