#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Installs the gman release tree that sits beside this script into a prefix,
# or removes what an earlier run installed.
#
# Every file an install writes, and every directory it creates, is recorded
# in <prefix>/share/gman/install_manifest.txt, one absolute path a line and a
# trailing slash on a directory. --uninstall removes exactly those entries
# and each recorded directory left empty. Installing over an existing
# manifest uninstalls it first.

set -u
umask 022
LC_ALL=C
export LC_ALL

usage() {
  cat <<'EOF'
usage: install.sh [--prefix DIR] [--uninstall]
       install.sh -h | --help

Installs bin/, lib/ and include/ from beside this script into DIR
(default /usr/local), or removes what it installed there.

  --prefix DIR  install into, or uninstall from, DIR
  --uninstall   remove the files an earlier install recorded in
                DIR/share/gman/install_manifest.txt
  -h, --help    print this text
EOF
}

die() {
  printf 'install.sh: %s\n' "$*" >&2
  exit 1
}

bad_usage() {
  printf 'install.sh: %s\n' "$*" >&2
  usage >&2
  exit 2
}

prefix=/usr/local
uninstall=0
while [ $# -gt 0 ]; do
  case $1 in
    -h | --help)
      usage
      exit 0
      ;;
    --prefix)
      if [ $# -lt 2 ] || [ -z "$2" ]; then
        bad_usage '--prefix needs a directory'
      fi
      prefix=$2
      shift 2
      ;;
    --uninstall)
      uninstall=1
      shift
      ;;
    *)
      bad_usage "unknown option: $1"
      ;;
  esac
done

src=$(cd "$(dirname -- "$0")" && pwd -P) || die "cannot locate $0"
if [ "$uninstall" -eq 0 ] && [ ! -f "$src/bin/gman" ]; then
  bad_usage "no bin/gman beside $0; run it from the unpacked release folder"
fi

case $prefix in
  /*) ;;
  *) prefix=$PWD/$prefix ;;
esac
while [ "$prefix" != / ] && [ "${prefix%/}" != "$prefix" ]; do
  prefix=${prefix%/}
done

manifest=$prefix/share/gman/install_manifest.txt

# The nearest existing ancestor of the prefix must accept writes.
require_writable() {
  probe=$prefix
  while [ ! -d "$probe" ]; do
    probe=$(dirname -- "$probe")
  done
  [ -w "$probe" ] || die "cannot write to $prefix; rerun with sudo"
}

# Deletes the manifest's files, the manifest, then its directories deepest
# first. A file outside the prefix is skipped. rmdir leaves a directory
# another program has since filled.
uninstall_recorded() {
  dirs=$(awk '/\/$/ { dir[++n] = $0 } END { for (i = n; i >= 1; i--) print dir[i] }' \
    "$manifest") || return 1
  while IFS= read -r entry; do
    case $entry in
      */ | */.. | */../*) ;;
      "$prefix"/*) rm -f -- "$entry" ;;
      *) printf 'install.sh: skipped %s: outside %s\n' "$entry" "$prefix" >&2 ;;
    esac
  done <"$manifest"
  rm -f -- "$manifest"
  printf '%s\n' "$dirs" | while IFS= read -r entry; do
    case $entry in
      */.. | */../*) ;;
      /*/) rmdir -- "${entry%/}" 2>/dev/null ;;
    esac
  done
  return 0
}

made=
record_dir() {
  if [ -f "$manifest" ]; then
    printf '%s/\n' "$1" >>"$manifest"
  else
    made="$made$1/
"
  fi
}

make_dir() {
  [ -d "$1" ] && return 0
  make_dir "$(dirname -- "$1")" || return 1
  mkdir -- "$1" || return 1
  record_dir "$1"
}

# Clears the download quarantine Gatekeeper enforces on a file `cp` copied
# with it. A file without the attribute is left as it is.
clear_quarantine() {
  [ "$(uname -s)" = Darwin ] || return 0
  command -v xattr >/dev/null 2>&1 || return 0
  grep -v '/$' "$manifest" | tr '\n' '\0' |
    xargs -0 xattr -d com.apple.quarantine >/dev/null 2>&1
  return 0
}

if [ "$uninstall" -eq 1 ]; then
  [ -f "$manifest" ] || die "no manifest at $manifest; nothing to uninstall"
  require_writable
  uninstall_recorded
  printf 'Removed gman from %s\n' "$prefix"
  exit 0
fi

require_writable
if [ -f "$manifest" ]; then
  uninstall_recorded
fi

tops=
for top in bin lib include; do
  [ -d "$src/$top" ] && tops="$tops $top"
done

make_dir "$prefix/share/gman" || die "cannot create $prefix/share/gman"
printf '%s' "$made" >"$manifest" || die "cannot write $manifest"

(cd "$src" && find $tops -type d | sort) | while IFS= read -r rel; do
  make_dir "$prefix/$rel" || exit 1
done || die "cannot create directories under $prefix; --uninstall removes the partial install"

(cd "$src" && find $tops \( -type f -o -type l \) | sort) |
  while IFS= read -r rel; do
    printf '%s\n' "$prefix/$rel" >>"$manifest"
    rm -f -- "$prefix/$rel"
    cp -P "$src/$rel" "$prefix/$rel" || exit 1
  done || die "cannot copy files into $prefix; --uninstall removes the partial install"

clear_quarantine

count=$(grep -vc '/$' "$manifest")
if ! version=$("$prefix/bin/gman" --version 2>&1); then
  {
    printf 'install.sh: the installed %s/bin/gman does not run:\n%s\n' \
      "$prefix" "$version"
    printf 'gman needs the libtiff, libpng, libjpeg and zlib runtime libraries.\n'
    printf 'The files stay installed; install.sh --prefix %s --uninstall removes them.\n' \
      "$prefix"
  } >&2
  exit 1
fi

printf 'Installed %s files into %s (%s)\n' "$count" "$prefix" \
  "$(printf '%s\n' "$version" | head -n 1)"
gman=$prefix/bin/gman
case ":$PATH:" in
  *":$prefix/bin:"*) gman=gman ;;
  *) printf '%s/bin is not on your PATH.\n' "$prefix" ;;
esac
if [ -f "$src/samples/vase.rib" ]; then
  printf 'Render the sample:\n  cd %s && %s -r gmanraytracer samples/vase.rib\n' \
    "$src" "$gman"
fi
