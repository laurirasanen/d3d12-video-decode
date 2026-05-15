#!/bin/bash

set -euo pipefail

rm -rf build/

CC=x86_64-w64-mingw32-gcc \
  CXX=x86_64-w64-mingw32-g++ \
  meson setup \
  --cross-file x86_64-w64-mingw32.txt \
  -Dbuildtype=debug \
  -Doptimization=0 \
  build

meson compile -C build

