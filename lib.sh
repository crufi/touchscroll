# Shared helpers for the mac-forks export/import scripts and hooks.
#
# Kept project-agnostic on purpose: no filenames, no assumptions about
# a specific project's layout. This whole tools/mac-forks/ directory is
# meant to be copied as-is into other vintage Mac projects.
#
# Sourced with `.`, not executed -- no shebang.

rsrc_fork_size() {
    if [ -e "$1/..namedfork/rsrc" ]; then
        wc -c <"$1/..namedfork/rsrc" | tr -d ' \n'
    else
        echo 0
    fi
}

data_fork_size() {
    if [ -e "$1" ]; then
        wc -c <"$1" | tr -d ' \n'
    else
        echo 0
    fi
}

# has_ext_ci PATH EXT -- case-insensitive, exact dotted-extension match
# (EXT without its leading dot, e.g. "r" or "hqx"). Deliberately NOT a
# bare suffix match: that would wrongly match e.g. "Reader.c" against
# extension "r".
has_ext_ci() {
    lower=$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')
    ext=$(printf '%s' "$2" | tr '[:upper:]' '[:lower:]')
    case "$lower" in
        *".$ext") return 0 ;;
        *) return 1 ;;
    esac
}

# finderinfo_field HEXBYTES OFFSET -- OFFSET 0 = type, OFFSET 4 =
# creator. HEXBYTES is the space-separated output of `xattr -px
# com.apple.FinderInfo`. Renders each byte as its ASCII character when
# printable, or \xHH when not, so the result is always safe to embed
# in a comment and always round-trips exactly.
finderinfo_field() {
    hexstr=$1
    offset=$2
    set -- $hexstr
    shift "$offset" 2>/dev/null || true
    out=""
    i=0
    for byte in "$@"; do
        [ "$i" -ge 4 ] && break
        dec=$((16#$byte))
        if [ "$dec" -ge 32 ] && [ "$dec" -le 126 ]; then
            fmt="\\$(printf '%03o' "$dec")"
            out="$out$(printf "$fmt")"
        else
            out="${out}\\x$(printf '%02X' "$dec")"
        fi
        i=$((i + 1))
    done
    printf '%s' "$out"
}
