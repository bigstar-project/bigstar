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
  // 相手を待つ枠の点線を、ゆっくり明るくしたり暗くしたりする
  'slot-sweep': {
    '0%, 100%': { borderColor: '{colors.gray.5}' },
    '50%': { borderColor: '{colors.gray.8}' },
  },
  // 「相手を待っています」の 3 つの点を順に点滅させる
  'waiting-dot': {
    '0%, 80%, 100%': { opacity: '0.2' },
    '40%': { opacity: '1' },
  },
});
