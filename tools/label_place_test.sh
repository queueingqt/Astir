#!/bin/bash
# Build and run the label placement checks.  See tools/label_place_test.c.
set -eu
cd "$(dirname "$0")/.."
gcc -DHAVE_CONFIG_H -Isrc -I. -O1 -g -Wall \
    tools/label_place_test.c src/core/render/label_place.c \
    src/core/util/snprintf.c -o /tmp/astir_label_test
exec /tmp/astir_label_test
