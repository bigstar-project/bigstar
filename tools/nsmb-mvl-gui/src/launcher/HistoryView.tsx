import { css } from 'styled-system/css';
import { Tabs } from '../components/ui';
import { MatchRecordCollapsible } from './MatchRecordCollapsible';
import { EmptyMatchResultCard } from './MatchResultCard';
import type { BattleMatchRecord } from './types';

export function HistoryView({
  matches,
  onDeleteMatch,
}: {
  matches: BattleMatchRecord[];
  onDeleteMatch?: (matchId: string) => Promise<void> | void;
}) {
  const visibleMatches = matches.filter(hasPlayedResult);
  const groupedMatches = groupMatchesByDate(visibleMatches);

  return (
    <Tabs.Content value="history">
      <section
        className={css({
          display: 'grid',
          gap: '3',
          maxW: 'contentWide',
          mx: 'auto',
          w: 'full',
        })}
      >
        {visibleMatches.length === 0 ? (
          <EmptyMatchResultCard
            title="対戦履歴"
            message="まだ記録された対戦はありません"
          />
        ) : (
          <div
            className={css({
              bg: 'app.card',
              backdropFilter: 'auto',
              backdropBlur: 'md',
              backdropSaturate: '180%',
              borderColor: 'gray.surface.border',
              borderRadius: 'l2',
              borderWidth: '1px',
              overflow: 'hidden',
            })}
          >
            {groupedMatches.map((group) => (
              <section key={group.label}>
                <div
                  className={css({
                    alignItems: 'center',
                    borderBottomColor: 'gray.surface.border',
                    borderBottomWidth: '1px',
                    color: 'fg.default',
                    display: 'flex',
                    gap: '2',
                    px: '3',
                    py: '2',
                  })}
                >
                  <h2
                    className={css({
                      fontWeight: 'black',
                      lineHeight: 'tight',
                      textStyle: 'sm',
                    })}
                  >
                    {group.label}
                  </h2>
                  <div
                    className={css({
                      bg: 'gray.surface.border',
                      flex: '1',
                      h: '[1px]',
                    })}
                  />
                </div>
                <div className={css({ display: 'grid' })}>
                  {group.matches.map((match) => (
                    <MatchRecordCollapsible
                      defaultOpen={false}
                      key={match.id}
                      match={match}
                      onDelete={
                        onDeleteMatch
                          ? () => onDeleteMatch(match.id)
                          : undefined
                      }
                    />
                  ))}
                </div>
              </section>
            ))}
          </div>
        )}
      </section>
    </Tabs.Content>
  );
}

function groupMatchesByDate(matches: BattleMatchRecord[]) {
  const groups = new Map<string, BattleMatchRecord[]>();
  for (const match of [...matches].sort(
    (left, right) =>
      new Date(right.startedAt).getTime() - new Date(left.startedAt).getTime(),
  )) {
    const label = relativeDateLabel(match.startedAt);
    groups.set(label, [...(groups.get(label) ?? []), match]);
  }
  return [...groups.entries()].map(([label, groupMatches]) => ({
    label,
    matches: groupMatches,
  }));
}

function hasPlayedResult(match: BattleMatchRecord) {
  return match.stages.some((stage) => stage.winner === 0 || stage.winner === 1);
}

function relativeDateLabel(value: string) {
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) {
    return '日付不明';
  }
  const now = new Date();
  const startOfToday = new Date(
    now.getFullYear(),
    now.getMonth(),
    now.getDate(),
  );
  const startOfDate = new Date(
    date.getFullYear(),
    date.getMonth(),
    date.getDate(),
  );
  const diffDays = Math.round(
    (startOfToday.getTime() - startOfDate.getTime()) / 86_400_000,
  );
  if (diffDays === 0) {
    return '今日';
  }
  if (diffDays === 1) {
    return '昨日';
  }
  if (diffDays < 7) {
    return '今週';
  }
  return date.toLocaleDateString('ja-JP', {
    day: '2-digit',
    month: '2-digit',
    year: 'numeric',
  });
}
