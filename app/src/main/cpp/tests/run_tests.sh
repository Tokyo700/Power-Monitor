#!/bin/bash
# Builds and runs the C core tests on this computer (no phone or NDK needed).
set -e
cd "$(dirname "$0")"
gcc -Wall -Wextra -std=gnu11 -o test_powermon test_powermon.c ../powermon.c
./test_powermon
