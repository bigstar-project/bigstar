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
import { loadEditionConfig, tauriEditionOverlay } from './edition-config.mjs';
import {
  editionArtifactDirectory,
  stageEditionArtifacts,
} from './stage-edition-artifacts.mjs';

const insiders = loadEditionConfig('insiders');
const publicEdition = loadEditionConfig('public');

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
});

test('共有Rustキャッシュから版別成果物だけを保存する', () => {
  const root = mkdtempSync(resolve(tmpdir(), 'bigstar-artifacts-'));
  try {
    const targetRelease = resolve(root, 'src-tauri', 'target', 'release');
    mkdirSync(resolve(targetRelease, 'bundle', 'nsis'), { recursive: true });
    mkdirSync(resolve(targetRelease, 'resources'), { recursive: true });
    for (const fileName of [
      'nsmb-mvl-gui.exe',
      'melonDS.exe',
      'nsmb-net-bridge.exe',
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
    /FileExists.*\$INSTDIR\\nsmb-mvl-gui\.exe/,
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
      '../../../.github/workflows/nsmb-mvl-tauri.yml',
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
