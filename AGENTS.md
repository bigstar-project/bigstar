# Repository Working Rules

## Progress Tracking

For this fork, keep the NSMB Mario vs Luigi online PoC progress updated in `docs/`.

When work starts, changes direction, completes a meaningful step, or hits a blocker, update the relevant Markdown file before ending the turn. The main tracking document is:

- `docs/nsmb-mario-vs-luigi-online-poc.md`

When reading Japanese text in PowerShell, it may sometimes appear garbled, but this is due to the character encoding; the content itself is normal.

The tracking document should show:

- completed work
- current blockers
- next actions
- verification status
- requirements from the user, such as ROM or tool installation needs

Do not leave implementation progress only in chat.

Before the final response of any turn that changes implementation status, review the tracking document for stale or contradictory content. Compact or update old sections instead of only appending new notes. In particular:

- remove or rewrite "next actions" that are already completed
- move obsolete blockers into completed/resolved notes or remove them
- keep the current blocker and next action easy to find near the top
- avoid long chronological append-only logs when a concise current-state summary is clearer
