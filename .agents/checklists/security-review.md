# Security Review

- [ ] Identify assets, actors, trust boundaries, authority, and denial-of-service limits.
- [ ] Validate all lengths, addresses, conversions, rights, and state transitions.
- [ ] Review unsafe code/assembly/MMIO safety contracts.
- [ ] Preserve W^X, NX, user/supervisor, guard, null-page, and least-authority rules.
- [ ] Add adversarial, malformed-input, exhaustion, stale-handle, and teardown tests.
- [ ] Check logs/errors for addresses, secrets, user data, and unbounded output.
- [ ] Record residual risk; do not claim capability security before enforcement exists.
