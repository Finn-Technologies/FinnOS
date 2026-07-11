# Coding style

> Status: Current conventions
> Implementation: Formatting and lint checks run in CI

Use rustfmt, treat Clippy warnings as errors in CI, choose descriptive names, document public APIs, handle errors explicitly, avoid unexplained `unwrap()` in production paths, and keep modules focused. Do not add undocumented unsafe code. Prefer architecture-neutral code. Use descriptive Markdown headings and relative links, and never make false implementation claims.
