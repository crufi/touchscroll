# mac-forks

Develop classic Mac OS software in a modern git workflow. mac-forks makes
resource-fork-bearing files (THINK C / Symantec C++ / CodeWarrior / MPW
projects, ResEdit resource files) and Mac Roman, CR-terminated source text
first-class citizens of an ordinary git repo on today's macOS — and gets
the whole project on and off emulator disk images with one `make` command.

## What this solves

Classic Mac projects break modern tooling in specific, silent ways:

- **Resource forks vanish.** Git only sees a file's data fork. A Symantec
  project file (`.π`) or a ResEdit resource file (`.rsrc`) keeps everything
  that matters in its *resource* fork — `git add` one and you commit
  nothing. mac-forks encodes each fork-bearing file to a plain-text sidecar
  (`.hqx` / DeRez'd `.r`) that git can store and diff, and reconstitutes
  the real file on every checkout, automatically, via git hooks.
- **Vintage text renders as garbage.** Classic Mac source is Mac Roman
  encoded with bare-CR line endings: GitHub shows the whole file as one
  line, and every character above ASCII (curly quotes, ©, é…) displays
  wrong. A pair of git filters stores clean UTF-8/LF blobs — readable,
  diffable, reviewable — while your working tree keeps genuine Mac
  Roman/CR files a vintage toolchain expects. Finder type/creator codes
  survive the round trip too.
- **Getting code in and out of an emulator is manual drudgery.** `make`
  builds an HFS disk image containing the project, ready to mount in an
  emulator; `make run` attaches it and launches
  [Snow](https://snowemu.com/) directly. Edit inside the emulator's
  IDE, then `make pull` brings the changes back into your working tree as
  ordinary git modifications. A safety guard refuses to overwrite a disk
  image that still holds edits you haven't pulled.
- **Existing projects live on old disk images.** One command bootstraps a
  new repo straight from a folder on an existing HFS disk image — every
  file copied fork-intact and byte-exact, no lossy text-mode conversions.
- **Releases.** `make release` packages the source disk image into a
  versioned zip, ready to attach to a GitHub Release.

Everything is driven by file *shape* (fork contents, git attributes) —
no per-project file lists — so the same `tools/mac-forks/` directory
drops into any project unchanged.

## Requirements

- macOS with the Xcode Command Line Tools (`xcode-select --install`) —
  provides `binhex`, `DeRez`, `Rez`, `SetFile`.
- For the emulator/disk-image tooling only: `hfsutils`
  (`brew install hfsutils`) and, for Snow's SCSI-style images,
  [djjr](https://diskjockey.onegeekarmy.eu/).

Each script checks for exactly the tools it needs and says what's missing.

## Quick start: a new repo from an existing disk image

The most common starting point: your project already lives in a folder on
an HFS disk image (an emulator hard disk, a BlueSCSI card image, a floppy
dump). Do **not** copy the files out by hand with `hcopy` — its text mode
rewrites line endings *and* remaps Mac Roman through Latin-1, silently
corrupting every non-ASCII character. `pull-from-disk.sh` copies
everything raw and fork-intact.

```sh
mkdir my-project && cd my-project
git init
git commit --allow-empty -m "Initial commit"       # subtree add needs a commit to attach to

git remote add mac-forks https://github.com/crufi/mac-forks.git
git subtree add --prefix=tools/mac-forks mac-forks main --squash
sh tools/mac-forks/install.sh                      # hooks, filters, starter .gitattributes/Makefile

# copy the project off the disk image, fork-intact and byte-exact:
sh tools/mac-forks/pull-from-disk.sh /path/to/disk.hda "My Folder:My Project ƒ"

git add .
git commit -m "Add project source"
```

That's a working repo: fork-bearing files are tracked as `.hqx`/`.r`
sidecars (the real files regenerate on every clone/checkout), text files
are stored as clean UTF-8/LF and materialize as Mac Roman/CR. Continue
with [the full checklist](#setting-up-a-new-project) below for
`.gitattributes` tuning, GitHub setup, and the emulator workflow.

Starting from files already sitting on your modern Mac instead? Same
sequence, minus the `pull-from-disk.sh` line.

## How it works: resource forks

Two scripts, driven entirely by file shape:

- **`export.sh`** scans the working tree for files with a non-empty
  resource fork and encodes each one to a plain-text sidecar:
  - a **`*.rsrc`-named file (case-insensitive) with an empty data fork**
    is decompiled with `DeRez` to `<name>.r`. DeRez only knows about the
    resources themselves — not the file's own Finder type/creator — so
    that's captured as a human-readable leading comment, e.g.
    `/* mac-forks: type=rsrc creator=RSED */` (Rez ignores C-style
    comments, so recompilation is unaffected).
  - **anything else with a resource fork** is archived with
    `binhex encode` to `<name>.hqx` — self-contained (data fork +
    resource fork + type/creator round-trip in one step). BinHex is
    preferred over the more space-efficient MacBinary because it's
    7-bit-clean text, which keeps GitHub's file view usable.

  The real, fork-bearing files are gitignored (`export.sh` maintains a
  generated block in `.gitignore` automatically) and never tracked
  directly.

- **`import.sh`** does the reverse: finds `.hqx`/`.r` sidecars and
  reconstitutes the real files, restoring resource fork and Finder
  type/creator exactly.

Both are wired up as git hooks by `install.sh`:

| Hook | Runs | Why |
|---|---|---|
| `pre-commit` | `export.sh` | keeps tracked sidecars in sync with the real files before every commit |
| `post-checkout` | `import.sh` | rebuilds real files after `checkout`/`clone`/`switch` |
| `post-merge` | `import.sh` | rebuilds real files after `merge`/`pull` (a fetch+merge doesn't go through checkout) |

Because the real files are gitignored rather than tracked, `import.sh`
writes them directly — there's no tracked path for git's own checkout
machinery to race against.

## How it works: classic Mac text

Classic Mac text has two properties that break modern tools:

- Lines end in a bare CR (0x0D). Git's line-ending machinery
  (`core.autocrlf`, `text=auto`, `eol=`) only understands LF and CRLF, so
  it can't help — a diff, or GitHub's blob view, renders the file as one
  giant line.
- The encoding is Mac Roman, not UTF-8. Anything above ASCII is a
  *different character* when read as UTF-8 or Latin-1: byte `0xD4` is a
  left curly quote in Mac Roman but renders as `Ô` in GitHub's viewer.

The **`mactext`** filter pair fixes both together (git allows only one
`filter=` per path): `clean` converts Mac Roman → UTF-8 and CR → LF, so
the stored blob is ordinary, correctly-rendering, diffable text; `smudge`
reverses both, so the working copy is a genuine Mac Roman, CR-only file.
It's a plain stdin/stdout transform (`iconv` + `tr`) wired as an ordinary
git filter — no forks or hooks involved.

`mactext-clean` also captures the file's Finder type/creator, when
present, as a leading comment:

```
/* auto-generated (do not modify): type=TEXT creator=KAHL hex=544558544B41484C... */
```

These files only carry Finder info when they came off a real HFS volume
(most modern editors don't preserve xattrs) — so the value captured at
first check-in is carried forward on later commits rather than silently
dropped, and `import.sh` restores it to the working file on checkout. (A
smudge filter can't do that restore itself: its stdout *is* the file
content git writes, so it can't safely side-effect the same path.)

You declare which extensions get the filter in your project's
`.gitattributes`:

```
*.c filter=mactext -text
*.h filter=mactext -text
```

### `macroman` — the encoding half alone, for `.r` sidecars

DeRez output embeds raw resource-fork bytes in its hex-dump comments, and
for text-bearing resources (`STR#`, `vers`, …) those bytes are genuine
Mac Roman prose that renders wrong on GitHub. But DeRez emits LF line
endings — there's no CR convention to preserve, and adding CR↔LF
conversion would corrupt the sidecar. So `.r` sidecars get a second
filter, **`macroman`**: exactly `mactext` minus the line-ending step.

```
*.r filter=macroman -text
```

## The emulator workflow

With `snow.mk` included in your Makefile (see the
[checklist](#9-optional-build--launch-in-an-emulator)), the day-to-day
loop is:

```sh
make run     # build the disk image, attach it, launch Snow
             # ... edit/build inside the emulator's IDE ...
make pull    # bring in-emulator edits back into the working tree
git diff     # review them like any other local change
```

- **`make` / `make all`** — builds `build/<project>.img` (plain HFS) and
  `build/<project>.hda` (SCSI-style device image for Snow).
- **`make run`** — builds, attaches the image to a copy of your Snow
  workspace, launches Snow.
- **`make pull`** — copies edits back off the *existing* `.hda` into the
  working tree (deliberately without rebuilding first, which would
  destroy the very edits it's rescuing).
- **`make clean`** — removes the build directory and generated workspace.
- **`make release`** — versioned zip of the disk image(s), in `dist/`.

### The overwrite guard

Rebuilding or `make clean`-ing the `.hda` destroys whatever's on it —
which is a problem if it holds in-emulator edits you haven't pulled.
`guard-overwrite.sh` gates every destructive step with two checks:

- **Whole-image**: is the `.hda` newer than the `.img` it was converted
  from (beyond normal build sequencing)?
- **Per-file**: does any file's HFS catalog date postdate that build?
  This also catches files that exist on the disk with *no* tracked
  counterpart — created inside the emulator, never pulled — and lists
  them separately, in green.

If anything trips, the guard prints exactly what it found and demands a
typed confirmation — `BORK N` (N = number of flagged files) or
`BORK DISK` — before proceeding. Anything else aborts with the `.hda`
untouched. `make pull` first if you want to keep the changes. Set
`FORCE=1` (e.g. `make run FORCE=1`) to skip the check for
non-interactive use.

### Project naming

Build artifacts take the project's name: `PROJECT` defaults to the repo
directory's name (`my-project` → `build/my-project.img`,
`my-project.hda`, `my-project.snoww`, and the release zip). It must stay
a filesystem-safe slug — Make splits prerequisite lists on whitespace, so
a target with a space in its name can't work. The pretty, human name goes
in `VOLUME_LABEL` — the HFS volume name shown inside the emulator, where
spaces and apostrophes are fine:

```makefile
VOLUME_LABEL := Bob's Big Project
```

Left unset, the volume is named after the `PROJECT` slug. `SNOW_PATH`,
`VOLUME_BLOCKS`, and `BUILD_DIR` also have sensible defaults (see
`snow.mk`/`image.mk`) and can be overridden before the `include` line.

### The pieces, individually

Each tool also works standalone, outside the Makefile:

**`build-floppy.sh out.img [blocks] [label] [text_creator]`** — builds a
plain HFS image (no partition map) containing every file mac-forks tracks
for the project. Text files are copied raw — `hcopy`'s own text mode
would remap Mac Roman through Latin-1 and corrupt non-ASCII characters —
then stamped `TEXT` plus your toolchain's creator code (e.g. `KAHL`, so
sources double-click-open in THINK C). Fork-bearing files are bridged
through MacBinary, since `hcopy` cannot read a macOS file's resource
fork directly. Works for any emulator that mounts HFS images.

**`pull-from-disk.sh <disk-image> [hfs-start-folder]`** — the reverse:
copies files off a disk image (plain `.img` or SCSI-style `.hda`) into
the working tree, raw and fork-intact. Tracked files update in place; new
files (created on the disk, never tracked) are rescued recursively too,
with Finder-junk folders (`Trash`, `Desktop Folder`, …) skipped.
`hfs-start-folder` (e.g. `"Dev:My Project ƒ"` — HFS paths are
colon-separated) scopes everything to that folder and drops its name from
the resulting local paths; this is the bootstrap-a-repo-from-a-disk mode
shown in the [Quick start](#quick-start-a-new-repo-from-an-existing-disk-image).
A nonexistent folder fails loudly. Finishes by running `export.sh`, so
sidecars immediately reflect what came back and the next `make` knows to
rebuild.

**`snow-attach-disk.py <template.snoww> <disk.hda> <output.snoww>`** —
copies a Snow workspace, adding the disk image in the first empty SCSI
slot. Only that one entry is touched: the template's other entries (ROM,
PRAM, existing disks) are typically bare relative filenames that Snow
resolves relative to the workspace file's own location, so the output
must land in the same directory as the template — `snow.mk` writes it to
`SNOW_PATH` for exactly that reason. The `.hda` must be a SCSI-style
device image; `snow.mk` produces one from the plain HFS image with
`djjr convert to-device`.

**`release.mk`** — `make release VERSION=v1.2.0` zips the source disk
image into `dist/` (plus the `.hda`, when `snow.mk` is also included).
`VERSION` defaults to `git describe`. This packages *source*, not a
compiled binary — the actual build happens by hand inside the vintage
IDE. Tagging is deliberately left to plain git: declaring a release
mutates shared repo state, which a `make` target shouldn't do as a side
effect.

## Setting up a new project

mac-forks is vendored via `git subtree` at the fixed path
`tools/mac-forks/` (the scripts assume that path).

### 1. Init the repo

```sh
git init
git commit --allow-empty -m "Initial commit"
```

The empty commit matters: `git subtree add` (next step) attaches onto an
existing commit, and a bare `git init` has none — without it the add
fails with `fatal: ambiguous argument 'HEAD'`. If you already have
project files in the directory, committing those works just as well.

### 2. Create the GitHub repo, pull in mac-forks

```sh
gh repo create my-project --public --source=. --remote=origin   # or --private
git remote add mac-forks https://github.com/crufi/mac-forks.git
gh repo set-default origin
git subtree add --prefix=tools/mac-forks mac-forks main --squash
sh tools/mac-forks/install.sh
```

`gh repo set-default origin` matters as soon as the second remote
exists — without it, `gh` commands like `gh release create` fail with
`No default remote repository has been set`.

`install.sh` checks for required tools, symlinks the hooks, configures
the `mactext`/`macroman` filters, and creates a starter
`.gitattributes`/`Makefile` from `tools/mac-forks/templates/` if you
don't already have them (it never overwrites existing files). **Every
clone needs to run it once** — hooks and filter config live in `.git/`,
which `git clone` never populates.

### 3. (If bootstrapping from a disk image) pull the project off it

```sh
sh tools/mac-forks/pull-from-disk.sh /path/to/disk.hda "Folder:Project ƒ"
```

See the [Quick start](#quick-start-a-new-repo-from-an-existing-disk-image)
for why this — and not manual `hcopy` — is the way to get files off an
HFS volume intact.

### 4. Edit `.gitattributes`

Step 2 created a starter one. Tune the extension list for your project:
`*.r` gets `macroman` (mac-forks' own generated sidecars, always
LF-native); your actual vintage source gets `mactext`:

```
*.hqx -text
*.r filter=macroman -text

*.c filter=mactext -text
*.h filter=mactext -text
*.cp filter=mactext -text
*.cpp filter=mactext -text
*.hpp filter=mactext -text
```

Add more `filter=mactext -text` lines for whatever else your project
has — `.p`/`.pas` (Pascal), `.a`/`.asm`, etc.

⚠️ **Naming collision:** mac-forks generates `.r` sidecars for
resource-only files (`Foo.rsrc` → `Foo.rsrc.r`). If your project has
genuine hand-written Rez source ending in `.r`, **rename it** (`.rez` or
similar) before using mac-forks.

### 5. Normal `.gitignore` stuff

`export.sh` maintains its own generated block automatically — leave that
alone. You'll still want the usual:

```
.DS_Store
```

### 6. Add your files, commit

Edit everything normally — including the real `.π`/`.rsrc` files directly
in ResEdit or the IDE. The `pre-commit` hook encodes anything with a
resource fork automatically; you never `git add` those files yourself.

```sh
git add .
git commit -m "Add project source"
```

### 7. Verify with a genuinely fresh clone

Problems in this kind of setup mostly only show up on a fresh clone — an
already-configured working copy hides plenty:

```sh
cd /tmp && git clone /path/to/your/repo verify-me && cd verify-me
sh tools/mac-forks/install.sh
# diff verify-me's files against your real working copy
```

### 8. Push

```sh
git push -u origin main
```

### 9. (Optional) Build + launch in an emulator

Step 2 created a starter `Makefile`; point it at your setup:

```makefile
SNOW_WORKSPACE ?= $(HOME)/Snow/your-workspace.snoww   # your own Snow workspace
TEXT_CREATOR   := KAHL                                 # or whatever your vintage toolchain expects
VOLUME_LABEL   := My Project                           # HFS volume name shown in the emulator

include tools/mac-forks/snow.mk
include tools/mac-forks/release.mk
```

Then `make run`. See [The emulator workflow](#the-emulator-workflow).

### 10. (Optional) Cutting a release

```sh
git tag -a v1.0.0 -m "First public release"
git push origin v1.0.0
make release VERSION=v1.0.0
gh release create v1.0.0 dist/*-v1.0.0.zip \
  --title "v1.0.0" --notes "..."
```

## Keeping mac-forks up to date

Pulling in later mac-forks improvements:

```sh
git subtree pull --prefix=tools/mac-forks mac-forks main --squash -m "Pull mac-forks updates"
```

Pushing a change you made in-place back upstream:

```sh
git subtree push --prefix=tools/mac-forks mac-forks main
```

## Known limitations

- `DeRez`/`Rez` don't round-trip byte-for-byte — Rez recompiles a
  semantically equivalent resource fork, not necessarily identical bytes.
  Fine for ResEdit/an IDE/a linker; don't expect `cmp` to agree.
- The `.r` path only ever produces an empty data fork on import (that's
  the only case it's used for). A file with both real data-fork content
  *and* a resource fork goes through the BinHex path, which is fully
  fork-agnostic.
- Hooks are local to each clone — **every clone runs `install.sh` once**.
  Until then the repo is still valid; fork-bearing files just stay as
  their `.hqx`/`.r` sidecars, unexpanded.
- `mactext` assumes the only control characters in the text are line
  breaks.
- Mac Roman can't represent all of Unicode: if a modern editor introduces
  a character outside its repertoire (an emoji, say), the working-tree
  conversion on the next checkout fails with an explicit `iconv` error
  rather than guessing.
- Filenames containing *consecutive* spaces aren't matched reliably by
  the disk-image tooling (single spaces are fine).

## License

[MIT](LICENSE)
