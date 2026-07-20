# Agent Infrastructure Scripts

All scripts use Python 3 standard library and default to read-only behavior.

```bash
python3 .agents/scripts/validate.py --all       # complete integrity gate
python3 .agents/scripts/render_skills.py --check
python3 .agents/scripts/check_links.py
python3 .agents/scripts/validate_yaml.py
python3 .agents/scripts/capture_state.py
python3 .agents/scripts/new_handoff.py
```

`render_skills.py` regenerates registered `skills/*/SKILL.md` and `MANIFEST.md` from `skill_registry.py`. It refuses to prune unregistered directories or write through skill-directory symlinks. Review its complete diff. `new_handoff.py` prints by default and writes only a new `.md` filename under `.agents/handoffs/` when `--output` is supplied; it refuses overwrites and path traversal. `capture_state.py` runs diagnostic commands but does not install, build, clean, or edit.

`validate_yaml.py` validates the constrained FinnOS planning/template schema, indentation, required keys, and obvious credential fields. It deliberately is not a general YAML parser; GitHub remains the final parser for workflow/template semantics.
