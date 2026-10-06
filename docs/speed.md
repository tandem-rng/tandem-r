# Speed

`pixi run bench` produces the figures.

## CPU

Apple M4, one thread, `pixi run bench`, 2^22 draws, minimum of five runs, GiB/s of output, the
median of three passes in one session. Doubles count 8 bytes, integers 4. The Tandem column calls
the package's fills. The other two call base R with Tandem as the user-supplied generator and with
Mersenne-Twister. `rnorm` uses Ahrens-Dieter, the fastest `normal.kind` on Mersenne-Twister:
inversion, the default, writes 0.48 GiB/s.

| | Tandem | user-supplied Tandem | Mersenne-Twister |
|---|---|---|---|
| uniform, `tandem_runif` and `runif` | 7.7 | 1.9 | 1.8 |
| normal, `tandem_rnorm` and `rnorm` | 4.8 | 0.85 | 0.74 |
| exponential, `tandem_rexp` and `rexp` | 4.2 | 0.45 | 0.35 |
| `tandem_sample_int(rng, n, 1000)` and `sample.int(1000, n, TRUE)` | 6.7 | 0.57 | 0.60 |
| the same, weighted by `1:1000` | 0.79 | 0.18 | 0.18 |

The Tandem fills are the C fill plus R's allocation of the result. The user-supplied hook returns
one double per call, so `runif` through it runs at R's call rate. The hook fills a buffer of
1024 doubles at a time and keeps its state in `.Random.seed`. A draw checks one 64-bit token
against the buffer's. The cold
paths, a rebuild and the move to the next buffer, are kept out of line. `pixi run bench`
installs the package first, so it measures the
current sources.
