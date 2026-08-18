export const courseOptions = [
  { value: 'random', label: 'ランダム' },
  { value: 'select', label: '事前に選ぶ' },
];

const stageLabels = ['草原', '地下', '雪', '土管', '城'] as const;

export const stageOptions = [
  ...stageLabels.map((label, index) => ({ value: String(index), label })),
];

export function stageLabel(stage: number) {
  return stageLabels[stage] ?? `ステージ${stage}`;
}

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
  { value: 'on', label: '有効（試験機能）' },
];
