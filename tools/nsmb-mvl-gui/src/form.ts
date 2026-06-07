import type { CourseMode, FormState, GameSettings } from './types';

export const defaultInputDelayFrames = 4;
export const defaultInputMaxFrameLead = 4;
export const rollbackInputDelayFrames = 2;
export const rollbackInputMaxFrameLead = 2;

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
  wins: 3,
  bigStars: 10,
  lives: '3',
  matchSeed: '',
  inputDelayFrames: defaultInputDelayFrames,
  inputMaxFrameLead: defaultInputMaxFrameLead,
  rollbackEnabled: false,
};

export function currentSettings(form: FormState): GameSettings {
  return {
    course_mode: form.courseMode,
    wins: form.wins,
    big_stars: form.bigStars,
    lives: form.lives,
    match_seed: form.matchSeed.trim(),
    input_delay_frames: form.inputDelayFrames,
    input_max_frame_lead: form.inputMaxFrameLead,
    rollback_enabled: form.rollbackEnabled,
  };
}

export function processExited(value: string | null | undefined): boolean {
  return value?.startsWith('exited(') ?? false;
}

export function withRequiredSeed(form: FormState): FormState {
  if (form.courseMode === 'random' && form.matchSeed.trim() === '') {
    return { ...form, matchSeed: String(generateSeed()) };
  }
  return form;
}

export function selectedStageFrom(
  courseMode: CourseMode,
  matchSeed: string,
): number | null {
  if (courseMode === 'select') {
    return 0;
  }
  const seed = parseSeed(matchSeed.trim());
  if (seed === null) {
    return null;
  }
  return Number(seed % 5n);
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
