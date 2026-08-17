# Bigstar GUI

Tauri launcher for starting a two-peer NSMB Mario vs Luigi session with the
forked melonDS build and `bigstar-net-bridge`.

## Local commands

```powershell
corepack pnpm install
.\..\..\scripts\prepare-bigstar-tauri-sidecars.ps1
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

Build a local bundle for development and manual verification:

```powershell
corepack pnpm build:local:insiders
corepack pnpm build:local:public
```

Build a distribution bundle:

```powershell
.\..\..\scripts\prepare-bigstar-tauri-sidecars.ps1
corepack pnpm build:distribution:insiders
corepack pnpm build:distribution:public
```

`build` and `build:local` are aliases of `build:local:insiders`.
`build:insiders` and `build:public` remain compatibility aliases for the
corresponding distribution builds.

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
- `distribution` keeps signaling-server configuration available only in
  Insiders, while disabling it in Public together with the other
  development-oriented capabilities.

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
- `tools/bigstar-net-bridge/target/release/bigstar-net-bridge.exe`

Use `scripts/prepare-bigstar-tauri-sidecars.ps1` after rebuilding either
binary. ROM files are not bundled.

## Reusable ROM setup

The launcher generates patched host/client ROMs only when they are missing or
when the reusable ROM format changes. Match settings such as course stage, Big
Star target, and lives are applied by melonDS at launch time, so changing them
does not regenerate ROM files. The `共通ROM再準備` button is an explicit recovery
action for replacing the cached pair.

The current reusable ROM marker is `bigstar-reusable-runtime-config-v4`.

## act

The full Windows Tauri bundle needs a Windows GitHub runner. For local Docker
smoke checks with `act`, run the Linux frontend-only workflow:

```powershell
act workflow_dispatch -W .github/workflows/bigstar-local.yml -j gui-check
```
