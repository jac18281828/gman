#!/usr/bin/env bash
# Whole-machine path-tracing benchmark.
#
# Renders samples/materials.rib through gmanpathtracer as a pool of
# independent jobs, one per slot, and reports throughput in renders per
# minute. The path tracer is single-threaded, so the pool is what loads the
# machine; a fixed job queue keeps fast and slow cores both busy.
#
#   bench/bench.sh [-g gman] [-j slots] [-n jobs] [-s samples] [-r runs]

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
gman="$root/build-release/gman"
slots=$(getconf _NPROCESSORS_ONLN)
jobs=
samples=16
runs=3

while getopts "g:j:n:s:r:h" opt; do
  case $opt in
    g) gman=$OPTARG ;;
    j) slots=$OPTARG ;;
    n) jobs=$OPTARG ;;
    s) samples=$OPTARG ;;
    r) runs=$OPTARG ;;
    *) sed -n '2,10p' "$0"; exit 2 ;;
  esac
done
jobs=${jobs:-$((slots * 3))}

[ -x "$gman" ] || { echo "no gman at $gman: cmake --preset release && cmake --build --preset release" >&2; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
sed "s/\\[64\\]/[$samples]/" "$root/samples/materials.rib" > "$work/scene.rib"
grep -q "\\[$samples\\]" "$work/scene.rib" || { echo "scene sample count not set" >&2; exit 1; }

now() { perl -MTime::HiRes=time -e 'printf "%.3f\n", time'; }

# Decompressed IDAT bytes: the PNG's tIME chunk differs run to run.
pixelsum() {
  python3 -I -c '
import hashlib, struct, sys, zlib
d = open(sys.argv[1], "rb").read(); i = 8; z = b""
while i < len(d):
    n, t = struct.unpack(">I4s", d[i:i + 8])
    if t == b"IDAT":
        z += d[i + 8:i + 8 + n]
    i += 12 + n
print(hashlib.sha256(zlib.decompress(z)).hexdigest()[:16])' "$1"
}

export gman work
job() {
  dir="$work/job.$1"
  mkdir -p "$dir"
  (cd "$dir" && "$gman" -q -r gmanpathtracer ../scene.rib >/dev/null 2>&1)
}
export -f job

echo "cpu:     $(sysctl -n machdep.cpu.brand_string 2>/dev/null || grep -m1 'model name' /proc/cpuinfo | cut -d: -f2)"
echo "gman:    $("$gman" --version 2>&1 | head -1)"
echo "scene:   materials.rib, ${samples} samples/subpixel"
echo "pool:    $jobs jobs over $slots slots, $runs runs"

rates=()
for r in $(seq "$runs"); do
  rm -rf "$work"/job.*
  start=$(now)
  seq "$jobs" | xargs -P "$slots" -I{} bash -c 'job {}'
  wall=$(echo "$(now) - $start" | bc -l)
  rate=$(echo "$jobs * 60 / $wall" | bc -l)
  printf "run %d:   %7.1f s  %7.2f renders/min\n" "$r" "$wall" "$rate"
  rates+=("$rate")
done

sums=$(for f in "$work"/job.*/materials.png; do pixelsum "$f"; done | sort -u)
median=$(printf '%s\n' "${rates[@]}" | sort -g | awk '{a[NR]=$1} END {print a[int((NR+1)/2)]}')

printf "score:   %.2f renders/min (median)\n" "$median"
if [ "$(echo "$sums" | wc -l)" -eq 1 ]; then
  echo "pixels:  $sums (all $jobs renders identical)"
else
  echo "pixels:  MISMATCH across jobs:" >&2
  echo "$sums" >&2
  exit 1
fi
