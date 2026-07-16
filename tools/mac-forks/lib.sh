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

# hfspath REPO_PATH -- convert a repo-relative path to an HFS path:
# each component UTF-8 -> Mac Roman, '/' separators -> ':'. HFS treats
# '/' as an ordinary filename character (its separator is ':'), so a
# repo path passed through unconverted lands on the volume as a single
# file literally named "old/foo.c" instead of foo.c inside a folder.
# (The one repo path this can't represent: a filename containing a
# literal ':', which macOS's POSIX layer technically allows -- it would
# read as a folder separator on the HFS side. Don't do that.)
hfspath() {
    printf '%s' "$1" | LC_ALL=C iconv -f UTF-8 -t MACINTOSH | LC_ALL=C tr '/' ':'
}

# hfsmkdirs REPO_PATH -- ensure the HFS folder chain for a repo path's
# directory part exists on the currently hmount'ed volume, one level at
# a time (hmkdir can't create intermediate folders in one call).
# "already exists" errors are expected and suppressed.
hfsmkdirs() {
    _rel=$1
    _acc=
    while :; do
        case "$_rel" in
            */*) _comp=${_rel%%/*}; _rel=${_rel#*/} ;;
            *) break ;;
        esac
        _acc="$_acc:$(printf '%s' "$_comp" | LC_ALL=C iconv -f UTF-8 -t MACINTOSH)"
        hmkdir "$_acc" 2>/dev/null || true
    done
}

# to_mactext -- stdin (text in any shape) -> stdout as Mac Roman + CR.
# Shape-aware: valid-UTF-8 input (a modern editor rewrote the file --
# VS Code has no bare-CR support and re-encodes on save) gets the full
# conversion; anything else is assumed already Mac Roman and only has
# its line breaks normalized. Every break style (CRLF, LF, lone CR)
# becomes exactly one CR, so a genuine Mac Roman/CR file passes through
# byte-identical. A UTF-8 character with no Mac Roman equivalent makes
# iconv fail loudly rather than guess.
to_mactext() {
    _tm=$(mktemp)
    cat >"$_tm"
    if iconv -f UTF-8 -t UTF-8 <"$_tm" >/dev/null 2>&1; then
        _tm2=$(mktemp)
        iconv -f UTF-8 -t MACINTOSH <"$_tm" >"$_tm2" || { rm -f "$_tm" "$_tm2"; return 1; }
        mv "$_tm2" "$_tm"
    fi
    perl -0777 -pe 's/\r\n?|\n/\r/g' <"$_tm"
    rm -f "$_tm"
}
