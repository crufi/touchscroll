#!/bin/sh
# Builds an HFS floppy image containing every file mac-forks tracks for
# this project. Driven entirely by the same git-attribute discovery
# export.sh/import.sh/install.sh already use -- tracked .hqx/.r
# sidecars name the real, materialized forked files; filter=mactext
# names the real text files. No per-project file list needed at all.
#
# Usage: tools/mac-forks/build-floppy.sh <output.img> [blocks] [label] [text_creator]
#   blocks       512-byte blocks; default 8192 (4MB)
#   label        HFS volume name; default output filename's basename
#   text_creator Finder creator to stamp on text files (e.g. KAHL for
#                Symantec/THINK C) so an IDE double-click-opens them.
#                Left unset, text files just get whatever `-a` auto
#                mode assigns (correct type, generic creator).
#
# Requires hfsutils (brew install hfsutils) for hformat/hmount/humount/
# hcopy/hattrib, plus macbinary (ships with base macOS -- NOT part of
# hfsutils, don't try to brew-install it) and iconv/dd (also base
# macOS). None of this is part of mac-forks' core requirements (most
# consumers never need this script), so it's checked here rather than
# in install.sh.
set -eu

for tool in hformat hmount humount hcopy hattrib hmkdir; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "$0: missing $tool -- install hfsutils (brew install hfsutils)" >&2
        exit 1
    fi
done
for tool in dd macbinary iconv; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "$0: missing $tool -- expected to ship with base macOS" >&2
        exit 1
    fi
done

out=${1:?"usage: $0 <output.img> [blocks] [label] [text_creator]"}
blocks=${2:-8192}
label=${3:-$(basename "$out" | sed 's/\.[^.]*$//')}
text_creator=${4:-}

root=$(git rev-parse --show-toplevel)
cd "$root"
# shellcheck source=lib.sh
. "$root/tools/mac-forks/lib.sh"

# HFS catalog names are Mac Roman, not UTF-8 -- hcopy doesn't convert,
# it just writes whatever bytes you give it as the destination name.
# Handed a plain UTF-8 filename (macOS's own native encoding, which is
# all Unix filenames are), the result is silently wrong: "π" (UTF-8
# 0xCF 0x80) becomes "œÄ" on the classic Mac side, since those same two
# bytes are a different pair of characters in Mac Roman. Confirmed
# empirically -- this is exactly what happened before this existed.
hfsname() {
    printf '%s' "$1" | LC_ALL=C iconv -f UTF-8 -t MACINTOSH
}

mkdir -p "$(dirname "$out")"
rm -f "$out"
dd if=/dev/zero of="$out" bs=512 count="$blocks"
hformat -l "$(hfsname "$label")" "$out"

humount 2>/dev/null || true   # in case a previous run left something mounted
hmount "$out"

# Forked files: any tracked .hqx/.r sidecar names a real, materialized
# file with a genuine resource fork. hcopy has no idea macOS files can
# have one at all, so bridge through MacBinary (both forks +
# type/creator in one blob) and let hcopy -m decode it properly onto
# the volume.
git -c core.quotePath=false ls-files | while IFS= read -r f; do
    if has_ext_ci "$f" hqx || has_ext_ci "$f" r; then
        real=${f%.*}
        [ -f "$real" ] || continue
        tmp=$(mktemp -d)
        macbinary encode -p "$real" >"$tmp/blob.bin"
        hfsmkdirs "$real"
        hcopy -m "$tmp/blob.bin" ":$(hfspath "$real")"
        rm -rf "$tmp"
        echo "hcopy -m: $real"
    fi
done

# mactext-filtered text files are already genuine Mac Roman + CR on
# disk (mactext-smudge already did that conversion) -- hcopy -a/-t's
# own "text" mode isn't just CR/LF translation, it *also* reinterprets
# the Unix-side bytes as Latin-1 and re-encodes them into Mac Roman.
# Run an already-Mac-Roman file through that and every non-ASCII byte
# gets double-converted (confirmed: a curly quote came out the other
# side as an accented O). -r (raw) copies the data fork byte-for-byte,
# which is what we actually want here; it doesn't set type/creator
# though, so both are stamped explicitly afterward.
git -c core.quotePath=false ls-files | while IFS= read -r f; do
    attr=$(git check-attr filter -- "$f" | awk -F': ' '{print $NF}')
    if [ "$attr" = mactext ]; then
        hfsmkdirs "$f"
        hcopy -r "$f" ":$(hfspath "$f")"
        hattrib -t TEXT ":$(hfspath "$f")"
        [ -z "$text_creator" ] || hattrib -c "$text_creator" ":$(hfspath "$f")"
        echo "hcopy -r: $f"
    fi
done

humount
echo "done: $out"
