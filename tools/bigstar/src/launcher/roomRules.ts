import type { Lives } from '../types';

/** 部屋の一覧と公開中の部屋に出すルールの要約。「ランダム · 3本先取 · スター10 · 残機3」 */
export function roomRuleParts({
  bigStars,
  courseMode,
  lives,
  stageCount,
  wins,
}: {
  bigStars: number;
  courseMode: 'random' | 'select';
  lives: Lives;
  stageCount: number;
  wins: number;
}) {
  return [
    courseMode === 'random' ? 'ランダム' : `指定 ${stageCount}コース`,
    `${wins}本先取`,
    `スター${bigStars}`,
    lives === 'endless' ? '残機無限' : `残機${lives}`,
  ];
}

/** 部屋ができてからの時間。「たった今」「2分前」「1時間前」 */
export function formatRoomAge(createdAtMs: number, now: number) {
  const minutes = Math.floor(Math.max(0, now - createdAtMs) / 60_000);
  if (minutes < 1) return 'たった今';
  if (minutes < 60) return `${minutes}分前`;
  return `${Math.floor(minutes / 60)}時間前`;
}
