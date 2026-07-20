# Agent-System Governance

## Ownership and Review

No subsystem owners are assigned in `MAINTAINERS.md`; do not fabricate them in skill metadata. Any contributor or agent may propose skill changes. Changes that alter engineering policy, security gates, architecture requirements, completion evidence, or release criteria require the same review level as the affected code/ADR and must be called out explicitly.

## Source and Generation

- `.agents/scripts/skill_registry.py` is the machine-readable skill catalog and content source.
- `.agents/scripts/render_skills.py` renders `skills/*/SKILL.md` and `MANIFEST.md` deterministically.
- Generated skill files are reviewable repository content. Do not hand-edit them; update the registry and regenerate.
- Root policies, templates, checklists, scripts, and tests are maintained directly.

Schema fields include `name`, `title`, `category`, `version`, `status`, `owners`, `triggers`, `prerequisites`, `conditional_skills`, `implementation_gates`, `related_docs`, `related_milestones`, and `last_verified` (`base_commit`, `date`, `worktree_dirty`, `context`). Names are unique lowercase hyphenated identifiers matching directory names. `owners` remains empty until repository governance assigns ownership. Milestones must come from `ROADMAP.md` or be explicitly marked `post-1.0`. Skill prerequisites are documents/workflows an agent must load; implementation gates are product dependencies; conditional skills are loaded only for the named slice.

## Synchronization Rules

Update a skill when code, commands, artifacts, architecture boundaries, test names, milestone gates, security assumptions, or support status change. A behavior change is incomplete until affected skills and authoritative docs agree. `STATE.md` is only a pointer summary and must not become an independent roadmap.

Staleness signals include missing referenced paths, obsolete commands, a `last_verified` commit predating a changed subsystem, contradictions with `STATUS.md`, planned features described in present tense, or validation failures. Run the validator in CI and during pre-commit review.

## Resolving Contradictions

Reproduce current behavior first. Source plus execution outranks prose; `STATUS.md` and `ROADMAP.md` outrank skill summaries. Correct all affected layers in one reviewable change. If behavior cannot be reproduced, classify it as unknown or implemented-but-unverified rather than selecting a preferred claim.

## Versioning and Change History

Increment a skill's integer `version` for material workflow, policy, gate, prerequisite, or contract changes. Typographical/link fixes may retain the version. Record user-visible agent-system changes in `CHANGELOG.md` under Unreleased. Never silently weaken a gate or rewrite policy through generated output.

## Deprecation

Deprecated skills use `status: deprecated`, name their replacement, and remain valid until all manifest dependencies migrate. Deletion requires proof that no skill depends on them and no active issue references them.
