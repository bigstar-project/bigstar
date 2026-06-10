export const courseOptions = [
  { value: 'random', label: 'ランダム' },
  { value: 'select', label: '事前に選ぶ' },
];

export const stageOptions = [
  { value: '0', label: '0: Grass' },
  { value: '1', label: '1: Cave' },
  { value: '2', label: '2: Snow' },
  { value: '3', label: '3: Pipe' },
  { value: '4', label: '4: Castle' },
];

export const winsOptions = [1, 2, 3].map((value) => ({
  value: String(value),
  label: String(value),
}));

export const bigStarsOptions = [3, 5, 10].map((value) => ({
  value: String(value),
  label: String(value),
}));

export const livesOptions = [
  { value: '3', label: '3' },
  { value: '5', label: '5' },
  { value: 'endless', label: '無限' },
];

export const rollbackOptions = [
  { value: 'off', label: '無効' },
  { value: 'on', label: '有効' },
];
