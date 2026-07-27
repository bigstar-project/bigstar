import assert from 'node:assert/strict';
import {
  existsSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { resolve } from 'node:path';
import test from 'node:test';
import {
  loadBuildProfileConfig,
  resolveRuntimeCapabilities,
} from './build-profile-config.mjs';
import { loadEditionConfig, tauriEditionOverlay } from './edition-config.mjs';
import {
  editionArtifactDirectory,
  stageEditionArtifacts,
} from './stage-edition-artifacts.mjs';

const insiders = loadEditionConfig('insiders');
const publicEdition = loadEditionConfig('public');
const localBuild = loadBuildProfileConfig('local');
const distributionBuild = loadBuildProfileConfig('distribution');

test('版ごとにインストール先・更新先・既定サーバーを分離する', () => {
  assert.notEqual(insiders.identifier, publicEdition.identifier);
  assert.notEqual(
    insiders.dataDirectoryName,
    publicEdition.dataDirectoryName,
  );
  assert.notEqual(insiders.updater.endpoint, publicEdition.updater.endpoint);
  assert.notEqual(insiders.defaultSignalUrl, publicEdition.defaultSignalUrl);
  assert.notEqual(insiders.devPort, publicEdition.devPort);
});

test('Public版にInsiders限定機能を含めない', () => {
  assert.equal(
    insiders.capabilities.automaticUnresolvedSessionReport,
    true,
  );
  assert.equal(
    publicEdition.capabilities.automaticUnresolvedSessionReport,
    false,
  );
  assert.equal(publicEdition.capabilities.aiDevToolsInLocalBuilds, false);
});

test('local版とdistribution版の機能差を能力一覧として定義する', () => {
  assert.throws(
    () => loadBuildProfileConfig('preview'),
    /local または distribution/,
  );
  assert.deepEqual(localBuild.capabilities, {
    configurableSignalServer: true,
    developerTools: true,
    notifyOwnRooms: true,
  });
  assert.deepEqual(distributionBuild.capabilities, {
    configurableSignalServer: false,
    developerTools: false,
    notifyOwnRooms: false,
  });

  const localInsiders = resolveRuntimeCapabilities(insiders, localBuild);
  const localPublic = resolveRuntimeCapabilities(publicEdition, localBuild);
  const distributionInsiders = resolveRuntimeCapabilities(
    insiders,
    distributionBuild,
  );

  assert.equal(localInsiders.configurableSignalServer, true);
  assert.equal(localInsiders.notifyOwnRooms, true);
  assert.equal(localInsiders.aiDevTools, true);
  assert.equal(localPublic.aiDevTools, false);
  assert.equal(distributionInsiders.configurableSignalServer, false);
  assert.equal(distributionInsiders.notifyOwnRooms, false);
  assert.equal(distributionInsiders.aiDevTools, false);
  assert.equal(
    distributionInsiders.automaticUnresolvedSessionReport,
    true,
  );
});

test('Tauri overlayへ版固有値と指定バージョンを反映する', () => {
  const overlay = tauriEditionOverlay(publicEdition, '1.2.3');
  assert.equal(overlay.productName, publicEdition.displayName);
  assert.equal(overlay.identifier, publicEdition.identifier);
  assert.equal(overlay.version, '1.2.3');
  assert.deepEqual(overlay.plugins.updater.endpoints, [
    publicEdition.updater.endpoint,
  ]);
  assert.equal(overlay.bundle, undefined);

  const insidersOverlay = tauriEditionOverlay(insiders, '1.2.3');
  assert.equal(
    insidersOverlay.bundle.windows.nsis.installerHooks,
    './windows/legacy-install-migration.nsh',
  );

  const publicDevOverlay = tauriEditionOverlay(
    publicEdition,
    '1.2.3',
    { development: true },
  );
  assert.equal(
    publicDevOverlay.build.devUrl,
    `http://127.0.0.1:${publicEdition.devPort}`,
  );
});

test('開発起動を共通ランナー経由で版別に構成する', () => {
  const packageJson = JSON.parse(
    readFileSync(new URL('../package.json', import.meta.url), 'utf8'),
  );
  assert.match(packageJson.scripts['dev:insiders'], /tauri-edition\.mjs dev/);
  assert.match(packageJson.scripts['dev:public'], /tauri-edition\.mjs dev/);
  assert.match(
    packageJson.scripts['build:insiders'],
    /tauri-edition\.mjs build/,
  );
  assert.match(
    packageJson.scripts['build:public'],
    /tauri-edition\.mjs build/,
  );
  assert.match(
    packageJson.scripts.build,
    /--edition insiders --build-profile local/,
  );
  assert.match(
    packageJson.scripts['build:local:insiders'],
    /--edition insiders --build-profile local/,
  );
  assert.match(
    packageJson.scripts['build:local:public'],
    /--edition public --build-profile local/,
  );
  assert.match(
    packageJson.scripts['build:distribution:insiders'],
    /--edition insiders --build-profile distribution/,
  );
  assert.match(
    packageJson.scripts['build:distribution:public'],
    /--edition public --build-profile distribution/,
  );
  assert.equal(
    packageJson.scripts['build:insiders'],
    packageJson.scripts['build:distribution:insiders'],
  );
  assert.equal(
    packageJson.scripts['build:public'],
    packageJson.scripts['build:distribution:public'],
  );
});

test('共有Rustキャッシュから版別成果物だけを保存する', () => {
  const root = mkdtempSync(resolve(tmpdir(), 'bigstar-artifacts-'));
  try {
    const targetRelease = resolve(root, 'src-tauri', 'target', 'release');
    mkdirSync(resolve(targetRelease, 'bundle', 'nsis'), { recursive: true });
    mkdirSync(resolve(targetRelease, 'resources'), { recursive: true });
    for (const fileName of [
      'bigstar.exe',
      'melonDS.exe',
      'bigstar-net-bridge.exe',
    ]) {
      writeFileSync(resolve(targetRelease, fileName), fileName);
    }
    writeFileSync(resolve(targetRelease, 'resources', 'symbols9.x'), 'symbols');
    writeFileSync(
      resolve(
        targetRelease,
        'bundle',
        'nsis',
        'Bigstar Insiders_1.2.3_x64-setup.exe',
      ),
      'insiders',
    );
    writeFileSync(
      resolve(
        targetRelease,
        'bundle',
        'nsis',
        'Bigstar_1.2.3_x64-setup.exe',
      ),
      'public',
    );

    const destination = stageEditionArtifacts({
      appVersion: '1.2.3',
      buildProfile: 'distribution',
      buildStartedAt: Date.now(),
      editionConfig: insiders,
      guiRoot: root,
      includeBundles: true,
      profile: 'release',
      targetRoot: resolve(root, 'src-tauri', 'target'),
    });
    assert.equal(
      destination,
      editionArtifactDirectory(root, 'insiders', 'release'),
    );
    assert.equal(
      existsSync(
        resolve(
          destination,
          'installers',
          'Bigstar Insiders_1.2.3_x64-setup.exe',
        ),
      ),
      true,
    );
    assert.equal(
      existsSync(
        resolve(destination, 'installers', 'Bigstar_1.2.3_x64-setup.exe'),
      ),
      false,
    );
    const manifest = JSON.parse(
      readFileSync(
        resolve(destination, 'artifact-manifest.json'),
        'utf8',
      ),
    );
    assert.equal(manifest.edition, 'insiders');
    assert.equal(manifest.rustCache, 'shared');
  } finally {
    rmSync(root, { force: true, recursive: true });
  }
});

test('Bigstarの版名・識別子・保存先を使用する', () => {
  const packageJson = JSON.parse(
    readFileSync(new URL('../package.json', import.meta.url), 'utf8'),
  );
  const cargoManifest = readFileSync(
    new URL('../src-tauri/Cargo.toml', import.meta.url),
    'utf8',
  );
  const bridgeManifest = readFileSync(
    new URL('../../bigstar-net-bridge/Cargo.toml', import.meta.url),
    'utf8',
  );
  const romManifest = readFileSync(
    new URL('../../bigstar-rom/Cargo.toml', import.meta.url),
    'utf8',
  );
  const launcherShell = readFileSync(
    new URL('../src/launcher/LauncherShell.tsx', import.meta.url),
    'utf8',
  );

  assert.equal(packageJson.name, 'bigstar');
  assert.match(cargoManifest, /^name = "bigstar"$/m);
  assert.match(bridgeManifest, /^name = "bigstar-net-bridge"$/m);
  assert.match(romManifest, /^name = "bigstar-rom"$/m);
  assert.equal(publicEdition.displayName, 'Bigstar');
  assert.equal(publicEdition.identifier, 'io.github.bigstar-project.bigstar');
  assert.equal(publicEdition.dataDirectoryName, 'Bigstar');
  assert.equal(insiders.displayName, 'Bigstar Insiders');
  assert.equal(
    insiders.identifier,
    'io.github.bigstar-project.bigstar.insiders',
  );
  assert.equal(insiders.dataDirectoryName, 'Bigstar Insiders');
  assert.equal(
    publicEdition.updater.endpoint,
    'https://github.com/bigstar-project/bigstar/releases/download/public-latest/latest.json',
  );
  assert.equal(
    insiders.updater.endpoint,
    'https://github.com/bigstar-project/bigstar/releases/download/insiders-latest/latest.json',
  );
  assert.match(publicEdition.defaultSignalUrl, /bigstar-signaling-public/);
  assert.match(insiders.defaultSignalUrl, /bigstar-signaling-insiders/);
  assert.doesNotMatch(launcherShell, /\bNSMB\b|Mario vs Luigi Online/);
  assert.match(launcherShell, />\s*BIG\s*</);
  assert.match(launcherShell, />\s*STAR\s*</);
});

test('ウィンドウ状態を版別のアプリデータへ保存する', () => {
  const cargoManifest = readFileSync(
    new URL('../src-tauri/Cargo.toml', import.meta.url),
    'utf8',
  );
  const mainSource = readFileSync(
    new URL('../src-tauri/src/main.rs', import.meta.url),
    'utf8',
  );
  const patchedPlugin = readFileSync(
    new URL(
      '../src-tauri/vendor/tauri-plugin-window-state/src/lib.rs',
      import.meta.url,
    ),
    'utf8',
  );

  assert.match(
    cargoManifest,
    /tauri-plugin-window-state = \{ path = "vendor\/tauri-plugin-window-state" \}/,
  );
  assert.match(
    mainSource,
    /\.with_state_directory\(config::app_data_dir_name\(\)\)/,
  );
  assert.doesNotMatch(mainSource, /^mod window_state;/m);
  assert.match(patchedPlugin, /pub fn with_state_directory/);
  assert.match(patchedPlugin, /\.path\(\)\.data_dir\(\)/);
});

test('旧版の移行インストーラーをInsidersだけへ組み込む', () => {
  const migrationHooks = readFileSync(
    new URL(
      '../src-tauri/windows/legacy-install-migration.nsh',
      import.meta.url,
    ),
    'utf8',
  );
  assert.match(
    migrationHooks,
    /NSMB Mario vs Luigi Online/,
  );
  assert.match(migrationHooks, /NSIS_HOOK_PREINSTALL/);
  assert.match(migrationHooks, /NSIS_HOOK_POSTINSTALL/);
  assert.match(migrationHooks, /\/UPDATE \/P/);
  assert.match(migrationHooks, /UninstallString/);
  assert.match(
    migrationHooks,
    /FileExists.*\$BigstarLegacyInstallDir\\nsmb-mvl-gui\.exe/,
  );
  assert.match(
    migrationHooks,
    /FileExists.*\$INSTDIR\\bigstar\.exe/,
  );
  assert.match(migrationHooks, /BIGSTAR_REMOVE_LEGACY_SHORTCUT/);
  assert.match(migrationHooks, /DeleteRegValue HKCU/);
  assert.match(migrationHooks, /DeleteRegKey SHCTX/);
  assert.doesNotMatch(migrationHooks, /RMDir\s+\/r/i);
  assert.doesNotMatch(
    JSON.stringify(publicEdition),
    /legacy-install-migration/,
  );
});

test('旧LatestをInsiders移行チャンネルへ固定する', () => {
  const workflow = readFileSync(
    new URL(
      '../../../.github/workflows/bigstar-tauri.yml',
      import.meta.url,
    ),
    'utf8',
  );
  assert.match(workflow, /make_latest: "false"/);
  assert.match(workflow, /\$legacyChannelTag = "legacy-latest"/);
  assert.match(workflow, /make_latest=true/);
});

test('通常のInsidersビルドは専用更新チャンネルだけを参照する', () => {
  const tauriConfig = JSON.parse(
    readFileSync(
      new URL('../src-tauri/tauri.conf.json', import.meta.url),
      'utf8',
    ),
  );
  assert.deepEqual(tauriConfig.plugins.updater.endpoints, [
    insiders.updater.endpoint,
  ]);
  assert.equal(tauriConfig.app.windows[0].title, 'Bigstar');
});

test('通常ビルドのバージョンをNode・Tauri・Cargoで統一する', () => {
  const packageVersion = JSON.parse(
    readFileSync(new URL('../package.json', import.meta.url), 'utf8'),
  ).version;
  const tauriVersion = JSON.parse(
    readFileSync(
      new URL('../src-tauri/tauri.conf.json', import.meta.url),
      'utf8',
    ),
  ).version;
  const cargoManifest = readFileSync(
    new URL('../src-tauri/Cargo.toml', import.meta.url),
    'utf8',
  );
  const cargoVersion = cargoManifest.match(
    /^\[package\][\s\S]*?^version = "([^"]+)"/m,
  )?.[1];

  assert.equal(tauriVersion, packageVersion);
  assert.equal(cargoVersion, packageVersion);
});
