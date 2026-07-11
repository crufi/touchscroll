#!/bin/sh
# Reconstitutes the real, gitignored, resource-fork-bearing files from
# their tracked .hqx / .r sidecars. Run automatically by the
# post-checkout / post-merge hooks; safe to run by hand too (e.g.
# after pulling, or `tools/mac-forks/import.sh`).
#
# Because the real files are gitignored rather than tracked, this
# writes them directly with no risk of racing git's own checkout
# machinery -- there's no tracked path for git to be materializing at
# the same time.
set -eu
# restore_finderinfo (below) runs `head`/`sed` over files that can
# contain raw high-bit Mac Roman bytes (mactext-tracked source) --
# under a UTF-8 locale those choke with "illegal byte sequence", same
# class of bug the mactext/macroman filters had to work around.
export LC_ALL=C

root=$(git rev-parse --show-toplevel)
cd "$root"
# shellcheck source=lib.sh
. "$root/tools/mac-forks/lib.sh"

from_hqx() {
    src=$1
    dest=${src%.*}
    tmp=$(mktemp -d)
    cp "$src" "$tmp/in.hqx"
    (cd "$tmp" && /usr/bin/binhex decode -n -o restored in.hqx)
    mkdir -p "$(dirname "$dest")"
    rm -rf "$dest"
    mv "$tmp/restored" "$dest"
    rm -rf "$tmp"
    echo "binhex: $src -> $dest"
}

from_r() {
    src=$1
    dest=${src%.*}
    first_line=$(head -n 1 "$src")
    type_raw=$(printf '%s' "$first_line" | sed -n 's#^/\* mac-forks: type=\(.*\) creator=\(.*\) \*/$#\1#p')
    creator_raw=$(printf '%s' "$first_line" | sed -n 's#^/\* mac-forks: type=\(.*\) creator=\(.*\) \*/$#\2#p')
    tmp=$(mktemp -d)
    /usr/bin/Rez "$src" -o "$tmp/restored"
    mkdir -p "$(dirname "$dest")"
    rm -rf "$dest"
    mv "$tmp/restored" "$dest"
    rm -rf "$tmp"
    if [ -n "$type_raw" ] && [ -n "$creator_raw" ]; then
        /usr/bin/SetFile -t "$(printf '%b' "$type_raw")" -c "$(printf '%b' "$creator_raw")" "$dest"
    fi
    echo "derez:  $src -> $dest"
}

find "$root" \( -path "$root/.git" -o -path "$root/tools/mac-forks" \) -prune -o -type f -print |
while IFS= read -r f; do
    rel=${f#"$root"/}
    if has_ext_ci "$rel" hqx; then
        from_hqx "$f"
    elif has_ext_ci "$rel" r; then
        from_r "$f"
    fi
done

# mactext-clean captures a file's Finder type/creator as a leading
# comment when it has one (see mactext-clean for where that info
# originally comes from). Restoring it can't happen inside
# mactext-smudge itself -- that's a git filter, and its stdout *is*
# the file content git writes; side-effecting the same path directly
# loses the exact race machex-smudge used to hit (git materializing
# the file at the same time something else tries to touch it). So
# this runs as its own pass, after git has already safely checked the
# file out, same as the resource-fork restoration above.
#
# Reads the marker line from the git BLOB (`git show`), not the
# smudged working-tree file: after mactext-smudge the working copy is
# genuinely CR-only, and `head`/`sed` split on LF, so they can't pull
# "just the first line" back out of it -- they'd read the whole file
# as one line. The blob is always LF (mactext-clean guarantees it),
# so reading from there sidesteps the problem entirely.
restore_finderinfo() {
    rel=$1
    dest="$root/$rel"
    [ -f "$dest" ] || return 0
    first_line=$(git -C "$root" show ":$rel" | head -n 1)
    hex=$(printf '%s' "$first_line" | sed -n 's#^/\* auto-generated (do not modify): .* hex=\([0-9A-Fa-f]*\) \*/$#\1#p')
    [ -n "$hex" ] || return 0
    xattr -wx com.apple.FinderInfo "$hex" "$dest"
    echo "finderinfo: $rel"
}

git -C "$root" -c core.quotePath=false ls-files | while IFS= read -r f; do
    attr=$(git -C "$root" check-attr filter -- "$f" | awk -F': ' '{print $NF}')
    if [ "$attr" = mactext ]; then
        restore_finderinfo "$f"
    fi
    true
done
