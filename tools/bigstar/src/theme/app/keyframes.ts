import { defineKeyframes } from '@pandacss/dev';

export const appKeyframes = defineKeyframes({
  // 状態を示す点の周りに、点と同じ色の輪を広げる。色は currentColor で渡す
  'status-ring': {
    '0%': {
      boxShadow: '0 0 0 0 color-mix(in srgb, currentColor 50%, transparent)',
    },
    '70%': { boxShadow: '0 0 0 8px transparent' },
    '100%': { boxShadow: '0 0 0 0 transparent' },
  },
});
