# Agent Handoffs

Store durable handoffs here when work spans sessions, remains blocked, or another agent must continue from repository evidence. Use a descriptive, dated filename:

```bash
python3 .agents/scripts/new_handoff.py --output r1-build-orchestration-2026-07-16.md
```

The generator refuses absolute/nested paths and existing files. Review and complete every field before committing a handoff. For a completed small task whose final report already contains every required field, stdout output may be included directly in that report instead of creating a permanent file.
