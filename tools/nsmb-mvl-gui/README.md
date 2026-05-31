# NSMB Mario vs Luigi Online GUI

Tauri launcher for starting a two-peer NSMB Mario vs Luigi session with the
forked melonDS build and `nsmb-net-bridge`.

## Local commands

```powershell
corepack pnpm install
.\..\..\scripts\prepare-nsmb-mvl-tauri-sidecars.ps1
corepack pnpm typecheck
corepack pnpm vite:build
cd src-tauri
cargo check
```

Run the GUI during development:

```powershell
corepack pnpm dev
```

Build a bundle:

```powershell
.\..\..\scripts\prepare-nsmb-mvl-tauri-sidecars.ps1
corepack pnpm build
```

## Bundled binaries

The Tauri bundle includes sidecar copies of:

- `build/release-windows-x86_64/melonDS.exe`
- `tools/nsmb-net-bridge/target/release/nsmb-net-bridge.exe`

Use `scripts/prepare-nsmb-mvl-tauri-sidecars.ps1` after rebuilding either
binary. ROM files are not bundled.

## act

The full Windows Tauri bundle needs a Windows GitHub runner. For local Docker
smoke checks with `act`, run the Linux frontend-only workflow:

```powershell
act workflow_dispatch -W .github/workflows/nsmb-mvl-gui-local.yml -j gui-check
```
