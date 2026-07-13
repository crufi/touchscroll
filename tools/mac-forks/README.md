# mac-forks

Git tools for tracking classic Mac OS files that mainly live in their **resource
fork** rather than their data fork — project files, ResEdit resource files,
anything from the Symantec/THINK C/CodeWarrior/MPW era.

See [Setting up a new project](#setting-up-a-new-project)
below for a step-by-step checklist. The rest of this README explains what
each piece does and why.

## The problem

Git only ever sees a file's data fork. A Symantec C++ project (`.π`) or a
ResEdit resource-only file (`.rsrc`) typically has an empty data fork —
everything that matters lives in the resource fork, which git has no concept
of at all. `git add` such a file and you silently commit nothing.

## The approach

Two scripts, driven entirely by file attributes (not stricly filenames, so this
directory can be dropped into any vintage Mac project unchanged):

- **`export.sh`** scans the working tree for files with a non-empty resource
  fork and encodes each one to a plain-text sidecar that git can actually
  store and diff:
  - a **`*.rsrc`-named file (case-insensitive) with an empty data fork** is
    decompiled with `DeRez` to `<name>.r`. Since `DeRez` only knows about the
    resources themselves — not the file's own Finder type/creator — that's
    captured separately as a human-readable leading comment, e.g.
    `/* mac-forks: type=rsrc creator=RSED */`.
  - **anything else with a resource fork** is archived with `binhex encode` to
    `<name>.hqx` — self-contained (data fork + resource fork + type/creator
    all round-trip in one step). (We prefer binhex here to the more space-efficient
    MacBinary format just for github display nicety.)

  The real, fork-bearing files are gitignored (`export.sh` maintains a
  generated block in `.gitignore` automatically) and never tracked directly.

- **`import.sh`** does the reverse: finds `.hqx`/`.r` sidecars and
  reconstitutes the real files from them, restoring the resource fork and
  Finder type/creator exactly.

Both are wired up as git hooks (see `install.sh`):

| Hook | Runs | Why |
|---|---|---|
| `pre-commit` | `export.sh` | keeps the tracked sidecars in sync with whatever real files exist before every commit |
| `post-checkout` | `import.sh` | rebuilds real files after `checkout`/`clone`/`switch` |
| `post-merge` | `import.sh` | rebuilds real files after `merge`/`pull` (a fetch+merge doesn't go through checkout, so this needs its own hook) |

Because the real files are gitignored rather than tracked, `import.sh` writes
them directly — there's no tracked path for git's own checkout machinery to
be racing against.

## Classic Mac text (`mactext`)

Separately from resource forks, classic Mac OS text files have two
properties that break modern git tools and GitHub's viewer:

- Lines are separated by a bare CR (0x0D), not LF — git's own
  line-ending handling (`core.autocrlf`, `text=auto`, `eol=`) only
  understands LF and CRLF, so it can't help, and a diff (or GitHub's blob
  view) of a CR-only file just renders as one giant line.
- The encoding is MacRoman, not UTF-8 — so anything above ASCII
  (curly quotes, em dashes, accented letters, ©) is the wrong character
  if read back as UTF-8 or Latin-1. (Byte `0xD4` is a left curly quote in Mac
  Roman but shows up as `Ô` if something reads it as Latin-1/UTF-8 instead
  — which is exactly what GitHub's viewer does, since it doesn't know the
  file is MacRoman.)

`mactext-clean`/`mactext-smudge` fix both together (git only allows one
`filter=` per path, so this can't be two independent filters layered on the
same files): clean converts MacRoman → UTF-8 then CR → LF, so the stored
blob is ordinary, correctly-rendering, diffable text; smudge reverses both
so the working copy still has genuine Mac Roman, CR-only text.

Unlike the resource-fork tools, this has no forks or races to worry about —
it's a plain stdin/stdout content transform (`iconv` + `tr`), so it's wired
as an ordinary git filter rather than a hook. (`iconv`/`tr` ship with base
macOS, no Xcode Command Line Tools required for this part specifically.)

`mactext-clean` also captures the file's current Finder type/creator, if it
has one, as a human-readable leading comment:

```
/* auto-generated (do not modify): type=TEXT creator=KAHL hex=544558544B41484C... */
```

Where that ever comes from: these files don't normally carry Finder info at
all (most editors don't preserve xattrs) -- but the *first* time one is
checked in, it's expected to have been pulled straight off a real vintage
volume (e.g. via `hcopy`), which is exactly when it'd have a meaningful
type/creator to capture. Once captured, it's carried forward automatically:
if a later commit has no live Finder info (the common case), the previous
value already in the comment is preserved rather than silently dropped.

Restoring it on checkout does **not** happen inside `mactext-smudge` --
same reasoning as the resource-fork tools: a smudge filter's stdout *is* the
file content git writes, so it can't safely side-effect the same path's
Finder info too. Instead `import.sh` does it as a separate pass, once git has
already checked the file out, reading the marker from the tracked blob
(always LF, guaranteed by `mactext-clean`) rather than the working-tree copy
(which is genuinely CR-only after smudge, so ordinary `head`/`sed` can't
pull "just the first line" back out of it).

Add it to whichever extensions your project's classic Mac source uses, in
your own `.gitattributes` (see [Setting up a new project](#setting-up-a-new-project)):

```
*.c filter=mactext -text
*.h filter=mactext -text
```

### `macroman` — the encoding half alone, for `.r` sidecars

`export.sh`'s own DeRez-generated `.r` sidecars need the Mac-Roman-encoding
fix too — DeRez's hex-dump comments embed raw bytes straight from the
resource fork, and for text-bearing resources (`STR#`, `vers`, an owner-name
resource, etc.) those bytes are genuine Mac Roman prose that renders wrong on
GitHub, the same as any other Mac Roman byte would.

But they do **not** need (and must not get) the CR↔LF half: DeRez always
emits LF-terminated output on this machine, right now — it's a live tool's
output, not a persisted vintage file, so there's no CR convention to
preserve. Running `mactext` on a `.r` sidecar would wrongly inject CR line
endings into it and break `import.sh`'s own `head -n 1` parsing of its
leading type/creator comment (which assumes LF). Confirmed empirically: an
encoding-only round trip matches byte-for-byte; adding CR↔LF conversion
turned the whole file into a single line.

So `.r` sidecars get their own filter, `macroman`, which is exactly
`mactext-clean`/`-smudge` minus the `tr` step — Mac Roman ↔ UTF-8 only,
line endings untouched:

```
*.r filter=macroman -text
```

## Building a floppy image (`build-floppy.sh`)

For loading a project into an emulator (Snow, Mini vMac, Basilisk II,
whatever): `build-floppy.sh` builds a plain HFS floppy image (no partition
map -- real floppies never had one) containing every file mac-forks tracks
for the project, with no per-project file list. It finds them the same way
`export.sh`/`import.sh` do: tracked `.hqx`/`.r` sidecars name the real,
materialized forked files, and `filter=mactext` names the real text files.

```sh
tools/mac-forks/build-floppy.sh out.img [blocks] [label] [text_creator]
```

- `blocks` -- 512-byte blocks, default 8192 (4MB). Real floppies topped out
  at 1.44MB; most emulators are lenient about `--floppy`-style attached
  images being larger, but that's emulator-specific -- worth confirming
  yours actually accepts it.
- `text_creator` -- stamped onto text files via `hattrib` so an IDE
  recognizes/double-click-opens them (e.g. `KAHL` for Symantec/THINK C).
  Left unset, text files still get the correct `TEXT` type (from `hcopy`'s
  own auto-detection) but a generic creator.

`.π`/`.rsrc`-style files carry a real resource fork, and `hcopy` has no idea
macOS files can have one at all -- it silently reads only the (usually
empty) data fork and produces a zero-byte, type-`????` file. So this bridges
through MacBinary first (`macbinary encode`, both forks + type/creator in
one blob), then `hcopy -m` to decode it properly onto the volume. Confirmed
empirically: `hcopy -m` directly on a real macOS file with a resource fork
fails outright ("error reading MacBinary file header"); going through this
bridge round-trips exactly.

Requires `hfsutils` (`brew install hfsutils`) and `macbinary` (ships with
base macOS) -- checked by the script itself, not `install.sh`, since most
mac-forks consumers never need this.

## Pulling edits back out of a disk image (`pull-from-disk.sh`)

If you edit files directly inside the emulator (in the IDE, say) rather
than on the host, those edits only exist on the disk image until something
brings them back. `pull-from-disk.sh` is `build-floppy.sh` run in reverse --
same file discovery, opposite direction:

```sh
tools/mac-forks/pull-from-disk.sh <disk-image> [hfs-start-folder]
```

Works on a plain `.img` or a `djjr`-converted `.hda` (mounts either with
plain `hmount`). Forked files come back via `hcopy -m` + `macbinary
decode`; `mactext` files come back via `hcopy -r` (raw -- same
double-conversion bug `build-floppy.sh` works around applies here too),
which lands them in the working tree exactly as genuine Mac Roman + CR,
ready for git's own `mactext` filter to normalize at the next `git add`.

Also rescues files that exist on the disk with no tracked counterpart at
all -- created directly in the emulator, never added to git. These are
pulled via MacBinary regardless of what they turn out to be, since
there's no `.gitattributes` match to consult for something that's never
been tracked. `export.sh` (run automatically at the end, see below) then
sorts out what each one actually is: `filter=mactext` if the extension
matches, or its own resource-fork detection sidecars it if it's
genuinely forked.

That "new files" pass is recursive -- `hls`/`hcopy` have no built-in
recursive mode, but HFS paths are just colon-separated components from
the volume root, so walking the catalog by hand and descending into
directory entries works fine. Finder-junk folders (`Trash`, `Temporary
Items`, `Desktop Folder`, etc.) are skipped by name. Give it
`hfs-start-folder` (e.g. `MyProject` or `MyProject:Sources`) to limit the
walk to that folder and everything under it -- its own name is dropped
from the resulting local paths, so contents land as if that folder's
contents *were* the volume root. This is the way to bootstrap a brand
new project straight from an existing `.hda`: point it at wherever the
real project lives on the volume (skipping whatever else -- an old
System Folder, other unrelated projects -- sits alongside it), before
`.gitattributes`/`.gitignore` even exist yet. Omit it to walk the whole
volume from its root, as before. It's converted from UTF-8 to Mac Roman
before use (same as every other HFS name here), and checked up front --
a folder that doesn't exist on the volume fails loudly rather than
silently pulling nothing, which is what happened before this check
existed (`hls` prints its own "no such file or directory" but still
exits 0, so a naive exit-status check doesn't catch it either).

Runs `export.sh` at the end, so tracked `.hqx`/`.r` sidecars immediately
reflect whatever came back -- without this, the working tree has fresh
content but `image.mk`'s `tracked-files stamp` dependency (which watches the
*sidecars*, not the real files) never changes, so the `.img` never
rebuilds and [`guard-overwrite.sh`](#guarding-against-overwriting-in-emulator-edits-guard-overwritesh)
keeps warning even right after a successful pull. `git status`/`git
diff` afterward show exactly what came back from the emulator, same as
any other local edit.

Wired into `snow.mk` as `make pull` -- see below.

## Attaching a disk in Snow (`snow-attach-disk.py`)

For Snow specifically (a Mac II-class emulator): its `--floppy` doesn't
recognize a plain HFS image the way real floppy hardware would (confirmed:
doesn't boot/mount), and attaching a SCSI/HDD-style device image has no CLI
flag at all -- but that's just an edit to the workspace's own JSON
(`scsi_targets`), the same edit Snow itself makes when you attach a disk
through the GUI and save.

```sh
tools/mac-forks/snow-attach-disk.py <template.snoww> <disk.hda> <output.snoww>
```

Copies `template.snoww`, adding `disk.hda` as an additional SCSI target in
the first empty slot, writing the result to `output.snoww`. Touches only
that one new entry -- every other field (ROM, PRAM, existing disks) is left
exactly as the template has it, which matters: those are typically bare
relative filenames, and Snow resolves them relative to wherever the
workspace file itself lives on disk (confirmed by a real "Failed to load
workspace: No such file or directory" until this was accounted for). So
`output.snoww` generally needs to land in the same directory as the
template (e.g. Snow's own install directory) for those to keep resolving --
`disk.hda` itself is fine anywhere, since it gets an absolute path.

`disk.hda` needs to be a SCSI-style device image (partition map + driver),
not the plain HFS image `build-floppy.sh` produces directly -- convert with
[djjr](https://diskjockey.onegeekarmy.eu/) first: `djjr convert to-device
in.img out.hda`.

### Wiring it all together for Snow (`snow.mk`)

`build-floppy.sh`, `djjr convert to-device`, and `snow-attach-disk.py`
chained together is the whole recipe for "open this project in Snow" --
and that chain is identical across every project that uses it, so it lives
in one shared `snow.mk` rather than getting hand-copied into each
project's own Makefile. Include it and set the two things that actually
vary per project:

```makefile
SNOW_WORKSPACE ?= $(HOME)/Snow/your-workspace.snoww   # required, no default
TEXT_CREATOR   := KAHL                                 # required -- your toolchain's creator code

include tools/mac-forks/snow.mk
```

Gives you `make`/`make all` (builds both `<project>.img` and
`<project>.hda`), `make run` (builds, then launches Snow with it
attached), `make pull` (pulls edits back out of the *existing* `.hda` --
see
[Pulling edits back out of a disk image](#pulling-edits-back-out-of-a-disk-image-pull-from-disksh)
above; deliberately doesn't trigger a rebuild first, since that would
destroy the very edits it's meant to rescue), and `make clean`.

Artifacts are named after the project: `PROJECT` defaults to the repo
directory's own name (`todays-the-day` → `build/todays-the-day.img`,
`todays-the-day.hda`, `todays-the-day.snoww`, and the release zip). It
has to stay a filesystem-safe slug -- Make splits prerequisite lists on
whitespace, so a target with a space in its name can't work. The pretty,
human name goes in `VOLUME_LABEL` instead -- the HFS volume name shown
inside the emulator, where spaces and apostrophes are fine:

```makefile
VOLUME_LABEL := Today's the Day
```

Left unset, the volume is just named after the `PROJECT` slug.
`SNOW_PATH`, `VOLUME_BLOCKS`, and `BUILD_DIR` also have reasonable
defaults (see the top of `snow.mk`/`image.mk`) and can be overridden the
same way, set before the `include` line.

### Guarding against overwriting in-emulator edits (`guard-overwrite.sh`)

Rebuilding the `.hda` (via `djjr convert to-device`) or running `make
clean` both destroy whatever's currently on it -- fine normally, not fine
if it holds edits `make pull` hasn't rescued yet. Both are gated by
`guard-overwrite.sh`, which checks two things before letting either
proceed:

- **Whole-image**: is the `.hda` itself newer than the `.img` it was
  last converted from (plus a small tolerance for normal build
  sequencing)?
- **Per-file**: does any individual file's HFS catalog modification date
  (`hls -l`) fall after that same reference point?

Both are compared against the `.img`'s own mtime, not local source files
directly -- a build always happens strictly after the local files it
reads, so comparing against local mtimes flags every ordinary build for
no real reason (confirmed; that was the first version's bug). The check
runs *after* the `.img` has had its chance to rebuild but *before*
anything touches the `.hda` -- so the comparison uses the freshest
possible reference (crucially, that's what lets a `make pull` actually
clear the warning: the pull regenerates the tracked sidecars, the next
`make` rebuilds the `.img` from them, and only then does the guard
compare). To keep repeated no-op builds from drifting the two apart
purely by wall-clock time, the `.hda`'s mtime is pinned to the `.img`'s
after every conversion -- the only way they diverge is something else
(the emulator) writing to the `.hda` in between.

The per-file check also catches files that exist on the `.hda` with no
tracked/local counterpart at all -- created directly in the emulator,
never pulled before -- and lists those separately, in green.

If either check trips, it prints exactly what it found and requires
typing a phrase before continuing: `BORK N` (N = the number of specific
files flagged) if the per-file check caught anything, or `BORK DISK` if
only the whole-image check did. Anything else aborts -- the existing
`.hda` is left untouched, and the calling `make` target fails.

Set `FORCE=1` (e.g. `make run FORCE=1`) to skip the check entirely --
needed for non-interactive use, since the prompt reads from stdin and
would otherwise hang.

The disk-image build rule itself actually lives in `image.mk`, which
`snow.mk` includes -- see the next section for why that's a separate file.

### Packaging a release (`release.mk`)

Builds a versioned zip of the project's source disk image -- not a
compiled binary; there's no way to automate the actual build, since that
happens by hand inside whatever vintage IDE you're using.

```makefile
TEXT_CREATOR := KAHL

include tools/mac-forks/release.mk
```

Then `make release` (or `make release VERSION=v1.2.0` -- left unset, it
falls back to `git describe`, using the commit hash if the repo has no
tags yet). Writes to `dist/` by default (`RELEASE_DIR`, overridable).

If the including project's Makefile also pulled in `snow.mk` (before
`release.mk`), the zip bundles the `.hda` alongside the `.img` --
`DEVICE_IMAGE` being defined is what triggers this, so it's automatic, not
a separate flag. Projects that never use Snow don't get a `.hda` and don't
pick up a `djjr` dependency just from including `release.mk`.

Tagging the commit is deliberately **not** part of this target -- building
an artifact and declaring a commit "the v1.2.0 release" are different
actions, and the latter mutates shared repo state (visible to others, if
pushed), which a `make` target shouldn't do as a side effect. Do that the
plain way: `git tag -a v1.2.0 -m "..." && git push origin v1.2.0`.

Both `snow.mk` and `release.mk` need the same disk-image build rule, so it
lives once in `image.mk`, which each includes -- guarded against being
processed twice (`ifndef`/`endif`), so a project using both `snow.mk` and
`release.mk` together doesn't get a "overriding recipe for target"
warning from Make.

## Requirements

macOS with the Xcode Command Line Tools installed (`xcode-select --install`),
for `/usr/bin/binhex`, `/usr/bin/DeRez`, `/usr/bin/Rez`, `/usr/bin/SetFile`
(for the resource-fork tools only, not mactext).

## Setting up a new project

This repo is meant to be vendored via `git subtree`, at the fixed path
`tools/mac-forks/` (the scripts assume that path — they don't work run from
anywhere else). Checklist for wiring it into a fresh git repo around a
classic Mac project (THINK C/Symantec C++/CodeWarrior/MPW era); see the
sections above for what each piece does and why, and
[Requirements](#requirements) below for what needs to be installed first.

### 1. Init the repo

```sh
git init
git commit --allow-empty -m "Initial commit"
```

The empty commit matters -- `git subtree add` (next step) attaches onto
an existing commit, and a bare `git init` has none yet. Skipping it fails
with `fatal: ambiguous argument 'HEAD': unknown revision or path not in
the working tree.` (and often a second, related `working tree has
modifications` error, since git can't tell what's "modified" without a
HEAD to compare against). If you already have real project files sitting
in the directory ready to go, committing those instead of an empty
commit works exactly the same way -- either way, subtree just needs
*some* commit to exist first.

### 2. Create the GitHub repo, pull in mac-forks

```sh
gh repo create your-project-name --public --source=. --remote=origin   # or --private
git remote add mac-forks https://github.com/crufi/mac-forks.git
gh repo set-default origin
git subtree add --prefix=tools/mac-forks mac-forks main --squash
sh tools/mac-forks/install.sh
```

`gh repo set-default origin` matters the moment a second remote exists —
without it, `gh` can't always guess which one is the "real" GitHub repo,
and commands like `gh release create` (see step 9) fail later with `No
default remote repository has been set`. Setting it now, before it can
bite, beats debugging it after the fact.

`install.sh` checks for the required tools, symlinks the `pre-commit` /
`post-checkout` / `post-merge` hooks, configures the `mactext`/`macroman`
filters, and creates a starting `.gitattributes`/`Makefile` from
`tools/mac-forks/templates/` if you don't already have them (see the next
two steps for what to do with those). **Every clone needs to run this
once** — hooks and filter config live in `.git/`, which `git clone` never
populates.

### 3. Edit `.gitattributes`

The previous step already created one (from
`tools/mac-forks/templates/gitattributes.example`) if you didn't have
one — `install.sh` only ever creates it, never overwrites an existing
one, so this is safe to re-run on later clones. Edit the extension list
for your project. `*.r` gets `macroman` (encoding only — these are
mac-forks' own generated sidecars, always LF-native, never CR); your
actual vintage source gets `mactext` (encoding *and* CR↔LF, since those
files are genuinely CR-authored). Defaults to:

```
*.hqx -text
*.r filter=macroman -text

*.c filter=mactext -text
*.h filter=mactext -text
*.cp filter=mactext -text
*.cpp filter=mactext -text
*.hpp filter=mactext -text
```

Add more `filter=mactext -text` lines for whatever else your project has —
`.p`/`.pas` (Pascal), `.a`/`.asm`, etc.

⚠️ **Naming collision to watch for:** mac-forks generates `.r` sidecars for
resource-only files (`Foo.rsrc` → `Foo.rsrc.r`). If your project also has
genuine hand-written Rez source ending in `.r`, **rename them** (`.rez` or
similar instead of `.r`) before using mac-forks.

### 4. Normal `.gitignore` stuff

`export.sh` maintains its own generated block automatically (listing
whichever resource-fork-bearing files it finds) — leave that alone. You'll
still want the usual:

```
.DS_Store
```

### 5. Add your files, commit

Edit everything normally — including the real `.π`/`.rsrc` files directly in
ResEdit/the IDE. The `pre-commit` hook finds anything with a resource fork
and encodes it automatically; you never `git add` those files yourself.

```sh
git add .
git commit -m "Add project source"
```

### 6. Verify with a genuinely fresh clone

Problems in this kind of setup mostly only show up on a fresh clone — an
already-configured working copy hides plenty. Before trusting it:

```sh
cd /tmp && git clone /path/to/your/repo verify-me && cd verify-me
sh tools/mac-forks/install.sh
# diff verify-me's files against your real working copy
```

### 7. Push

`origin` is already set from step 2:

```sh
git push -u origin main
```

### 8. (Optional) Build + launch in an emulator

Not part of getting git tracking working — a separate, optional layer for
actually opening the project somewhere. If you use Snow, step 2 already
created a starting `Makefile` (from
`tools/mac-forks/templates/Makefile.example`) if you didn't have one —
edit `SNOW_WORKSPACE`/`TEXT_CREATOR` for your project. Defaults to:

```makefile
SNOW_WORKSPACE ?= $(HOME)/Snow/your-workspace.snoww   # point this at your own workspace
TEXT_CREATOR   := KAHL                                 # or whatever your vintage toolchain expects

include tools/mac-forks/snow.mk
include tools/mac-forks/release.mk
```

`make run` builds the disk image and launches Snow with it attached. See
[Wiring it all together for Snow](#wiring-it-all-together-for-snow-snowmk)
and [Packaging a release](#packaging-a-release-releasemk) above for what
else can be overridden (`SNOW_PATH`, `VOLUME_BLOCKS`, `VOLUME_LABEL`,
`BUILD_DIR`, `RELEASE_DIR`).

### 9. (Optional) Cutting a release

Tagging a commit and packaging its disk image are deliberately separate
steps — see [Packaging a release](#packaging-a-release-releasemk) above for
why. `gh release create` is what actually publishes it, attaching the zip
`make release` built:

```sh
git tag -a v1.0.0 -m "First public release"
git push origin v1.0.0
make release VERSION=v1.0.0
gh release create v1.0.0 dist/*-v1.0.0.zip \
  --title "v1.0.0" --notes "..."
```

If this fails with `No default remote repository has been set`, you
likely skipped `gh repo set-default origin` back in step 2 — needed as
soon as a second remote (`mac-forks`) exists, since `gh` can't always
guess which one is the "real" GitHub repo otherwise. Fix it the same way:

```sh
gh repo set-default owner/repo
```

## Keeping mac-forks up to date

Pulling in later mac-forks improvements:

```sh
git subtree pull --prefix=tools/mac-forks mac-forks main --squash
```

Pushing a change you made in-place back upstream:

```sh
git subtree push --prefix=tools/mac-forks mac-forks main
```

## Known limitations

- `DeRez`/`Rez` don't round-trip byte-for-byte — Rez recompiles a
  semantically equivalent resource fork, not necessarily identical bytes.
  Fine for ResEdit/an IDE/a linker; don't expect `cmp` to agree after a round
  trip.
- The `.r` path only ever produces an empty data fork on import (that's the
  only case it's used for: `*.rsrc`-named files with an empty data fork to
  begin with). If a resource-fork-bearing file also has real data-fork
  content, it always goes through the BinHex (`.hqx`) path instead, which is
  fully fork-agnostic.
- Hooks are local to each clone (`.git/hooks` is never populated by `git
  clone`) — **every clone needs to run `install.sh` once**. Until then, the repo
  is still completely valid; the fork-bearing files just stay as their
  `.hqx`/`.r` sidecars, unexpanded.
- `mactext` assumes the *only* control characters in the text are line
  breaks.
- MacRoman can't represent all of Unicode: if a file gets edited
  with a modern tool and picks up a character outside MacRoman's repertoire
  (an emoji, say), `mactext-clean`'s reverse conversion on the next checkout
  will fail with an error — `iconv` errors out on
  unencodable characters instead of guessing.
