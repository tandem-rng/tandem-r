# Converts tandem-c's cross-check headers, which tools/gen_cross.cpp generates from the CUDA
# port's core.hpp, to tests/testthat/data/cross_bounded.json.
# Usage: Rscript tools/gen_cross_fixtures.R path/to/tandem-c/tests
args <- commandArgs(TRUE)
dir <- if (length(args)) args[1] else "../tandem-c/tests"
read <- function(f) paste(readLines(file.path(dir, f)), collapse = "\n")

cases <- function(text, name) {
  body <- sub("\\};.*$", "", sub(paste0(".*", name, "\\[\\] = \\{"), "", text))
  m <- regmatches(body, gregexpr("\\{[0-9]+ull,\\s*[0-9]+u+l*,\\s*\\{[^}]*\\},\\s*[0-9]+u\\}", body))[[1]]
  lapply(m, function(s) {
    nums <- regmatches(s, gregexpr("[0-9]+", s))[[1]]
    list(start = nums[1], n = nums[2], want = nums[3:(length(nums) - 1)],
         end_pos = nums[length(nums)])
  })
}

normals <- function(text, name) {
  body <- sub("\\n\\};.*$", "", sub(paste0(".*", name, "\\[(2 \\* )?CROSS_NORMAL_COUNT\\] = \\{"), "", text))
  as.numeric(sub("f$", "", strsplit(gsub("\\s+", "", body), ",")[[1]]))
}

# Only the double table. R has no single precision fill, and the C test pins the floats. The values
# stay text, since R's own parser can miss the correctly rounded double by one ulp.
exponentials <- function(text) {
  body <- sub("\\n\\};.*$", "", sub(".*CROSS_EXPONENTIAL\\[\\] = \\{", "", text))
  m <- regmatches(body, gregexpr("\\{[0-9]+ull,[^u]*u\\}", body))[[1]]
  lapply(m, function(s) {
    nums <- regmatches(s, gregexpr("[0-9]+(\\.[0-9]+)?(e[-+]?[0-9]+)?", s))[[1]]
    list(start = nums[1], want = nums[2:(length(nums) - 1)], end_pos = nums[length(nums)])
  })
}

fill <- read("cross_fill_below.h")
normal <- read("cross_normal.h")
out <- list(
  exponential = exponentials(read("cross_exponential.h")),
  fill_u32 = cases(fill, "CROSS_FILL_U32"),
  fill_u64 = cases(fill, "CROSS_FILL_U64"),
  normal = normals(normal, "CROSS_NORMAL"),
  normal_end_pos = regmatches(normal, regexpr("[0-9]+(?=u;\\s*\\nstatic const float)", normal,
                                              perl = TRUE))
)
jsonlite::write_json(out, "tests/testthat/data/cross_bounded.json", auto_unbox = TRUE,
                     digits = NA)
