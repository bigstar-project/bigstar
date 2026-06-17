import type { CourseMode, FormState, GameSettings } from './types';

export const defaultInputDelayFrames = 4;
export const defaultInputMaxFrameLead = 4;
export const rollbackInputDelayFrames = 2;
export const rollbackInputMaxFrameLead = 2;

export const courseCount = 5;

export const initialForm: FormState = {
  role: 'host',
  hostName: 'Player',
  signalUrl: '',
  roomCode: '',
  port: 8165,
  hostRomPath: '',
  clientRomPath: '',
  baseRomPath: '',
  courseMode: 'random',
  courseStages: defaultCourseStages(3),
  wins: 3,
  bigStars: 10,
  lives: '3',
  matchSeed: '',
  rngSeeds: [],
  inputDelayFrames: defaultInputDelayFrames,
  inputMaxFrameLead: defaultInputMaxFrameLead,
  rollbackEnabled: false,
  diagnosticEventsEnabled: false,
};

export function currentSettings(form: FormState): GameSettings {
  const rngSeeds = normalizedRngSeeds(form);
  return {
    course_mode: form.courseMode,
    course_stages: normalizedCourseStages(form),
    wins: form.wins,
    big_stars: form.bigStars,
    lives: form.lives,
    match_seed: rngSeeds[0] ?? form.matchSeed.trim(),
    rng_seeds: rngSeeds,
    input_delay_frames: form.inputDelayFrames,
    input_max_frame_lead: form.inputMaxFrameLead,
    rollback_enabled: form.rollbackEnabled,
  };
}

export function processExited(value: string | null | undefined): boolean {
  return value?.startsWith('exited(') ?? false;
}

export function withRequiredSeed(form: FormState): FormState {
  return withRequiredPlan(form);
}

export function withRequiredPlan(
  form: FormState,
  options: { refreshRandom?: boolean } = {},
): FormState {
  const rngSeeds = normalizedRngSeeds(form, options.refreshRandom);
  const courseStages = normalizedCourseStages(form, options.refreshRandom);
  return {
    ...form,
    courseStages,
    matchSeed: rngSeeds[0] ?? form.matchSeed.trim(),
    rngSeeds,
  };
}

export function maxGamesForWins(wins: number): number {
  const normalizedWins = Math.min(3, Math.max(1, Math.trunc(wins || 1)));
  return normalizedWins * 2 - 1;
}

export function normalizedCourseStages(
  form: Pick<FormState, 'courseMode' | 'courseStages' | 'wins'>,
  refreshRandom = false,
): number[] {
  const count = maxGamesForWins(form.wins);
  if (form.courseMode === 'random' && refreshRandom) {
    return Array.from({ length: count }, () => generateStage());
  }
  const source =
    form.courseStages.length > 0
      ? form.courseStages
      : defaultCourseStages(form.wins);
  return Array.from({ length: count }, (_, index) =>
    clampStage(source[index] ?? source[source.length - 1] ?? 0),
  );
}

export function normalizedRngSeeds(
  form: Pick<FormState, 'matchSeed' | 'rngSeeds' | 'wins'>,
  refreshRandom = false,
): string[] {
  const count = maxGamesForWins(form.wins);
  if (refreshRandom) {
    return Array.from({ length: count }, () => String(generateSeed()));
  }
  const firstSeed = form.matchSeed.trim();
  const source = form.rngSeeds.map((seed) => seed.trim()).filter(Boolean);
  if (firstSeed) {
    source[0] = firstSeed;
  }
  return Array.from({ length: count }, (_, index) =>
    (source[index] ?? String(generateSeed())).trim(),
  );
}

export function selectedStageFrom(
  courseMode: CourseMode,
  matchSeed: string,
  courseStages: number[] = [],
): number | null {
  if (courseStages.length > 0) {
    return clampStage(courseStages[0]);
  }
  const seed = parseSeed(matchSeed.trim());
  if (seed === null) {
    return courseMode === 'select' ? 0 : null;
  }
  return Number(seed % BigInt(courseCount));
}

function parseSeed(value: string): bigint | null {
  if (/^0x[0-9a-f]+$/i.test(value)) {
    return BigInt(value);
  }
  if (/^[0-9]+$/.test(value)) {
    return BigInt(value);
  }
  return null;
}

export function generateSeed(): number {
  const bytes = new Uint32Array(1);
  crypto.getRandomValues(bytes);
  return bytes[0] || Date.now() >>> 0;
}

export function generateStage(): number {
  const bytes = new Uint32Array(1);
  crypto.getRandomValues(bytes);
  return bytes[0] % courseCount;
}

export function clampStage(value: number): number {
  if (!Number.isFinite(value)) {
    return 0;
  }
  return Math.min(courseCount - 1, Math.max(0, Math.trunc(value)));
}

export function defaultCourseStages(wins: number): number[] {
  return Array.from(
    { length: maxGamesForWins(wins) },
    (_, index) => index % courseCount,
  );
}
