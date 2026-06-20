import { css } from 'styled-system/css';
import { Tabs } from '../components/ui';
import { EmptyMatchResultCard, MatchResultCard } from './MatchResultCard';
import type { BattleMatchRecord } from './types';

export function HistoryView({ matches }: { matches: BattleMatchRecord[] }) {
  return (
    <Tabs.Content value="history">
      <section
        className={css({
          display: 'grid',
          gap: '4',
          maxW: 'contentWide',
          mx: 'auto',
          w: 'full',
        })}
      >
        {matches.length === 0 ? (
          <EmptyMatchResultCard
            title="対戦履歴"
            message="まだ記録された対戦はありません"
          />
        ) : (
          matches.map((match) => (
            <MatchResultCard key={match.id} match={match} title="対戦履歴" />
          ))
        )}
      </section>
    </Tabs.Content>
  );
}
