import { css, cx } from 'styled-system/css';
import type { StatusKind } from '../types';

export function StatusPill({
  children,
  kind,
}: {
  children: string;
  kind: StatusKind;
}) {
  const colors: Record<StatusKind, Parameters<typeof css>[0]> = {
    idle: {
      bg: 'gray.surface.bg',
      borderColor: 'gray.surface.border',
      color: 'fg.muted',
    },
    ok: {
      bg: 'green.subtle.bg',
      borderColor: 'green.outline.border',
      color: 'green.subtle.fg',
    },
    warn: {
      bg: 'yellow.subtle.bg',
      borderColor: 'yellow.outline.border',
      color: 'yellow.subtle.fg',
    },
    error: {
      bg: 'red.subtle.bg',
      borderColor: 'red.outline.border',
      color: 'red.subtle.fg',
    },
  };
  const label: Record<StatusKind, string> = {
    idle: '待機',
    ok: '正常',
    warn: '注意',
    error: 'エラー',
  };
  return (
    <div
      className={cx(
        css({
          borderRadius: 'l2',
          borderWidth: '1px',
          display: 'grid',
          gap: '0.5',
          maxW: 'statusMax',
          minH: '12',
          overflowWrap: 'anywhere',
          px: '3',
          py: '2',
        }),
        css(colors[kind]),
      )}
    >
      <span
        className={css({
          fontWeight: 'black',
          opacity: '0.75',
          textStyle: 'xs',
          textTransform: 'uppercase',
        })}
      >
        {label[kind]}
      </span>
      <span
        className={css({
          fontWeight: 'bold',
          lineHeight: 'snug',
          textStyle: 'sm',
        })}
      >
        {children}
      </span>
    </div>
  );
}
