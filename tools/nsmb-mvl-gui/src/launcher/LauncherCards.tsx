import type { ReactNode } from 'react';
import { css } from 'styled-system/css';
import { Badge, Card } from '../components/ui';

export function LauncherCard({
  badge,
  badgeTone = 'slate',
  children,
  icon,
  title,
}: {
  badge?: string;
  badgeTone?: 'green' | 'slate';
  children: ReactNode;
  icon?: ReactNode;
  title?: string;
}) {
  return (
    <Card.Root
      variant="outline"
      css={{
        bg: 'app.card',
        backdropFilter: 'auto',
        backdropBlur: 'md',
        backdropSaturate: '180%',
        display: 'grid',
        gap: '4',
        p: '5',
      }}
    >
      {title ? (
        <div
          className={css({
            alignItems: 'center',
            display: 'flex',
            gap: '3',
            justifyContent: 'space-between',
          })}
        >
          <h2
            className={css({
              alignItems: 'center',
              color: 'fg.default',
              display: 'flex',
              fontWeight: 'black',
              gap: '2',
              textStyle: 'lg',
            })}
          >
            {icon ? (
              <span className={css({ color: 'blue.plain.fg' })}>{icon}</span>
            ) : null}
            {title}
          </h2>
          {badge ? (
            <Badge
              colorPalette={badgeTone === 'green' ? 'green' : 'gray'}
              variant="subtle"
            >
              {badge}
            </Badge>
          ) : null}
        </div>
      ) : null}
      <div className={css({ display: 'grid', gap: '3' })}>{children}</div>
    </Card.Root>
  );
}

export function InfoPanel({
  badge,
  badgeTone,
  children,
  icon,
  title,
}: {
  badge?: string;
  badgeTone?: 'green' | 'slate';
  children: ReactNode;
  icon: ReactNode;
  title: string;
}) {
  return (
    <LauncherCard badge={badge} badgeTone={badgeTone} icon={icon} title={title}>
      {children}
    </LauncherCard>
  );
}

export function SettingsPanel({
  children,
  icon,
  title,
}: {
  children: ReactNode;
  icon: ReactNode;
  title: string;
}) {
  return (
    <LauncherCard icon={icon} title={title}>
      {children}
    </LauncherCard>
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
      variant="outline"
      css={{
        bg: 'app.card',
        backdropFilter: 'auto',
        backdropBlur: 'lg',
        backdropSaturate: '180%',
        display: 'grid',
        gap: '2',
        minH: '28',
        p: '4',
      }}
    >
      <div
        className={css({
          color: 'fg.muted',
          fontWeight: 'black',
          textStyle: 'sm',
        })}
      >
        {label}
      </div>
      <div
        className={css({
          alignItems: 'center',
          color: 'fg.default',
          display: 'flex',
          fontWeight: 'black',
          gap: '3',
          textStyle: 'xl',
        })}
      >
        {imageSrc ? (
          <img
            src={imageSrc}
            alt=""
            className={css({
              h: '12',
              objectFit: 'contain',
              w: '12',
            })}
          />
        ) : (
          <span className={css({ color: 'red.plain.fg' })}>{icon}</span>
        )}
        {value}
      </div>
      {caption ? (
        <div
          className={css({
            color: 'fg.subtle',
            fontWeight: 'semibold',
            textStyle: 'sm',
          })}
        >
          {caption}
        </div>
      ) : null}
    </Card.Root>
  );
}
