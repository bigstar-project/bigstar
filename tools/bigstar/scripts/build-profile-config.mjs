import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import process from 'node:process';

export const BUILD_PROFILES = ['local', 'distribution'];

const CAPABILITIES = [
  'configurableSignalServer',
  'developerTools',
  'notifyOwnRooms',
];

export function normalizeBuildProfile(value) {
  const profile = value || 'local';
  if (!BUILD_PROFILES.includes(profile)) {
    throw new Error(
      `build profile は ${BUILD_PROFILES.join(' または ')} を指定してください: ${profile}`,
    );
  }
  return profile;
}

export function loadBuildProfileConfig(profile, root = process.cwd()) {
  const normalized = normalizeBuildProfile(profile);
  const path = resolve(root, 'build-profiles', `${normalized}.json`);
  const config = JSON.parse(readFileSync(path, 'utf8'));
  validateBuildProfileConfig(config, normalized);
  return config;
}

export function validateBuildProfileConfig(config, expectedProfile) {
  if (config.profile !== expectedProfile) {
    throw new Error(
      `build profile がファイル名と一致しません: ${expectedProfile}`,
    );
  }
  for (const capability of CAPABILITIES) {
    if (typeof config.capabilities?.[capability] !== 'boolean') {
      throw new Error(
        `${expectedProfile}.capabilities.${capability} は boolean が必要です`,
      );
    }
  }
}

export function resolveRuntimeCapabilities(edition, buildProfile) {
  return {
    aiDevTools:
      edition.capabilities.aiDevToolsInLocalBuilds &&
      buildProfile.capabilities.developerTools,
    automaticUnresolvedSessionReport:
      edition.capabilities.automaticUnresolvedSessionReport,
    configurableSignalServer:
      buildProfile.capabilities.configurableSignalServer,
    manualLogUpload: edition.capabilities.manualLogUpload,
    notifyOwnRooms: buildProfile.capabilities.notifyOwnRooms,
  };
}
