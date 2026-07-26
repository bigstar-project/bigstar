import { afterEach, describe, expect, test, vi } from 'vitest';
import {
  areAiDevToolsEnabled,
  currentBuildProfile,
  currentEdition,
  currentEditionConfig,
  editionCapabilities,
  isDistributionBuild,
} from './buildProfile';

afterEach(() => {
  vi.unstubAllGlobals();
});

describe('build profile', () => {
  test('未指定ならlocal buildとして扱う', () => {
    expect(currentBuildProfile()).toBe('local');
    expect(isDistributionBuild()).toBe(false);
  });

  test('distribution指定ならdistribution buildとして扱う', () => {
    vi.stubGlobal('__NSMB_MVL_BUILD_PROFILE__', 'distribution');

    expect(currentBuildProfile()).toBe('distribution');
    expect(isDistributionBuild()).toBe(true);
  });

  test('AI開発機能は明示的にfalseのときだけ無効化する', () => {
    expect(areAiDevToolsEnabled()).toBe(true);

    vi.stubGlobal('__NSMB_MVL_AI_DEVTOOLS_ENABLED__', false);

    expect(areAiDevToolsEnabled()).toBe(false);
  });

  test('未指定ならInsiders版として扱う', () => {
    expect(currentEdition()).toBe('insiders');
    expect(currentEditionConfig().badge).toBe('Insiders');
    expect(editionCapabilities().automaticUnresolvedSessionReport).toBe(true);
  });

  test('生成されたPublic版設定を参照する', () => {
    vi.stubGlobal('__NSMB_MVL_EDITION_CONFIG__', {
      badge: 'Public',
      capabilities: {
        aiDevToolsInLocalBuilds: false,
        automaticUnresolvedSessionReport: false,
        manualLogUpload: true,
      },
      displayName: 'Bigstar',
      edition: 'public',
    });

    expect(currentEdition()).toBe('public');
    expect(editionCapabilities().automaticUnresolvedSessionReport).toBe(false);
  });
});
