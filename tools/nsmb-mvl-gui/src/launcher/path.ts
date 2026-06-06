export function shortPath(value: string) {
  if (!value) {
    return '未設定';
  }
  const parts = value.split(/[\\/]/);
  return parts.at(-1) || value;
}

export function readyLabel(value: string) {
  return value ? '設定済み' : '未設定';
}
