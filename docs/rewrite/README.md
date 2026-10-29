# The rewrite

The C userland is preserved on the `pre-rewrite` tag and on the
`legacy/browser-session` branch. This branch is the new system.

Read in this order:

1. [contract.md](contract.md) — the five programs, authority, the app
   contract, the sandbox rules, what v1 does not do.
2. [milestones.md](milestones.md) — what each step must boot and pass.
3. [cpp-guide.md](cpp-guide.md) — the small C++ subset, ownership rules, and
   hands-on tasks for a first contribution.

## Status

- M0: in progress. The contract exists, the legacy work is preserved, and
  `src/session/app_registry.cpp` is the first new module.
- The old `src/compositor`, `src/control`, `src/core`, `src/pkg`, `apps/`,
  `sdk/`, `mcp/` and the C tests are still present on this branch. They are
  removed milestone by milestone as their replacements pass acceptance, not
  before. Deleting them first would leave nothing to build and nothing to
  compare against.
