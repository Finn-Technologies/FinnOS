# FinnOS Delivery Project

Create one organization/repository Project V2 after obtaining Projects permission.

## Fields

| Field | Type | Values |
|---|---|---|
| Status | Single select | Triage, Ready, In progress, In review, Blocked, Done |
| Priority | Single select | P0, P1, P2, P3, P4 |
| Subsystem | Single select | Boot, Kernel, IPC, Userspace, Drivers, Storage, Network, Graphics, UI, Tooling, Docs, Security, Release |
| Architecture | Single select | All, x86-64, ARM64 |
| Complexity | Single select | XS, S, M, L, XL |
| Maturity level | Single select | L0 through L6 |
| Milestone | Built-in | Repository milestone |
| Blocked by | Text | Issue links until dependency fields are available |

## Views

- Critical path: open P0/P1, grouped by milestone, dependency order.
- Current milestone: current milestone only, board by Status.
- x86-64 and ARM64: architecture-filtered tables.
- UI/UX: Graphics/UI subsystem, roadmap order.
- Bugs and security: type filters, sorted by priority.
- Documentation: docs issues and documentation requirements from active engineering issues.
- Untriaged: missing priority, subsystem, architecture, complexity, or milestone.

Work-in-progress policy: one implementation issue per contributor unless an issue is blocked. “Done” requires code, tests, documentation, and acceptance evidence; merged code alone is insufficient.
