#!/bin/bash
# Build a test archive with the independent Python writer, then read it with C.
set -eu
cd "$(dirname "$0")/.."
./tools/make_test_pmtiles.py /tmp/astir_test.pmtiles
gcc -DHAVE_CONFIG_H -Isrc -I. -O1 -g -Wall -Wextra \
    tools/pmtiles_test.c src/core/map/pmtiles.c -lz -o /tmp/astir_pmtiles_test
exec /tmp/astir_pmtiles_test /tmp/astir_test.pmtiles
