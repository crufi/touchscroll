# util

Shared classic Mac OS utility library for my THINK C / Symantec C++
projects: error handling and reporting (`CrutchError`), general Toolbox
helpers (`CrutchUtilities`), preferences storage (`CrutchSettings`), and
structured exception handling (`Exceptions`).

The per-project `TinyCrutch*` variants are deliberately **not** here —
those are hand-minimized per project and diverge on purpose.

## How it's stored

This repo holds the sources as ordinary modern text — UTF-8, LF line
endings — which is exactly the blob format
[mac-forks](https://github.com/crufi/mac-forks) uses for vintage text.
There's no tooling vendored here and none needed: a consuming project's
own mac-forks filters convert these files to genuine Mac Roman/CR
working copies on checkout (and its disk-image build puts them in a real
`util` folder on the volume). Standalone, the repo is safe to read and
edit in any modern editor.

The leading `/* auto-generated ... */` comment on each file carries its
Finder type/creator; the consuming project's tooling restores that on
checkout. Leave it alone.

## Using it in a project

Vendored via `git subtree` at the prefix `util/`:

```sh
git remote add util https://github.com/crufi/think-c-util.git
git subtree add --prefix=util util main --squash
```

Pulling later improvements:

```sh
git subtree pull --prefix=util util main --squash -m "Pull util updates"
```

Pushing a change made in-place inside a project back upstream:

```sh
git subtree push --prefix=util util main
```

After adding it, re-point the THINK C project's source file entries at
the `util` folder if the IDE asks.

## License

[MIT](LICENSE)
