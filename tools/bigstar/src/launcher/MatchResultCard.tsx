import { Clock } from '@phosphor-icons/react';
import { css } from 'styled-system/css';
import { Card } from '../components/park-ui';

export function EmptyMatchResultCard({
  message,
  title,
}: {
  message: string;
  title: string;
}) {
  return (
    <Card.Root
      variant="glass"
      css={{
        display: 'grid',
        gap: '2',
        p: '3.5',
      }}
    >
      <div
        className={css({
          alignItems: 'center',
          color: 'fg.default',
          display: 'flex',
          fontWeight: 'black',
          gap: '1.5',
          textStyle: 'md',
        })}
      >
        <Clock className={css({ color: 'blue.plain.fg' })} size={20} />
        {title}
      </div>
      <div
        className={css({
          color: 'fg.muted',
          fontWeight: 'bold',
          textStyle: 'sm',
        })}
      >
        {message}
      </div>
    </Card.Root>
  );
}
