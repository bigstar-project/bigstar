export type BuildProfile = 'distribution' | 'local';

export function currentBuildProfile(): BuildProfile {
  return globalThis.__NSMB_MVL_BUILD_PROFILE__ === 'distribution'
    ? 'distribution'
    : 'local';
}

export function isDistributionBuild() {
  return currentBuildProfile() === 'distribution';
}
