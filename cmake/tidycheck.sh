#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1-or-later

set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$1/
build=$2
tidy=$3
cmake=$4
sysroot=$5
source_re=$6
header_re=$7
shift 7
db=$build/compile_commands.json
if [ ! -f "$db" ]; then
  echo "tidy-check needs $db; configure with cmake --preset dev" >&2
  exit 1
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"$cmake" "-DDB=$db" "-DROOT=$root" "-DSOURCE_RE=$source_re" "-DOUT=$work/sources" \
  -P "$here/tidysources.cmake"
count=$(wc -l < "$work/sources")
echo "production translation units: $count"
if [ "$count" -eq 0 ]; then
  echo "no production translation units matched the filter" >&2
  exit 1
fi
sed 's#/[^/]*$##' "$work/sources" | sort -u > "$work/compiled"
cd "$root"
find "$@" -name '*.h' | sort | while read -r header; do
  dir=${header%/*}
  set -- "$dir"/*.cpp
  if [ -e "$1" ] && ! grep -qxF "$dir" "$work/compiled"; then
    echo "skipping $header: $dir compiles no source in this build" >&2
    continue
  fi
  echo "$header"
done > "$work/headers"
echo "production headers: $(wc -l < "$work/headers")"
if [ -n "$sysroot" ]; then
  set -- --extra-arg=-isysroot "--extra-arg=$sysroot"
else
  set --
fi
jobs=$(getconf _NPROCESSORS_ONLN)
status=0
run_tidy() {
  xargs -P "$jobs" -I{} "$tidy" -p "$build" --experimental-custom-checks \
    "--header-filter=^$root($header_re)/" \
    "--exclude-header-filter=^${root}(build|tests)/" \
    "$@" {}
}
sed "s#^#$root#" "$work/sources" | run_tidy "$@" || status=1
sed "s#^#$root#" "$work/headers" | run_tidy \
  --extra-arg=-xc++ \
  --extra-arg=-Wno-pragma-once-outside-header \
  --extra-arg=-Wno-unused-const-variable \
  "$@" || status=1
exit $status
