import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import process from 'node:process';

export const EDITIONS = ['insiders', 'public'];

export function normalizeEdition(value) {
  const edition = value || 'insiders';
  if (!EDITIONS.includes(edition)) {
    throw new Error(
      `edition は ${EDITIONS.join(' または ')} を指定してください: ${edition}`,
    );
  }
  return edition;
}

export function loadEditionConfig(edition, root = process.cwd()) {
  const normalized = normalizeEdition(edition);
  const path = resolve(root, 'editions', `${normalized}.json`);
  const config = JSON.parse(readFileSync(path, 'utf8'));
  validateEditionConfig(config, normalized);
  return config;
}

export function validateEditionConfig(config, expectedEdition) {
  if (config.edition !== expectedEdition) {
    throw new Error(`edition がファイル名と一致しません: ${expectedEdition}`);
  }
  for (const field of [
    'displayName',
    'badge',
    'identifier',
    'dataDirectoryName',
    'defaultSignalUrl',
  ]) {
    if (typeof config[field] !== 'string' || config[field].trim() === '') {
      throw new Error(`${expectedEdition}.${field} は空にできません`);
    }
  }
  if (
    config.dataDirectoryName === '.' ||
    config.dataDirectoryName === '..' ||
    /[\\/]/.test(config.dataDirectoryName)
  ) {
    throw new Error(
      `${expectedEdition}.dataDirectoryName にパス区切りは指定できません`,
    );
  }
  if (!/^wss?:\/\//.test(config.defaultSignalUrl)) {
    throw new Error(`${expectedEdition}.defaultSignalUrl は WebSocket URL が必要です`);
  }
  if (
    typeof config.updater?.endpoint !== 'string' ||
    !config.updater.endpoint.startsWith('https://')
  ) {
    throw new Error(`${expectedEdition}.updater.endpoint は HTTPS URL が必要です`);
  }
  if (
    typeof config.updater?.pubkey !== 'string' ||
    config.updater.pubkey.trim() === ''
  ) {
    throw new Error(`${expectedEdition}.updater.pubkey は空にできません`);
  }
  for (const capability of [
    'automaticUnresolvedSessionReport',
    'manualLogUpload',
    'aiDevToolsInLocalBuilds',
  ]) {
    if (typeof config.capabilities?.[capability] !== 'boolean') {
      throw new Error(
        `${expectedEdition}.capabilities.${capability} は boolean が必要です`,
      );
    }
  }
}

export function tauriEditionOverlay(config, version) {
  const overlay = {
    productName: config.displayName,
    identifier: config.identifier,
    plugins: {
      updater: {
        pubkey: process.env.NSMB_MVL_UPDATER_PUBKEY || config.updater.pubkey,
        endpoints: [config.updater.endpoint],
      },
    },
  };
  if (version) {
    overlay.version = version;
  }
  return overlay;
}

export function writeTauriEditionOverlay(config, version, root = process.cwd()) {
  const path = resolve(
    root,
    'src-tauri',
    'target',
    'edition-config',
    `${config.edition}.json`,
  );
  mkdirSync(dirname(path), { recursive: true });
  writeFileSync(
    path,
    `${JSON.stringify(tauriEditionOverlay(config, version), null, 2)}\n`,
    'utf8',
  );
  return path;
}
