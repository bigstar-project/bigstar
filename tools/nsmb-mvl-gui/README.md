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

Run the React UI in a browser without Tauri or sidecars:

```powershell
corepack pnpm preview:browser
```

Open `http://127.0.0.1:1420/?preview=ready` to inspect the main launcher UI
with onboarding completed. Open `http://127.0.0.1:1420/` to inspect the
first-run onboarding flow.

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

## Reusable ROM setup

The launcher generates patched host/client ROMs only when they are missing or
when the reusable ROM format changes. Match settings such as course stage, Big
Star target, and lives are applied by melonDS at launch time, so changing them
does not regenerate ROM files. The `共通ROM再準備` button is an explicit recovery
action for replacing the cached pair.

The current reusable ROM marker is `nsmb-mvl-reusable-runtime-config-v3`.

## act

The full Windows Tauri bundle needs a Windows GitHub runner. For local Docker
smoke checks with `act`, run the Linux frontend-only workflow:

```powershell
act workflow_dispatch -W .github/workflows/nsmb-mvl-gui-local.yml -j gui-check
```
