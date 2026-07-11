#!/bin/sh
# Regenerates .hqx / .r sidecar files from any resource-fork-bearing
# file in the working tree, and keeps a generated block of .gitignore
# in sync so the real (fork-bearing) files never get committed
# directly. Run automatically by the pre-commit hook; safe to run by
# hand too (e.g. `tools/mac-forks/export.sh`).
#
# Project-agnostic on purpose: makes no assumptions about filenames.
# A file is a candidate purely by its shape:
#   - has a non-empty resource fork, AND
#   - is not itself an already-generated .hqx/.r sidecar
#
# Among candidates:
#   - name ends in ".rsrc" (case-insensitive) AND has an empty data
#     fork -> DeRez'd to <name>.r. DeRez only knows about the
#     resources themselves, not the file's own Finder type/creator,
#     so that's captured separately as a human-readable leading
#     comment (Rez ignores C-style comments, so it doesn't affect
#     recompilation).
#   - anything else with a resource fork -> BinHex'd to <name>.hqx
#     (self-contained: data fork + resource fork + type/creator all
#     round-trip in one step, no extra metadata needed).
set -eu

root=$(git rev-parse --show-toplevel)
cd "$root"
# shellcheck source=lib.sh
. "$root/tools/mac-forks/lib.sh"

# Stable on purpose: this exact text is matched against .gitignore to
# find and replace the generated block, so it must not change even if
# this script gets renamed or reworded elsewhere.
GITIGNORE_BEGIN="# BEGIN mac-forks (generated -- do not edit by hand)"
GITIGNORE_END="# END mac-forks"

manifest=$(mktemp)
tmp_gi=$(mktemp)
trap 'rm -f "$manifest" "$tmp_gi"' EXIT

to_hqx() {
    /usr/bin/binhex encode -p "$1" >"$1.hqx"
}

to_r() {
    src=$1
    hex=$(xattr -px com.apple.FinderInfo "$src" 2>/dev/null || true)
    type=$(finderinfo_field "$hex" 0)
    creator=$(finderinfo_field "$hex" 4)
    {
        printf '/* mac-forks: type=%s creator=%s */\n' "$type" "$creator"
        /usr/bin/DeRez "$src"
    } >"$src.r"
}

find "$root" \( -path "$root/.git" -o -path "$root/tools/mac-forks" \) -prune -o -type f -print |
while IFS= read -r f; do
    rel=${f#"$root"/}

    has_ext_ci "$rel" hqx && continue
    has_ext_ci "$rel" r && continue

    rsize=$(rsrc_fork_size "$f")
    [ "$rsize" -gt 0 ] || continue

    if has_ext_ci "$rel" rsrc && [ "$(data_fork_size "$f")" -eq 0 ]; then
        to_r "$f"
        git add -- "$f.r"
        echo "derez:  $rel -> $rel.r"
    else
        to_hqx "$f"
        git add -- "$f.hqx"
        echo "binhex: $rel -> $rel.hqx"
    fi

    # The real file should never itself be tracked. .gitignore (below)
    # only stops *future* `git add`, so also proactively untrack it in
    # case it was already committed (e.g. migrating a repo onto these
    # tools) or got staged some other way (e.g. `git add -A`).
    git rm --cached --ignore-unmatch -q -- "$f" >/dev/null 2>&1 || true

    printf '%s\n' "$rel" >>"$manifest"
done

# Drop sidecars for real files that no longer qualify (renamed,
# deleted, lost their resource fork) so the repo doesn't accumulate
# dead .hqx/.r files.
git -c core.quotePath=false ls-files | while IFS= read -r tracked; do
    if has_ext_ci "$tracked" hqx; then
        real=${tracked%.*}
    elif has_ext_ci "$tracked" r; then
        real=${tracked%.*}
    else
        continue
    fi
    grep -qxF "$real" "$manifest" 2>/dev/null && continue
    [ -e "$real" ] && continue
    git rm -q -- "$tracked"
    echo "removed stale sidecar: $tracked"
done

if [ -s "$manifest" ]; then
    if [ -f .gitignore ]; then
        awk -v b="$GITIGNORE_BEGIN" -v e="$GITIGNORE_END" '
            $0 == b {skip=1; next}
            $0 == e {skip=0; next}
            !skip
        ' .gitignore >"$tmp_gi"
    fi
    {
        cat "$tmp_gi" 2>/dev/null
        echo "$GITIGNORE_BEGIN"
        sort -u "$manifest"
        echo "$GITIGNORE_END"
    } >.gitignore
    git add .gitignore
fi
