# Repository Working Rules

## Package Manager

Use pnpm for Node.js projects unless instructed otherwise. In the Codex environment, a bundled pnpm can appear earlier on `PATH` than the project-managed pnpm, so run pnpm commands in this repository through Corepack, for example `corepack pnpm ...`.

## Git Operations

Do not push automatically. Only run `git push` when the user explicitly asks for a push in the current conversation.

Local commits are allowed when they are useful for preserving completed work, but pushing to remotes requires an explicit user request each time.

## Progress Tracking

For this fork, keep ROM analysis, ROM patching, and melonDS/input sync PoC progress updated in `docs/`.

When ROM analysis, ROM patching, or melonDS/input sync PoC work starts, changes direction, completes a meaningful step, or hits a blocker, update the relevant Markdown file before ending the turn. For GUI, WebRTC/WAN transport, sidecar, backend, matchmaking, ranking, and other non-ROM-analysis work, do not update Markdown progress documents. Do not assume there is only one tracking document. Use the document that matches the current work:

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

Before the final response of any turn that requires a Markdown update under the rules above, review the relevant tracking document for stale or contradictory content. Compact or update old sections instead of only appending new notes. In particular:

- remove or rewrite "next actions" that are already completed
- move obsolete blockers into completed/resolved notes or remove them
- keep the current blocker and next action easy to find near the top
- avoid long chronological append-only logs when a concise current-state summary is clearer

## Tauri GUI UI Components

When adding UI to the Tauri GUI in `tools/bigstar`, reuse the existing Kiso UI setup (components in `src/components/ui`, recipes in `src/theme/recipes`) before creating a new local component from scratch. The installed components are listed in `tools/bigstar/.kiso/installed.json`.

If a new UI component is needed, first check whether Kiso UI (https://github.com/uniunitaro/kiso-ui) provides it. Kiso has no published package; run its CLI from a local checkout of the Kiso repository and point `--target` at the GUI package:

```powershell
cd <kiso-ui checkout>
corepack pnpm ui list
corepack pnpm ui add <component-name> --target <path to tools/bigstar> --dry-run
corepack pnpm ui add <component-name> --target <path to tools/bigstar>
```

Use the component name shown by `ui list`, for example `dialog`, `tabs`, `select`, `toast`, or `collapsible`. The CLI does not install dependencies, so add any package the new component imports to `tools/bigstar` with `corepack pnpm add`. After adding components, review the generated files under `src/components/ui` and `src/theme/recipes`, and keep `panda.config.ts` with the default `--panda-config=keep`.

Do not change the Kiso repository for app-specific needs. Put app-specific variants or styles in the app's copies under `src/theme/recipes` or in the screen code; `ui diff` shows how the app's copies differ from Kiso, and `ui update` keeps files the app has changed.

## Code Quality Checks

When changing Rust code, run Rust formatting and Clippy before ending the turn. Use `cargo fmt` for the affected crate/workspace, then run the local strict Clippy alias, normally `cargo clippy-all`, which treats warnings as errors.

When changing TypeScript code, run Biome and typecheck before ending the turn. Use the package's existing scripts through Corepack, such as `corepack pnpm biome check`/`corepack pnpm biome format` and `corepack pnpm typecheck`, or the repo-local equivalents if the package defines different script names.

After changing a pnpm-managed package, always run that package's `corepack pnpm run ci` before ending the turn. If the package has no `ci` script or the command cannot be run, state the reason clearly in the final response.
