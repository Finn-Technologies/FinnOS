# AGENTS.md

How to work in this tree. It is written for an agent as much as for a
person: everything here is a rule, not a suggestion, and the reason is
given so that you can tell when the rule does not apply.

## Before you change anything

1. **Read `NAMING.md`.** It is the naming rule for the whole family, and
   it is short. A new binary, module, app or file type has to fit it.
2. **Read `docs/architecture.md`.** It says what exists and, just as
   importantly, what does not.
3. **Look at the code you are about to change.** The sources carry their
   reasoning in the comments. The style is deliberate: a comment explains
   *why*, never *what*, and the "what" is already in the code.

## The rules

### Never fake a capability

This is the rule the rest of the system is built on, and it is the one
most easily broken by accident.

- A field in the system state has an absent value (`-1`), never a
  plausible default. A machine with no battery reports `-1` and the shell
  says "mains".
- A control that cannot act must not look like it can. A tile for an app
  that is not installed is drawn as what it is, and says so.
- If a feature does not exist, say that it does not exist — in the code,
  in the documentation and in the reply to whoever asked. The
  documentation is part of the product: `docs/architecture.md` has a
  section for what is missing, and it must stay true.

### One copy of a constant

A path, a socket name or a magic number that two files need belongs in
one header, and that header is in `src/core`. If you find yourself
writing the same literal in two places, one of them is wrong.

### The core knows nothing about the rest

`src/core` is data and plumbing: the protocol, the socket helpers, the
state reader, the app registry. It may not grow a dependency on the
compositor or on the control plane, and neither of them may depend on the
other. The one thing that crosses the line is the protocol, and the
protocol is one header.

### The control plane is the only door

Anything that starts a program, changes the machine's power, or moves the
pointer goes through `finnd`. The compositor does not spawn programs and
does not power the machine off by itself: it asks the control plane, so
that there is one audit trail and one place where the authority is.

Do not add a TCP listener. Do not add a second way in.

### No AI in the code, and no model in the image

Nothing in the OS may talk to a model, ship weights, or require a network
call to a model service. FinnAI is opt-in and off by default. This is a
product decision, not a technical one, and it is not a problem to solve.

### Naming

- A binary is lowercase and hyphenated: `finn-comp`, `finnpkg`. A binary
  name is never shown to a user.
- A platform name is one word, CamelCase: `FinnOS`, `FinnUI`, `FinnSDK`.
- An app is the brand and a plain English noun, written with a space and
  Title Case: **Finn Code**, **Finn Files**. Never an invented word.
- A system surface inside the OS is named for what it renders: `finndock`,
  `finncontrol`, `finnclock`, `finnmenu`.
- A file type is the language, not the brand: `.fui` in, `.fbin` out.

### The protocol

The control protocol is line-oriented text, and it stays that way. JSON
belongs at the MCP boundary, in the adapter, on the host. If you need to
add a verb:

- add it to the daemon that owns it — `finnd` for state, power and apps;
  `finn-comp` for display and input;
- proxy it in `finnd` if it belongs to the compositor;
- document it in `docs/control-protocol.md`;
- add it to `finnctl`'s usage text.

A new verb that changes the machine is not the same as one that reports
on it. `display.get` reports; `display.set` changes. Do not make a
reporting verb change anything, and do not make a changing verb pretend
to only report.

## The dev loop

The build machine is the VM, not the host.

```sh
make deploy                 # build, install, restart
make deploy -- --no-restart
vm/shot.sh /tmp/shot.png    # look at it
make test                   # the tests
```

`vm/deploy.sh` sends the tree over as a tar stream, builds it in `~/finnos`
and restarts the two services. The VM has no `rsync`.

Before you claim that a change works, look at it: capture the frame and
read the image. A compositor change that compiles is not a compositor
change that works.

## The tests

`make test` runs the suite in `tests/`. It covers the parts that can be
tested without a display: the registry, the protocol, the compiler and the
SDK client. The compositor is verified by looking at it.

If you add a behaviour that can be tested without a display, test it. If
you cannot, say so in the pull request rather than leaving it implicit.

## Committing

- One change per commit, and the message says *why* in the body.
- Do not commit build output. `.gitignore` covers it; if something gets
  through, remove it from the index.
- Do not commit captures. `docs/images/` holds the ones that belong in the
  documentation, and they are resized before they are added.
- Update `CHANGELOG.md` when the user-visible behaviour changes.

## What to be careful about

- **The GPU path and the software path must agree.** A change to a
  surface usually has to be made in both: the software path composites
  layers with `finn_blit`, the accelerated path pastes them as quads.
- **Cached layers have a build key.** A surface that redraws is keyed on
  everything that can put a different pixel in it. If you add a property
  to a surface, add it to its key, or it will not redraw when it should.
- **The shell is one family.** The dock, the Control Center, the clock
  page and the Start Menu share their glass, their corner radii, their
  type scale and their motion. A surface that invents its own looks like a
  different operating system.
