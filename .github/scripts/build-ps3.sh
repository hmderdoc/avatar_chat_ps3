#!/usr/bin/env bash
# Builds avatar_chat_ps3.self and .pkg inside the ps3dev toolchain image.
# Used by CI and the release workflow; works locally too with Docker:
#   .github/scripts/build-ps3.sh
set -euo pipefail

# The toolchain image: ps3dev publishes to GitHub's registry; Docker Hub
# holds an older build. First one that pulls wins. PS3DEV_IMAGE overrides.
candidates=(${PS3DEV_IMAGE:-} ghcr.io/ps3dev/ps3dev:latest ps3dev/ps3dev:latest ps3dev/ps3dev:submodules)
img=""
for c in "${candidates[@]}"; do
  [ -n "$c" ] || continue
  if docker pull "$c"; then img="$c"; break; fi
done
[ -n "$img" ] || { echo "no ps3dev toolchain image could be pulled" >&2; exit 1; }
echo "toolchain image: $img"

docker run --rm -v "$PWD:/src" -w /src "$img" bash -ec '
  export PS3DEV="${PS3DEV:-/usr/local/ps3dev}"
  export PSL1GHT="${PSL1GHT:-$PS3DEV}"
  if [ ! -f "$PSL1GHT/ppu_rules" ]; then
    found=$(find / -name ppu_rules -path "*psl1ght*" 2>/dev/null | head -1 || true)
    [ -n "$found" ] && export PSL1GHT=$(dirname "$found")
  fi
  export PATH="$PATH:$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin"
  echo "PS3DEV=$PS3DEV PSL1GHT=$PSL1GHT"
  ls "$PSL1GHT/ppu_rules"
  ppu-gcc --version | head -1
  make clean
  make pkg
  ls -la avatar_chat_ps3.self avatar_chat_ps3.pkg
'
# Files written by root inside the container; make them ours.
if [ "$(id -u)" != "0" ]; then
  docker run --rm -v "$PWD:/src" -w /src "$img" chown -R "$(id -u):$(id -g)" /src >/dev/null 2>&1 || sudo chown -R "$(id -u):$(id -g)" . || true
fi
