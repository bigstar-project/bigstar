# Repository Working Rules

## Git Operations

Do not push automatically. Only run `git push` when the user explicitly asks for a push in the current conversation.

Local commits are allowed when they are useful for preserving completed work, but pushing to remotes requires an explicit user request each time.

## Progress Tracking

For this fork, keep the NSMB Mario vs Luigi online progress updated in `docs/`.

When work starts, changes direction, completes a meaningful step, or hits a blocker, update the relevant Markdown file before ending the turn. Do not assume there is only one tracking document. Use the document that matches the current work:

- `docs/nsmb-mario-vs-luigi-online-poc.md`: melonDS/ROM patch/input sync PoC status and verification.
- `docs/nsmb-wan-netplay-roadmap.md`: WAN transport, WebRTC sidecar, desktop GUI, backend, matchmaking, and ranking roadmap.
- `docs/nsmb-mvl-rollback-design-notes.md`: rollback design notes kept for later reference.

When reading Japanese Markdown or other Japanese UTF-8 text in PowerShell, do not use plain `Get-Content` or `-Encoding Default`; they can mojibake in this environment. Use one of these instead:

- Full or ranged reads: `Get-Content -LiteralPath <path> -Encoding UTF8`
- Searches and line-numbered reads: `rg -n "<pattern>" <path>`

If Japanese text still appears garbled, treat it as an output decoding issue first and retry with explicit UTF-8 before assuming the file content is corrupt.

The relevant tracking document should show:

- completed work
- current blockers
- next actions
- verification status
- requirements from the user, such as ROM or tool installation needs

Do not leave implementation progress only in chat.

Before the final response of any turn that changes implementation status or project direction, review the relevant tracking document for stale or contradictory content. Compact or update old sections instead of only appending new notes. In particular:

- remove or rewrite "next actions" that are already completed
- move obsolete blockers into completed/resolved notes or remove them
- keep the current blocker and next action easy to find near the top
- avoid long chronological append-only logs when a concise current-state summary is clearer

## Code Quality Checks

When changing Rust code, run Rust formatting and Clippy before ending the turn. Use `cargo fmt` for the affected crate/workspace, then run the local strict Clippy alias, normally `cargo clippy-all`, which treats warnings as errors.

When changing TypeScript code, run Biome and typecheck before ending the turn. Use the package's existing scripts, such as `pnpm biome check`/`pnpm biome format` and `pnpm typecheck`, or the repo-local equivalents if the package defines different script names.
