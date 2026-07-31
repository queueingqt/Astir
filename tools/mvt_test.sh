#!/bin/bash
# Build and run the MVT decoder checks.  See tools/mvt_test.c.
set -eu
cd "$(dirname "$0")/.."
gcc -DHAVE_CONFIG_H -Isrc -I. -O2 -Wall -Wextra -g \
    tools/mvt_test.c src/core/map/mvt.c src/core/util/snprintf.c \
    -o /tmp/astir_mvt_test
exec /tmp/astir_mvt_test
