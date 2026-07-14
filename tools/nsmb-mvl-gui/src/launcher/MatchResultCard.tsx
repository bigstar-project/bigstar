import { Clock, Trophy } from '@phosphor-icons/react';
import { css, cx } from 'styled-system/css';
import { surface } from 'styled-system/recipes';
import { Card } from '../components/ui';
import { MatchRecordCollapsible } from './MatchRecordCollapsible';
import type { BattleMatchRecord } from './types';

export function MatchResultCard({
  match,
  title = '対戦状況',
}: {
  match: BattleMatchRecord;
  title?: string;
}) {
  return (
    <section
      className={css({
        display: 'grid',
        gap: '2',
      })}
    >
      <div
        className={css({
          alignItems: 'center',
          color: 'fg.default',
          display: 'flex',
          fontWeight: 'black',
          gap: '1.5',
          px: '0.5',
          textStyle: 'md',
        })}
      >
        <Trophy
          className={css({ color: 'yellow.plain.fg' })}
          size={20}
          weight="fill"
        />
        {title}
      </div>
      <div className={cx(surface(), css({ overflow: 'hidden' }))}>
        <MatchRecordCollapsible
          defaultOpen
          match={match}
          showStageDots={false}
          showStartedAt={false}
        />
      </div>
    </section>
  );
}

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
