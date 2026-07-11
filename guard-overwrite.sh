#!/bin/sh
# Warns before something is about to destroy a disk image (typically a
# djjr-converted .hda) that might hold emulator-side edits nothing has
# pulled back into git yet -- see pull-from-disk.sh. Two independent
# checks; if either trips, requires typing an exact confirmation phrase
# before letting the caller proceed. Anything else aborts (non-zero
# exit), leaving the disk image untouched.
#
# Usage: tools/mac-forks/guard-overwrite.sh <disk-image> [built-from-image]
#
# MUST run before disk-image gets touched by anything this invocation --
# the caller (snow.mk) is responsible for that ordering. Everything here
# compares disk-image against built-from-image (e.g. disk.hda against the
# disk.img it was last converted from), not against local source files:
# a build always happens strictly after the local files it reads, so
# comparing against local mtimes directly means the disk always looks
# "newer" than its own sources, for no reason other than build sequencing
# -- confirmed, that was flagging every ordinary build. disk.hda and
# built-from-image, from the same prior build, should sit within a few
# seconds of each other; only something that touched disk-image
# afterwards (i.e. the emulator) should push it meaningfully later.
#
# Falls back to comparing against the newest local tracked file if
# built-from-image is omitted -- worse (re-introduces the false-positive
# risk above) but better than no check at all for standalone use.
#
# Set FORCE=1 (env or `make ... FORCE=1`) to skip all of this --
# needed for non-interactive use, since the confirmation prompt reads
# from stdin and would otherwise hang.
set -eu
export LC_ALL=C   # HFS catalog names off hls are raw Mac Roman bytes

disk=${1:?"usage: $0 <disk-image> [built-from-image]"}
ref_image=${2:-}

[ -z "${FORCE:-}" ] || exit 0
[ -f "$disk" ] || exit 0   # nothing to lose

for tool in hmount humount hls; do
    command -v "$tool" >/dev/null 2>&1 || { echo "$0: missing $tool -- install hfsutils (brew install hfsutils)" >&2; exit 1; }
done

root=$(git rev-parse --show-toplevel)
cd "$root"
# shellcheck source=lib.sh
. "$root/tools/mac-forks/lib.sh"

hfsname() {
    printf '%s' "$1" | iconv -f UTF-8 -t MACINTOSH
}

# Reverse direction, for displaying a raw HFS catalog name (Mac Roman)
# readably -- best-effort, falls back to the raw bytes if a file was
# named with something outside Mac Roman's repertoire (shouldn't happen
# from a real HFS volume, but don't fail the whole check over a display
# nicety).
display_name() {
    printf '%s' "$1" | iconv -f MACINTOSH -t UTF-8 2>/dev/null || printf '%s' "$1"
}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Candidate files: same discovery build-floppy.sh/pull-from-disk.sh use
# (tracked .hqx/.r sidecars name the real forked file; filter=mactext
# names the real text file directly) -- used only to label a flagged
# file as "modified" vs "new", never for timing.
git -c core.quotePath=false ls-files > "$tmp/tracked"

newest_local=0
: >"$tmp/candidate_hfsnames"  # every candidate's expected HFS catalog name, one per line
while IFS= read -r f; do
    if has_ext_ci "$f" hqx || has_ext_ci "$f" r; then
        real=${f%.*}
    else
        attr=$(git check-attr filter -- "$f" | awk -F': ' '{print $NF}')
        [ "$attr" = mactext ] || continue
        real=$f
    fi
    [ -e "$real" ] || continue
    mtime=$(stat -f %m "$real")
    [ "$mtime" -gt "$newest_local" ] && newest_local=$mtime
    printf '%s\n' "$(hfsname "$real")" >>"$tmp/candidate_hfsnames"
done <"$tmp/tracked"

# Reference point for everything below: when built-from-image (disk.img)
# was last written. A small tolerance covers the normal gap between
# build-floppy.sh finishing disk.img and djjr finishing disk.hda from it
# -- both part of the same prior build, typically a couple seconds apart.
if [ -n "$ref_image" ] && [ -f "$ref_image" ]; then
    ref_mtime=$(stat -f %m "$ref_image")
else
    ref_mtime=$newest_local
fi
tolerance=30
threshold=$((ref_mtime + tolerance))

disk_mtime=$(stat -f %m "$disk")
whole_flag=0
[ "$disk_mtime" -gt "$threshold" ] && whole_flag=1

# Per-file check: hls -l shows "Mon DD HH:MM" for recent files or
# "Mon DD  YYYY" for older ones -- a 4-digit third token means "use this
# year," an HH:MM token means "assume the current year" (hls's own
# recent-vs-old heuristic, same as classic ls -l).
humount 2>/dev/null || true
hmount "$disk" >/dev/null
: >"$tmp/newer_files"
: >"$tmp/new_files"
hls -l 2>/dev/null | while IFS= read -r line; do
    case "$line" in
        f\ *|F\ *) ;;
        *) continue ;;
    esac
    set -- $line
    mon=$5; day=$6; tyr=$7
    shift 7
    name="$*"
    # Force :00 seconds -- hls only has minute resolution, but `date -j
    # -f` fills any field missing from the input (seconds, here) from the
    # *current* wall-clock time rather than zero. Left alone, that makes
    # the parsed epoch drift later the longer this script takes to run.
    case "$tyr" in
        *:*) fmt="%b %d %Y %H:%M:%S"; ts="$mon $day $(date +%Y) $tyr:00" ;;
        *)   fmt="%b %d %Y %H:%M:%S"; ts="$mon $day $tyr 00:00:00" ;;
    esac
    epoch=$(date -j -f "$fmt" "$ts" +%s 2>/dev/null) || continue
    # Compared against the SAME threshold as the whole-image check above
    # -- a file's catalog date only matters here if something touched it
    # after the last build, exactly like the whole-image case. A file
    # that's simply old (an orphan nothing's cleaned up, say) doesn't
    # need flagging every single time just because it has no candidate.
    [ "$epoch" -gt "$threshold" ] || continue
    if grep -qxF "$name" "$tmp/candidate_hfsnames" 2>/dev/null; then
        echo "$name" >>"$tmp/newer_files"
    else
        echo "$name" >>"$tmp/new_files"
    fi
done
humount >/dev/null

modified_count=$(wc -l <"$tmp/newer_files" | tr -d ' ')
new_count=$(wc -l <"$tmp/new_files" | tr -d ' ')
file_count=$((modified_count + new_count))

# Red for the warning + confirmation prompt, green for new files
# specifically -- both skipped when stderr isn't a terminal (FORCE=1
# already exits before this point for non-interactive use, but a
# redirected-but-not-forced run shouldn't get raw escape codes in a log
# file).
if [ -t 2 ]; then
    RED=$(printf '\033[31m')
    GREEN=$(printf '\033[32m')
    RESET=$(printf '\033[0m')
else
    RED=
    GREEN=
    RESET=
fi

if [ "$whole_flag" = 1 ] || [ "$file_count" -gt 0 ]; then
    printf '%sWARNING: %s may hold changes that only exist there.%s\n' "$RED" "$disk" "$RESET" >&2
    [ "$whole_flag" = 1 ] && echo "  - $disk itself is newer than the image it was last built from" >&2
    if [ "$modified_count" -gt 0 ]; then
        echo "  - these files on $disk look newer than the last build:" >&2
        while IFS= read -r nf; do echo "      $(display_name "$nf")"; done <"$tmp/newer_files" >&2
    fi
    if [ "$new_count" -gt 0 ]; then
        echo "  - these files exist on $disk but have no local/tracked counterpart (new):" >&2
        while IFS= read -r nf; do
            printf '      %s%s%s\n' "$GREEN" "$(display_name "$nf")" "$RESET" >&2
        done <"$tmp/new_files"
    fi
    echo "" >&2
    echo "Run 'make pull' first if you want to keep those changes." >&2
    [ "$new_count" -gt 0 ] && echo "(note: new files land untracked -- git add them once you're happy with them)" >&2
    echo "" >&2
    if [ "$file_count" -gt 0 ]; then
        phrase="BORK $file_count"
    else
        phrase="BORK DISK"
    fi
    printf '%sType %s to overwrite %s and discard anything only stored there: %s' "$RED" "$phrase" "$disk" "$RESET" >&2
    read -r answer
    if [ "$answer" != "$phrase" ]; then
        echo "aborted -- $disk left untouched" >&2
        exit 1
    fi
fi
