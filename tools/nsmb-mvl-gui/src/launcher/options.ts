export const courseOptions = [
  { value: 'random', label: 'ランダム' },
  { value: 'select', label: '毎回選ぶ' },
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
