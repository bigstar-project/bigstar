export type BuildProfile = 'distribution' | 'local';
export type AppEdition = 'insiders' | 'public';

const insidersFallback = {
  badge: 'Insiders',
  capabilities: {
    aiDevToolsInLocalBuilds: true,
    automaticUnresolvedSessionReport: true,
    manualLogUpload: true,
  },
  displayName: 'Bigstar Insiders',
  edition: 'insiders' as const,
};

export function currentBuildProfile(): BuildProfile {
  return globalThis.__NSMB_MVL_BUILD_PROFILE__ === 'distribution'
    ? 'distribution'
    : 'local';
}

export function isDistributionBuild() {
  return currentBuildProfile() === 'distribution';
}

export function areAiDevToolsEnabled() {
  return globalThis.__NSMB_MVL_AI_DEVTOOLS_ENABLED__ !== false;
}

export function currentEditionConfig() {
  return globalThis.__NSMB_MVL_EDITION_CONFIG__ ?? insidersFallback;
}

export function currentEdition(): AppEdition {
  return currentEditionConfig().edition;
}

export function editionCapabilities() {
  return currentEditionConfig().capabilities;
}
