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
  badgeTone?: 'green' | 'red' | 'slate' | 'yellow';
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
        gap: '3',
        p: '3.5',
      }}
    >
      {title ? (
        <div
          className={css({
            alignItems: 'center',
            display: 'flex',
            gap: '2',
            justifyContent: 'space-between',
          })}
        >
          <h2
            className={css({
              alignItems: 'center',
              color: 'fg.default',
              display: 'flex',
              fontWeight: 'black',
              gap: '1.5',
              textStyle: 'md',
            })}
          >
            {icon ? (
              <span className={css({ color: 'blue.plain.fg' })}>{icon}</span>
            ) : null}
            {title}
          </h2>
          {badge ? (
            <Badge colorPalette={badgeColorPalette(badgeTone)} variant="subtle">
              {badge}
            </Badge>
          ) : null}
        </div>
      ) : null}
      <div className={css({ display: 'grid', gap: '2.5' })}>{children}</div>
    </Card.Root>
  );
}

function badgeColorPalette(
  tone: 'green' | 'red' | 'slate' | 'yellow',
): 'gray' | 'green' | 'red' | 'yellow' {
  switch (tone) {
    case 'green':
      return 'green';
    case 'red':
      return 'red';
    case 'yellow':
      return 'yellow';
    default:
      return 'gray';
  }
}

export function InfoPanel({
  badge,
  badgeTone,
  children,
  icon,
  title,
}: {
  badge?: string;
  badgeTone?: 'green' | 'red' | 'slate' | 'yellow';
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
        gap: '1.5',
        minH: '20',
        p: '3',
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
          gap: '2',
          textStyle: 'lg',
        })}
      >
        {imageSrc ? (
          <img
            src={imageSrc}
            alt=""
            className={css({
              h: '9',
              objectFit: 'contain',
              w: '9',
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
