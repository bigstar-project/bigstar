import type { ReactNode } from 'react';
import { css } from 'styled-system/css';
import { Badge } from '@/components/ui/badge';
import * as Card from '@/components/ui/card';

type BadgeTone = 'green' | 'red' | 'slate' | 'yellow';

const badgePalette = {
  green: 'success',
  red: 'danger',
  slate: 'gray',
  yellow: 'warning',
} as const satisfies Record<BadgeTone, string>;

/** AI とひとり検証で使う、見出し付きの枠 */
export function LauncherCard({
  badge,
  badgeTone = 'slate',
  children,
  icon,
  title,
}: {
  badge?: string;
  badgeTone?: BadgeTone;
  children: ReactNode;
  icon?: ReactNode;
  title?: string;
}) {
  return (
    <Card.Root size="sm" variant="raised">
      {title ? (
        <Card.Header
          className={css({
            alignItems: 'center',
            flexDirection: 'row',
            gap: '2',
            justifyContent: 'space-between',
            pb: '3',
          })}
        >
          <h2
            className={css({
              alignItems: 'center',
              display: 'flex',
              fontWeight: 'bold',
              gap: '2',
              textStyle: 'md',
            })}
          >
            {icon ? (
              <span className={css({ color: 'fg.subtle', display: 'flex' })}>
                {icon}
              </span>
            ) : null}
            {title}
          </h2>
          {badge ? (
            <Badge colorPalette={badgePalette[badgeTone]}>{badge}</Badge>
          ) : null}
        </Card.Header>
      ) : null}
      {/* 隣のカードに高さをそろえられても、中身は上に詰める */}
      <Card.Body
        className={css({ alignContent: 'start', display: 'grid', gap: '2.5' })}
      >
        {children}
      </Card.Body>
    </Card.Root>
  );
}

export function SmallInfoCard({
  caption,
  icon,
  imageSrc,
  label,
  value,
}: {
  caption?: string;
  icon?: ReactNode;
  imageSrc?: string;
  label: string;
  value: string;
}) {
  return (
    <Card.Root
      className={css({ display: 'grid', gap: '1.5', minH: '20', p: '3' })}
      variant="raised"
    >
      <div className={css({ color: 'fg.muted', textStyle: 'xs' })}>{label}</div>
      <div
        className={css({
          alignItems: 'center',
          color: 'fg.default',
          display: 'flex',
          fontWeight: 'bold',
          gap: '2',
          textStyle: 'lg',
        })}
      >
        {imageSrc ? (
          <img
            src={imageSrc}
            alt=""
            className={css({ h: '9', objectFit: 'contain', w: '9' })}
          />
        ) : (
          <span className={css({ color: 'fg.subtle', display: 'flex' })}>
            {icon}
          </span>
        )}
        {value}
      </div>
      {caption ? (
        <div className={css({ color: 'fg.subtle', textStyle: 'xs' })}>
          {caption}
        </div>
      ) : null}
    </Card.Root>
  );
}
