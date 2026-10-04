# Speed

Apple M4, one thread, `pixi run bench`, 2^22 doubles, minimum of five runs. Normal and
exponential rows count 8 bytes per value.

| | GiB/s |
|---|---|
| `tandem_runif(rng, n)` | 15.6 |
| `runif(n)` with Tandem as the user-supplied generator | 2.2 |
| `runif(n)`, Mersenne-Twister | 2.4 |
| `tandem_rnorm(rng, n)` | 10.4 |
| `rnorm(n)`, Mersenne-Twister with inversion | 0.5 |
| `tandem_rexp(rng, n)` | 6.2 |
| `rexp(n)`, Mersenne-Twister | 0.4 |

The Tandem fill rows are the C fill plus R's allocation of the result. The user-supplied hook returns
one double per call, so `runif` through it runs at R's call rate. The hook fills a buffer of
1024 doubles at a time and keeps its state in `.Random.seed`. A draw checks one 64-bit token
against the buffer's. The cold
paths, a rebuild and the move to the next buffer, are kept out of line. `pixi run bench`
installs the package first, so it measures the
current sources.
