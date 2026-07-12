import { css } from 'styled-system/css';
import type { StatusKind } from '../types';
import { Badge, Spinner } from './ui';

export function StatusPill({
  children,
  kind,
  loading = false,
}: {
  children: string;
  kind: StatusKind;
  loading?: boolean;
}) {
  const palette: Record<StatusKind, 'gray' | 'green' | 'yellow' | 'red'> = {
    idle: 'gray',
    ok: 'green',
    warn: 'yellow',
    error: 'red',
  };
  return (
    <Badge
      colorPalette={palette[kind]}
      variant="subtle"
      className={css({
        gap: '1.5',
        maxW: 'statusMax',
        overflowWrap: 'anywhere',
        px: '2.5',
        py: '1.5',
      })}
    >
      {loading ? (
        <Spinner size="xs" borderWidth="0.125em" color="current" />
      ) : null}
      {children}
    </Badge>
  );
}
