#!/bin/sh
# Prints the path of a stamp file whose mtime advances exactly when any
# tracked file's name, mtime, or size changes -- image.mk depends on
# the stamp instead of listing tracked files as prerequisites directly.
#
# Why the indirection exists at all: Make splits prerequisite lists on
# whitespace, so a tracked filename containing a space ("My Project
# source.<pi>.rsrc.r" -- completely idiomatic for classic Mac files) read
# straight into a prerequisite list becomes two phantom targets and a
# hard "No rule to make target" error. Confirmed on a real project.
# One space-free stamp path sidesteps Make's limitation entirely while
# keeping the same semantics: rebuild the disk image iff something
# tracked actually changed.
#
# Usage: tools/mac-forks/tracked-stamp.sh <build-dir>
# (stdout is consumed by image.mk's $(shell ...), so it must print
# exactly the stamp path and nothing else)
set -eu

build_dir=${1:?"usage: $0 <build-dir>"}

root=$(git rev-parse --show-toplevel)
cd "$root"

mkdir -p "$build_dir"
stamp="$build_dir/.tracked.stamp"

# Name + mtime + size per tracked file; a deleted-but-still-tracked
# file just drops out of the stat output (rather than erroring the
# whole pipeline), which still changes the digest -- exactly right.
digest=$( { git -c core.quotePath=false ls-files -z |
            xargs -0 stat -f '%N %m %z' 2>/dev/null || true; } | md5 -q)

if [ ! -f "$stamp" ] || [ "$(cat "$stamp")" != "$digest" ]; then
    printf '%s\n' "$digest" >"$stamp"
fi

printf '%s\n' "$stamp"
