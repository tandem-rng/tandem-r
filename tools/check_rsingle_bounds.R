# Run from the package root: Rscript tools/check_rsingle_bounds.R
# A private build instruments the wrapper without adding an installed test entry point.
main <- function() {
  root <- normalizePath(".")
  build <- tempfile("rsingle-bounds-")
  dir.create(build)
  on.exit(unlink(build, recursive = TRUE), add = TRUE)
  dir.create(file.path(build, "src"))
  file.copy(list.files(file.path(root, "src"), pattern = "\\.[ch]$", full.names = TRUE),
            file.path(build, "src"))
  dir.create(file.path(build, "tools"))
  file.copy(file.path(root, "tools", "check_rsingle_bounds.c"), file.path(build, "tools"))
  previous <- setwd(build)
  on.exit(setwd(previous), add = TRUE, after = FALSE)
  status <- system2(file.path(R.home("bin"), "R"), c("CMD", "SHLIB", "tools/check_rsingle_bounds.c",
                                                    "src/tandem.c"))
  stopifnot(status == 0)
  dll <- dyn.load(file.path("tools", paste0("check_rsingle_bounds", .Platform$dynlib.ext)))
  on.exit({
    # Run generator finalizers while their native library is still loaded.
    gc()
    dyn.unload(dll[["path"]])
  }, add = TRUE, after = FALSE)
  check <- getNativeSymbolInfo("check_rsingle_bounds", PACKAGE = dll)
  for (n in c(0, 1, 2, 3, 4095, 4096)) {
    intact <- .Call(check, as.double(n))
    cat(sprintf("n=%d allocation guard intact=%s\n", n, intact))
    stopifnot(intact)
  }
}
main()
