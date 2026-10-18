#!/bin/sh
# Build a tiny root filesystem from host binaries plus the shared libs they need.
# usage: ./make-rootfs.sh DIR [bin...]   (default bins: sh ls cat hostname ps echo)
set -e
dir=${1:?usage: make-rootfs.sh DIR [bin...]}
shift || true
bins=${*:-sh ls cat hostname ps echo}
mkdir -p "$dir/bin" "$dir/proc"
for b in $bins; do
  src=$(command -v "$b") || { echo "skip $b (not found)"; continue; }
  case $src in /*) ;; *) echo "skip $b (shell builtin)"; continue ;; esac
  cp -L "$src" "$dir/bin/$b"
  for lib in $(ldd "$src" 2>/dev/null | grep -o '/[^ ]*'); do
    mkdir -p "$dir$(dirname "$lib")"
    cp -L "$lib" "$dir$lib"
  done
done
echo "rootfs ready in $dir"
