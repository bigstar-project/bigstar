import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import { loadEditionConfig, tauriEditionOverlay } from './edition-config.mjs';

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
