import { describe, expect, test, vi } from 'vitest';
import {
  currentSettings,
  generateUniqueStageSequence,
  initialForm,
  normalizedCourseStages,
  processExited,
  rollbackInputDelayFrames,
  rollbackInputMaxFrameLead,
  rollbackPredictionHorizonFrames,
  selectedStageFrom,
  withRequiredPlan,
  withRequiredSeed,
} from './form';

describe('フォーム補助関数', () => {
  test('ROM-loopロールバックの固定契約を使う', () => {
    expect(rollbackInputDelayFrames).toBe(2);
    expect(rollbackInputMaxFrameLead).toBe(0);
    expect(rollbackPredictionHorizonFrames).toBe(7);
  });

  test('現在のフォーム値から起動設定を組み立てる', () => {
    expect(
      currentSettings({
        ...initialForm,
        bigStars: 10,
        courseMode: 'select',
        courseStages: [4, 3, 2, 1, 0],
        inputDelayFrames: 2,
        inputMaxFrameLead: 0,
        lives: '5',
        matchSeed: ' 0x0e ',
        rngSeeds: ['0x0e', '2', '3', '4', '5'],
        rollbackEnabled: true,
        wins: 3,
      }),
    ).toEqual({
      big_stars: 10,
      course_mode: 'select',
      course_stages: [4, 3, 2, 1, 0],
      input_delay_frames: 2,
      input_max_frame_lead: 0,
      lives: '5',
      match_seed: '0x0e',
      rng_seeds: ['0x0e', '2', '3', '4', '5'],
      rollback_enabled: true,
      wins: 3,
    });
  });

  test('10進数と16進数のシードからステージを決定する', () => {
    expect(selectedStageFrom('random', '7')).toBe(2);
    expect(selectedStageFrom('random', '0x0e')).toBe(4);
    expect(selectedStageFrom('select', 'not-a-seed', [3, 2, 1])).toBe(3);
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
    ).toBe('42');
  });

  test('勝利数から最大試合数分のコースとRNG seedを揃える', () => {
    vi.spyOn(crypto, 'getRandomValues').mockImplementation((array) => {
      (array as Uint32Array)[0] = 7;
      return array;
    });

    expect(
      withRequiredPlan({
        ...initialForm,
        courseStages: [4],
        matchSeed: '11',
        rngSeeds: [],
        wins: 2,
      }),
    ).toMatchObject({
      courseStages: [4, 0, 1],
      matchSeed: '11',
      rngSeeds: ['11', '7', '7'],
    });
  });

  test('ランダムコースは最大5試合分を重複なしで生成する', () => {
    vi.spyOn(crypto, 'getRandomValues').mockImplementation((array) => {
      (array as Uint32Array)[0] = 7;
      return array;
    });

    expect(generateUniqueStageSequence(5)).toEqual([0, 4, 1, 3, 2]);
    expect(
      normalizedCourseStages({
        courseMode: 'random',
        courseStages: [4, 4, 3],
        wins: 2,
      }),
    ).toEqual([4, 3, 0]);
    expect(
      withRequiredPlan(
        {
          ...initialForm,
          matchSeed: '11',
          rngSeeds: [],
          wins: 3,
        },
        { refreshRandom: true },
      ).courseStages,
    ).toEqual([0, 4, 1, 3, 2]);
  });

  test('終了済みプロセスのステータス文字列を判定する', () => {
    expect(processExited('exited(1)')).toBe(true);
    expect(processExited('running')).toBe(false);
    expect(processExited(null)).toBe(false);
  });
});
