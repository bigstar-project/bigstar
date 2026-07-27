# Bigstar GUI

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
corepack pnpm dev:insiders
corepack pnpm dev:public
```

`dev` is an alias of `dev:insiders`. Both commands use the same Rust target
cache, but apply the complete edition configuration before starting Tauri.
Insiders uses Vite port `1420`; Public uses `1421`. Their identifiers, updater
channels, signaling defaults, capabilities, and AppData directories remain
separate.

Run the React UI in a browser without Tauri or sidecars:

```powershell
corepack pnpm preview:browser
```

Open `http://127.0.0.1:1420/?preview=ready` to inspect the main launcher UI
with onboarding completed and sample match history when no preview history has
been stored. Open `http://127.0.0.1:1420/` to inspect the first-run onboarding
flow.

Build a bundle:

```powershell
.\..\..\scripts\prepare-nsmb-mvl-tauri-sidecars.ps1
corepack pnpm build:insiders
corepack pnpm build:public
```

Cargo dependencies stay in the shared `src-tauri/target` cache. After each
successful build, the runnable payload and only the installers produced by that
build are copied to `artifacts/<edition>/release`. The generated
`artifact-manifest.json` records the edition and files, so do not use the
mutable executable directly under `src-tauri/target/release` when comparing
editions.

## Capability configuration

Product differences are declared under `editions/`, while local/distribution
build differences are declared under `build-profiles/`. Vite resolves both
axes into one runtime capability set, so application code checks positive
capability names instead of edition names or negated build-profile conditions.

- `local` allows signaling-server configuration, developer tools when the
  edition permits them, and notifications for rooms hosted by the local player.
- `distribution` disables those development-oriented capabilities.

## Legacy updater bridge

Released `0.9.x` installations read updater metadata from GitHub's
`releases/latest/download/latest.json`. Insiders publishing therefore keeps the
mutable `legacy-latest` release marked as GitHub Latest and uploads the same
metadata used by `insiders-latest`. Public and versioned releases must remain
non-latest until the legacy bridge is retired.

The Insiders NSIS bundle includes
`src-tauri/windows/legacy-install-migration.nsh`. It installs Bigstar Insiders
first and only then runs the old per-user NSIS uninstaller with `/UPDATE`, which
preserves `%APPDATA%\dev.melonds.nsmb-mvl`. The application copies that data to
`%APPDATA%\Bigstar Insiders` on first launch without deleting the source. After
the old uninstaller succeeds, the hook removes only shortcuts, autostart, and
install-location registry entries that belong to the old product.

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
