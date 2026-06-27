import { afterEach, describe, expect, test, vi } from 'vitest';
import {
  areAiDevToolsEnabled,
  currentBuildProfile,
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
});
