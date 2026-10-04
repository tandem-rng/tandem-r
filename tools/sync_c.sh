#!/bin/sh
# Copy the reference C sources from a tandem-c checkout. Usage: tools/sync_c.sh path/to/tandem-c
set -e
src=${1:-../tandem-c}
cp "$src/tandem.c" "$src/tandem.h" "$src/tandem_normal_tables.h" "$(dirname "$0")/../src/"
