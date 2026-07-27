import { afterEach, describe, expect, test, vi } from 'vitest';
import {
  areAiDevToolsEnabled,
  currentEdition,
  currentEditionConfig,
  currentRuntimeCapabilities,
} from './buildProfile';

afterEach(() => {
  vi.unstubAllGlobals();
});

describe('build profile', () => {
  test('未指定ならlocal Insiders向け能力を使用する', () => {
    expect(areAiDevToolsEnabled()).toBe(true);
    expect(currentRuntimeCapabilities()).toEqual({
      aiDevTools: true,
      automaticUnresolvedSessionReport: true,
      configurableSignalServer: true,
      manualLogUpload: true,
      notifyOwnRooms: true,
    });
    expect(currentEdition()).toBe('insiders');
    expect(currentEditionConfig().badge).toBe('Insiders');
  });

  test('生成されたPublic distribution設定を参照する', () => {
    vi.stubGlobal('__NSMB_MVL_EDITION_CONFIG__', {
      badge: 'Public',
      displayName: 'Bigstar',
      edition: 'public',
    });
    vi.stubGlobal('__NSMB_MVL_RUNTIME_CAPABILITIES__', {
      aiDevTools: false,
      automaticUnresolvedSessionReport: false,
      configurableSignalServer: false,
      manualLogUpload: true,
      notifyOwnRooms: false,
    });

    expect(currentEdition()).toBe('public');
    expect(areAiDevToolsEnabled()).toBe(false);
    expect(currentRuntimeCapabilities().configurableSignalServer).toBe(false);
  });
});
