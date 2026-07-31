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
  if (
    !Number.isInteger(config.devPort) ||
    config.devPort < 1024 ||
    config.devPort > 65535
  ) {
    throw new Error(
      `${expectedEdition}.devPort は 1024 から 65535 の整数が必要です`,
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
  if (config.windows !== undefined) {
    if (
      typeof config.windows?.installerHooks !== 'string' ||
      config.windows.installerHooks.trim() === ''
    ) {
      throw new Error(
        `${expectedEdition}.windows.installerHooks は空にできません`,
      );
    }
    if (
      config.windows.installerHooks.includes('..') ||
      !/^\.\/windows\/[^/\\]+\.nsh$/.test(config.windows.installerHooks)
    ) {
      throw new Error(
        `${expectedEdition}.windows.installerHooks は windows 配下の .nsh ファイルを指定してください`,
      );
    }
  }
  for (const capability of [
    'automaticUnresolvedSessionReport',
    'feedbackSubmission',
    'aiDevToolsInLocalBuilds',
  ]) {
    if (typeof config.capabilities?.[capability] !== 'boolean') {
      throw new Error(
        `${expectedEdition}.capabilities.${capability} は boolean が必要です`,
      );
    }
  }
}

export function tauriEditionOverlay(config, version, options = {}) {
  const overlay = {
    productName: config.displayName,
    identifier: config.identifier,
    plugins: {
      updater: {
        pubkey: config.updater.pubkey,
        endpoints: [config.updater.endpoint],
      },
    },
  };
  if (config.windows?.installerHooks) {
    overlay.bundle = {
      windows: {
        nsis: {
          installerHooks: config.windows.installerHooks,
        },
      },
    };
  }
  if (version) {
    overlay.version = version;
  }
  if (options.development) {
    overlay.build = {
      devUrl: `http://127.0.0.1:${config.devPort}`,
    };
  }
  return overlay;
}

export function writeTauriEditionOverlay(
  config,
  version,
  root = process.cwd(),
  options = {},
) {
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
    `${JSON.stringify(tauriEditionOverlay(config, version, options), null, 2)}\n`,
    'utf8',
  );
  return path;
}
