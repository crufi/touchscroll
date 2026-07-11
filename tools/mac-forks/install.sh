#!/bin/sh
# Wires up the mac-forks hooks for THIS clone.
#
# Hooks live in .git/hooks, which git never populates from a clone --
# every clone needs to run this once:
#
#   sh tools/mac-forks/install.sh
#
# Without it, the repo is still completely valid: resource-fork-bearing
# files just stay as their .hqx / .r sidecars, unexpanded, and classic
# Mac text files just stay in their UTF-8/LF form. Running this makes
# the real files show up in the working tree as a vintage Mac
# toolchain expects.
#
# Requires macOS with the Xcode Command Line Tools installed (for
# /usr/bin/binhex, /usr/bin/DeRez, /usr/bin/Rez, /usr/bin/SetFile).
set -eu

root=$(git rev-parse --show-toplevel)

for tool in /usr/bin/binhex /usr/bin/DeRez /usr/bin/Rez /usr/bin/SetFile; do
    if [ ! -x "$tool" ]; then
        echo "install.sh: missing $tool -- install the Xcode Command Line Tools (xcode-select --install)" >&2
        exit 1
    fi
done

mkdir -p "$root/.git/hooks"
for hook in pre-commit post-checkout post-merge; do
    ln -sf "../../tools/mac-forks/hooks/$hook" "$root/.git/hooks/$hook"
done

# mactext and macroman have no forks/races to worry about, so they're
# plain git filters rather than hooks -- but the filter *driver* still
# has to be configured locally, same reasoning as the machex/macderez
# filters used to need (see this project's git history for why that
# approach was abandoned for resource forks specifically: git's own
# checkout can't be raced safely). Text-only content has no such problem.
git config filter.mactext.clean  "$root/tools/mac-forks/mactext-clean %f"
git config filter.mactext.smudge "$root/tools/mac-forks/mactext-smudge"
git config filter.mactext.required true

git config filter.macroman.clean  "$root/tools/mac-forks/macroman-clean"
git config filter.macroman.smudge "$root/tools/mac-forks/macroman-smudge"
git config filter.macroman.required true

echo "mac-forks hooks + filters installed for $root"

# Filter config only affects *future* checkouts. If this clone was
# checked out before a filter was configured, its filtered files are
# still sitting there un-smudged. A plain `git checkout HEAD -- .`
# doesn't fix this: git compares the clean-filtered worktree content
# against the stored blob first, and since an already-clean file
# cleans to itself, git thinks nothing changed and skips re-invoking
# smudge entirely. So instead, delete and re-checkout specifically the
# files each filter applies to -- with nothing on disk, git has no
# "already matches" shortcut to take, and smudge is forced to actually
# run.
force_resmudge() {
    filter_name=$1
    label=$2
    paths=$(mktemp)
    git -C "$root" -c core.quotePath=false ls-files | while IFS= read -r f; do
        attr=$(git -C "$root" check-attr filter -- "$f" | awk -F': ' '{print $NF}')
        if [ "$attr" = "$filter_name" ]; then
            printf '%s\n' "$f"
        fi
        true
    done >"$paths"
    if [ -s "$paths" ]; then
        # These filters can't announce themselves the way import.sh's
        # binhex/derez conversions do -- their stdout as a git filter
        # *is* the file content git writes, so a status line there
        # would corrupt the file. This is the only place that can say
        # it happened.
        echo "$label:"
        sed 's/^/  /' "$paths"
        while IFS= read -r f; do
            rm -f "$root/$f"
        done <"$paths"
        xargs git -C "$root" checkout HEAD -- <"$paths"
    fi
    rm -f "$paths"
}
force_resmudge mactext "converting to Mac Roman / CR line endings"
force_resmudge macroman "converting to Mac Roman"

echo "materializing real files from their .hqx/.r sidecars..."
"$root/tools/mac-forks/import.sh"

# .gitattributes/Makefile live in the project root, not tools/mac-forks/,
# so they're outside what git subtree pull manages -- only ever created
# here if genuinely missing, never overwritten. On a fresh project
# that's true; on every later clone the file's already checked out by
# git before install.sh even runs, so this is a no-op then, no separate
# "first run" tracking needed.
if [ ! -f "$root/.gitattributes" ]; then
    cp "$root/tools/mac-forks/templates/gitattributes.example" "$root/.gitattributes"
    echo "created .gitattributes from template -- edit it for your project's file types"
fi
if [ ! -f "$root/Makefile" ]; then
    cp "$root/tools/mac-forks/templates/Makefile.example" "$root/Makefile"
    echo "created Makefile from template -- edit SNOW_WORKSPACE/TEXT_CREATOR for your project"
fi

echo "done."
