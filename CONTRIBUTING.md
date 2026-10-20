# Contributing to FinnOS

FinnOS is a small operating system, and the tree is small on purpose. The
bar for a change is that it fits the shape of what is already there.

Read [AGENTS.md](AGENTS.md) first: it is the working agreement, and it
explains the reasoning behind the rules rather than only stating them.

## The short version

1. `make deploy` builds and runs it on the dev VM.
2. `vm/shot.sh out.png` shows you what you did.
3. `make test` runs the tests.
4. One change per commit, and the message says why.

## The dev VM

The compositor needs DRM, GBM, EGL, FreeType and the Fixel fonts. The Mac
is an editor; the Arch VM in Parallels is the build machine. `vm/README.md`
has the details, including the address and the credentials.

```sh
make deploy                       # build, install, restart
make deploy -- --no-restart       # build and install only
vm/shot.sh /tmp/shot.png          # a capture of the frame
```

`make deploy` runs `vm/deploy.sh`, which sends the tree over as a tar
stream, builds it in `~/finnos` and restarts `finn-comp` and `finnd`.

## Style

The C follows the style of the sources that are already there: four spaces,
braces on the same line, `_` in names, comments that explain the reason.

Two things to keep:

- **Say why.** A comment that repeats the code is noise. The existing
  sources explain *why* a number is 20.8 rather than 20, and that is the
  bar.
- **Say what is missing.** If a feature is a stub, the stub says so, and
  so does the documentation.

## Naming

`NAMING.md` is the rule and it is short. In short: binaries are lowercase
and hyphenated, apps are the brand and a plain English noun (**Finn
Code**), and a file type is the language and not the brand (`.fui`).

## Pull requests

- Say what changed and what you saw when you ran it.
- Say what you did not manage to do, if anything.
- Update `docs/` and `CHANGELOG.md` when the behaviour changed.

## Licence

MIT. See [LICENSE](LICENSE).
