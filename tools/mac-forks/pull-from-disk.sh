#!/bin/sh
# Pulls files off a disk image (plain .img or a djjr-converted .hda) back
# into the git working tree -- the reverse of build-floppy.sh. For when
# files got edited directly in the emulator (inside the IDE, say) and
# those edits only exist on the disk image so far.
#
# Usage: tools/mac-forks/pull-from-disk.sh <disk-image> [hfs-start-folder]
#
# hfs-start-folder, if given (e.g. "MyProject" or "MyProject:Sources"),
# limits the "new files" pass (below) to that folder and everything
# under it, recursively -- contents land relative to the repo root, as
# if that folder's contents were the root (its own name isn't part of
# the resulting local paths). Meant for bootstrapping a new project
# straight from an existing .hda: point this at wherever the real
# project lives on the volume, skip whatever else is on there.
# Omit it to walk the whole volume from its root, as before.
#
# Driven by the same git-attribute discovery as build-floppy.sh/export.sh/
# import.sh: tracked .hqx/.r sidecars name the real, materialized forked
# files; filter=mactext names the real text files. No per-project file
# list needed.
#
# Also rescues files that exist on the disk with no tracked counterpart
# at all -- created directly in the emulator, never added to git. Pulled
# via MacBinary regardless of what they turn out to be (there's no
# tracked .gitattributes match to consult yet for something never
# tracked), which faithfully preserves whatever's actually there.
#
# Runs export.sh at the end, so tracked .hqx/.r sidecars immediately
# reflect whatever came back -- confirmed this matters: without it, the
# working tree has fresh content but the *tracked* sidecars (what
# image.mk's TRACKED_FILES dependency actually watches) don't change,
# so Make doesn't see disk.img as needing a rebuild, disk.img's mtime
# stays stale, and guard-overwrite.sh -- which compares disk.hda against
# disk.img, not local files -- keeps reporting the same "may hold
# changes" warning even immediately after a successful pull. Does NOT
# run `git add`/commit beyond what export.sh itself stages (new
# real-file sidecars, .gitignore) -- `git status`/`git diff` afterward
# show exactly what came back from the emulator, same as any other
# local edit.
#
# Requires hfsutils (hmount/humount/hcopy/hls) and macbinary -- same as
# build-floppy.sh.
set -eu

for tool in hmount humount hcopy hls; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "$0: missing $tool -- install hfsutils (brew install hfsutils)" >&2
        exit 1
    fi
done
for tool in macbinary iconv; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "$0: missing $tool -- expected to ship with base macOS" >&2
        exit 1
    fi
done

disk=${1:?"usage: $0 <disk-image> [hfs-start-folder]"}
[ -f "$disk" ] || { echo "$0: $disk: no such file" >&2; exit 1; }
start_folder=${2:-}

root=$(git rev-parse --show-toplevel)
cd "$root"
# shellcheck source=lib.sh
. "$root/tools/mac-forks/lib.sh"

# Same byte-level reasoning as build-floppy.sh's hfsname(): HFS catalog
# names are raw Mac Roman bytes, not UTF-8, so writing/matching them
# needs an explicit conversion, not whatever the locale happens to do.
hfsname() {
    printf '%s' "$1" | LC_ALL=C iconv -f UTF-8 -t MACINTOSH
}

# Reverse direction, for turning a raw HFS catalog name (Mac Roman) back
# into a usable local filename -- best-effort, falls back to the raw
# bytes if a file was named with something outside Mac Roman's
# repertoire (shouldn't happen from a real HFS volume).
display_name() {
    printf '%s' "$1" | LC_ALL=C iconv -f MACINTOSH -t UTF-8 2>/dev/null || printf '%s' "$1"
}

tmp_root=$(mktemp -d)
trap 'rm -rf "$tmp_root"' EXIT

humount 2>/dev/null || true   # in case a previous run left something mounted
hmount "$disk"

# The start folder scopes EVERY pass below, not just the new-files walk
# -- tracked files live under it on the volume too. Confirmed the hard
# way: with the prefix applied only to the walk, the tracked-file passes
# looked names up at the volume root, found nothing (hcopy's stderr is
# deliberately swallowed there), and silently updated zero tracked files
# while the run still ended "done pulling" -- an existing repo pulled
# against a subfolder was a complete no-op for everything already
# tracked. Validated up front so a typo'd folder fails loudly instead
# (hls exits 0 even for a nonexistent path, so stderr is the signal).
hfs_prefix=
if [ -n "$start_folder" ]; then
    hfs_prefix="$(hfsname "$start_folder"):"
    hls_err=$(hls -l ":${hfs_prefix%:}" 2>&1 >/dev/null)
    if [ -n "$hls_err" ]; then
        echo "$0: \"$start_folder\" not found on $disk: $hls_err" >&2
        humount
        exit 1
    fi
fi

# Forked files: pull as MacBinary (both forks + type/creator in one
# blob) and decode back into a real macOS file, overwriting whatever's
# currently reconstituted at that path. Decode into a tmpdir first and
# only replace the real file once that's succeeded -- same reasoning as
# import.sh's from_hqx: a failed decode should leave the existing file
# alone, not delete it and come up empty.
git -c core.quotePath=false ls-files | while IFS= read -r f; do
    if has_ext_ci "$f" hqx || has_ext_ci "$f" r; then
        real=${f%.*}
        tmp=$(mktemp -d)
        if hcopy -m ":$hfs_prefix$(hfsname "$real")" "$tmp/blob.bin" 2>/dev/null; then
            macbinary decode -p -C "$tmp" -o restored <"$tmp/blob.bin"
            mkdir -p "$(dirname "$real")"
            rm -rf "${real:?}"
            mv "$tmp/restored" "$real"
            touch "$real"   # macbinary decode sets mtime from the MacBinary
                             # header's own date field, not extraction time --
                             # confirmed empirically it can be hours stale,
                             # which would make guard-overwrite.sh re-flag a
                             # file we just pulled as still needing a pull
            echo "hcopy -m: $real"
        fi
        rm -rf "$tmp"
    fi
done

# mactext-filtered text files: pull the data fork raw (no translation --
# same hcopy -a/-t double-conversion bug build-floppy.sh works around).
# The bytes on disk are already genuine Mac Roman + CR, exactly what the
# working tree expects -- git's own mactext clean filter normalizes them
# at the next `git add`.
git -c core.quotePath=false ls-files | while IFS= read -r f; do
    attr=$(git check-attr filter -- "$f" | awk -F': ' '{print $NF}')
    if [ "$attr" = mactext ]; then
        if hcopy -r ":$hfs_prefix$(hfsname "$f")" "$f.pulled" 2>/dev/null; then
            mv "$f.pulled" "$f"
            echo "hcopy -r: $f"
        fi
    fi
done

# New files: present on the disk, no tracked counterpart at all -- build
# the same "expected HFS name per tracked candidate" list guard-overwrite.sh
# uses, then anything on the disk that doesn't match gets pulled too.
: >"$tmp_root/candidate_hfsnames"
git -c core.quotePath=false ls-files | while IFS= read -r f; do
    if has_ext_ci "$f" hqx || has_ext_ci "$f" r; then
        real=${f%.*}
    else
        attr=$(git check-attr filter -- "$f" | awk -F': ' '{print $NF}')
        [ "$attr" = mactext ] || continue
        real=$f
    fi
    printf '%s\n' "$(hfsname "$real")" >>"$tmp_root/candidate_hfsnames"
done

# Recursive: hls/hcopy have no built-in recursive mode, but HFS paths are
# just colon-separated components from the volume root (":Sub:Nested:File"),
# so walking the catalog by hand and recursing into directory entries
# works fine. Finder-junk folders are skipped by name -- nothing useful
# ever lives in them, and pulling "Trash" contents would be actively
# wrong. hfs_dir is always either "" (volume root, no path arg needed)
# or a leading-colon path; local_dir is the matching local prefix.
walk() {
    local hfs_dir=$1
    local local_dir=$2
    local line dname fname child_hfs real tmp
    if [ -z "$hfs_dir" ]; then
        hls -l 2>/dev/null
    else
        hls -l "$hfs_dir" 2>/dev/null
    fi | while IFS= read -r line; do
        case "$line" in
            d\ *)
                set -- $line
                shift 6   # flag, count, "item(s)", mon, day, time/year
                dname="$*"
                case "$dname" in
                    Trash|"Temporary Items"|"Desktop Folder"|Desktop|"Rescued Items"|"Network Trash Folder") continue ;;
                esac
                if [ -z "$hfs_dir" ]; then
                    walk ":$dname" "$local_dir$(display_name "$dname")/"
                else
                    walk "$hfs_dir:$dname" "$local_dir$(display_name "$dname")/"
                fi
                ;;
            f\ *|F\ *)
                set -- $line
                shift 7
                fname="$*"
                if [ -z "$hfs_dir" ]; then
                    child_hfs=":$fname"
                else
                    child_hfs="$hfs_dir:$fname"
                fi
                real="$local_dir$(display_name "$fname")"
                grep -qxF "$(hfsname "$real")" "$tmp_root/candidate_hfsnames" 2>/dev/null && continue
                tmp=$(mktemp -d)
                if hcopy -m "$child_hfs" "$tmp/blob.bin" 2>/dev/null; then
                    macbinary decode -p -C "$tmp" -o restored <"$tmp/blob.bin"
                    mkdir -p "$(dirname "$real")"
                    rm -rf "${real:?}"
                    mv "$tmp/restored" "$real"
                    touch "$real"
                    echo "hcopy -m (new): $real"
                fi
                rm -rf "$tmp"
                ;;
        esac
    done
}

# (start_folder was already Mac-Roman-converted and validated up top,
# before the tracked-file passes -- see hfs_prefix.)
if [ -n "$start_folder" ]; then
    walk ":${hfs_prefix%:}" ""
else
    walk "" ""
fi

humount

# Regenerate tracked sidecars from whatever just landed -- see the
# comment at the top of this file for why this can't be skipped.
"$root/tools/mac-forks/export.sh"

echo "done pulling from $disk"
