#!/bin/bash
# Tests du simulateur (tools/pc-sim/ci.sh) : image ubuntu:24.04. A faire passer avant chaque publication de la console.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update -q >/dev/null
apt-get install -y -q --no-install-recommends gcc g++ libc6-dev clang lld llvm ffmpeg python3 python3-pil curl git make ca-certificates >/dev/null
bash tools/pc-sim/ci.sh
