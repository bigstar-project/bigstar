import { cva, cx, type RecipeVariantProps } from 'styled-system/css';

// 状態を色で示す小さな点。色は tone のパレットの 9 番を使う
const statusDot = cva({
  base: {
    bg: 'current',
    borderRadius: 'full',
    color: 'colorPalette.9',
    flexShrink: '0',
  },
  defaultVariants: { size: 'md', tone: 'gray' },
  variants: {
    // status-ring は currentColor で輪を描くので、点の色は color で渡している
    pulse: { true: { animation: '[status-ring 1.8s ease-out infinite]' } },
    size: { sm: { boxSize: '1.5' }, md: { boxSize: '2' } },
    tone: {
      danger: { colorPalette: 'danger' },
      gray: { colorPalette: 'gray' },
      success: { colorPalette: 'success' },
      warning: { colorPalette: 'warning' },
    },
  },
});

type StatusDotVariants = NonNullable<RecipeVariantProps<typeof statusDot>>;

export type StatusTone = NonNullable<StatusDotVariants['tone']>;

export function StatusDot({
  className,
  ...variants
}: StatusDotVariants & { className?: string }) {
  return (
    <span aria-hidden="true" className={cx(statusDot(variants), className)} />
  );
}
