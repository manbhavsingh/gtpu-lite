#!/bin/sh
# Run the TSan build with address-space randomization off (needed on some kernels)
exec setarch "$(uname -m)" -R "$(dirname "$0")/../build/gtpu_tsan" "$@"