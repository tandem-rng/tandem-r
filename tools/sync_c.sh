#!/bin/sh
# Copy the reference C sources of one tandem-c commit from a checkout.
# Usage: tools/sync_c.sh path/to/tandem-c commit
# Then set TANDEM_C in .github/workflows/ci.yml and the README pin to that commit.
set -e
src=${1:-../tandem-c}
rev=${2:?commit}
for f in tandem.c tandem.h tandem_normal_tables.h; do
    git -C "$src" show "$rev:$f" > "$(dirname "$0")/../src/$f"
done
