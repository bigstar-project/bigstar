import { describe, expect, test, vi } from 'vitest';
import {
  currentSettings,
  initialForm,
  processExited,
  selectedStageFrom,
  withRequiredSeed,
} from './form';

describe('フォーム補助関数', () => {
  test('現在のフォーム値から起動設定を組み立てる', () => {
    expect(
      currentSettings({
        ...initialForm,
        bigStars: 10,
        courseMode: 'select',
        lives: '5',
        matchSeed: ' 0x0e ',
        wins: 3,
      }),
    ).toEqual({
      big_stars: 10,
      course_mode: 'select',
      lives: '5',
      match_seed: '0x0e',
      wins: 3,
    });
  });

  test('10進数と16進数のシードからステージを決定する', () => {
    expect(selectedStageFrom('random', '7')).toBe(2);
    expect(selectedStageFrom('random', '0x0e')).toBe(4);
    expect(selectedStageFrom('select', 'not-a-seed')).toBe(0);
    expect(selectedStageFrom('random', 'not-a-seed')).toBeNull();
  });

  test('ランダムコースでシードが空のときだけ必須シードを生成する', () => {
    vi.spyOn(crypto, 'getRandomValues').mockImplementation((array) => {
      (array as Uint32Array)[0] = 42;
      return array;
    });

    expect(withRequiredSeed({ ...initialForm, matchSeed: '' }).matchSeed).toBe(
      '42',
    );
    expect(
      withRequiredSeed({
        ...initialForm,
        courseMode: 'select',
        matchSeed: '',
      }).matchSeed,
    ).toBe('');
  });

  test('終了済みプロセスのステータス文字列を判定する', () => {
    expect(processExited('exited(1)')).toBe(true);
    expect(processExited('running')).toBe(false);
    expect(processExited(null)).toBe(false);
  });
});
