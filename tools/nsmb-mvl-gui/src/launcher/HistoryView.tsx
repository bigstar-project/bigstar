import {
  ArrowLeft,
  ChartLineUp,
  Funnel,
  SpinnerGap,
} from '@phosphor-icons/react';
import {
  useInfiniteQuery,
  useMutation,
  useQuery,
  useQueryClient,
} from '@tanstack/react-query';
import { useMemo, useState } from 'react';
import { css } from 'styled-system/css';
import { SelectField } from '../components/Fields';
import { Button, Tabs } from '../components/ui';
import { hasPlayedResult } from '../matchHistory';
import {
  matchHistoryDashboardOptions,
  matchHistoryKeys,
  matchHistoryListOptions,
  matchHistoryOpponentsOptions,
} from '../queries/historyQueries';
import { deleteMatchHistory } from '../tauriClient';
import type {
  MatchHistoryDashboard,
  MatchHistoryFilter,
  MatchHistoryOpponent,
  MatchHistoryOutcome,
} from '../types';
import { MatchRecordCollapsible } from './MatchRecordCollapsible';
import { EmptyMatchResultCard } from './MatchResultCard';
import { stageLabel } from './options';
import type { BattleMatchRecord } from './types';

const pageSize = 50;
type PeriodValue =
  | 'recent10'
  | 'recent30'
  | 'recent100'
  | 'days7'
  | 'days30'
  | 'all';

export function HistoryView({
  matches,
  onCreateLogArchive,
  onDeleteMatch,
  onOpenLogDir,
  onUploadLogArchive,
}: {
  matches?: BattleMatchRecord[];
  onCreateLogArchive?: (logDir: string) => Promise<void> | void;
  onDeleteMatch?: (matchId: string) => Promise<void> | void;
  onOpenLogDir?: (logDir: string) => Promise<void> | void;
  onUploadLogArchive?: (logDir: string) => Promise<void> | void;
}) {
  const [period, setPeriod] = useState<PeriodValue>('all');
  const [opponentId, setOpponentId] = useState('all');
  const [opponentName, setOpponentName] = useState<string | null>(null);
  const [stage, setStage] = useState('all');
  const [outcome, setOutcome] = useState<'all' | MatchHistoryOutcome>(
    'completed',
  );
  const queryClient = useQueryClient();

  const opponentsQuery = useQuery(matchHistoryOpponentsOptions(matches));
  const opponents = opponentsQuery.data ?? [];
  const selectedOpponent =
    opponents.find((opponent) => opponent.playerId === opponentId) ?? null;
  const effectiveOpponentId =
    opponentId === 'all' || opponentsQuery.isPending || selectedOpponent
      ? opponentId
      : 'all';

  const baseFilter = useMemo(
    () => createHistoryFilter(period, effectiveOpponentId, stage),
    [effectiveOpponentId, period, stage],
  );
  const listFilter = useMemo<MatchHistoryFilter>(
    () => ({ ...baseFilter, outcome: outcome === 'all' ? null : outcome }),
    [baseFilter, outcome],
  );

  const dashboardQuery = useQuery(
    matchHistoryDashboardOptions(baseFilter, matches),
  );
  const historyQuery = useInfiniteQuery(
    matchHistoryListOptions(listFilter, pageSize, matches),
  );
  const deleteMutation = useMutation({
    mutationFn: async (matchId: string) => {
      if (onDeleteMatch) {
        await onDeleteMatch(matchId);
      } else {
        await deleteMatchHistory(matchId);
      }
    },
    onSuccess: () =>
      queryClient.invalidateQueries({ queryKey: matchHistoryKeys.all }),
  });

  const dashboard = dashboardQuery.data ?? null;
  const page = historyQuery.data
    ? {
        matches: historyQuery.data.pages.flatMap((entry) => entry.matches),
        nextCursor: historyQuery.data.pages.at(-1)?.nextCursor ?? null,
        total: historyQuery.data.pages[0]?.total ?? 0,
      }
    : null;
  const loading = historyQuery.isPending || dashboardQuery.isPending;
  const loadingMore = historyQuery.isFetchingNextPage;
  const error =
    deleteMutation.error ??
    opponentsQuery.error ??
    dashboardQuery.error ??
    historyQuery.error;
  const detailName =
    effectiveOpponentId === 'all'
      ? null
      : (opponentName ?? selectedOpponent?.latestName ?? null);
  const visibleMatches = (page?.matches ?? []).filter(hasPlayedResult);
  const groupedMatches = groupMatchesByDate(visibleMatches);

  const selectOpponent = (playerId: string, playerName: string) => {
    if (!playerId) return;
    setOpponentId(playerId);
    setOpponentName(playerName);
    setOutcome('completed');
  };

  const loadMore = () => historyQuery.fetchNextPage();

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
        {detailName ? (
          <div
            className={css({ alignItems: 'center', display: 'flex', gap: '2' })}
          >
            <Button
              aria-label="全対戦履歴に戻る"
              onClick={() => {
                setOpponentId('all');
                setOpponentName(null);
              }}
              size="xs"
              variant="plain"
            >
              <ArrowLeft size={16} weight="bold" />
              対戦履歴
            </Button>
            <span className={css({ color: 'fg.subtle' })}>/</span>
            <h2
              className={css({
                color: 'fg.default',
                fontWeight: 'black',
                textStyle: 'lg',
              })}
            >
              {detailName}との戦績
            </h2>
          </div>
        ) : null}

        <HistoryFilters
          opponentId={effectiveOpponentId}
          opponents={opponents}
          outcome={outcome}
          period={period}
          stage={stage}
          onOpponentChange={(value) => {
            setOpponentId(value);
            setOpponentName(
              opponents.find((opponent) => opponent.playerId === value)
                ?.latestName ?? null,
            );
          }}
          onOutcomeChange={setOutcome}
          onPeriodChange={setPeriod}
          onStageChange={setStage}
          onReset={() => {
            setPeriod('all');
            setOpponentId('all');
            setOpponentName(null);
            setStage('all');
            setOutcome('completed');
          }}
        />

        {error ? (
          <div
            className={css({
              bg: 'red.subtle.bg',
              borderColor: 'red.outline.border',
              borderRadius: 'l2',
              borderWidth: '1px',
              color: 'red.subtle.fg',
              fontWeight: 'bold',
              px: '3',
              py: '2',
              textStyle: 'sm',
            })}
          >
            戦績を読み込めませんでした: {String(error)}
          </div>
        ) : null}

        {dashboard ? (
          <StatisticsDashboard dashboard={dashboard} stage={stage} />
        ) : null}

        <section className={css({ display: 'grid', gap: '2' })}>
          <div
            className={css({
              alignItems: 'end',
              display: 'flex',
              justifyContent: 'space-between',
            })}
          >
            <div>
              <h2
                className={css({
                  color: 'fg.default',
                  fontWeight: 'black',
                  textStyle: 'lg',
                })}
              >
                対戦履歴
              </h2>
              <p className={css({ color: 'fg.muted', textStyle: 'xs' })}>
                {page ? `${page.total}件` : '読み込み中'}
                {outcome !== 'all' ? '（結果フィルター適用中）' : ''}
              </p>
            </div>
          </div>

          {loading && !page ? (
            <LoadingCard />
          ) : visibleMatches.length === 0 ? (
            <EmptyMatchResultCard
              title="対戦履歴"
              message="条件に一致する対戦はありません"
            />
          ) : (
            <div className={historyCardClassName}>
              {groupedMatches.map((group) => (
                <section key={group.label}>
                  <DateGroupHeader label={group.label} />
                  <div className={css({ display: 'grid' })}>
                    {group.matches.map((match) => (
                      <MatchRecordCollapsible
                        defaultOpen={false}
                        key={match.id}
                        match={match}
                        onCreateLogArchive={
                          onCreateLogArchive && match.logDir
                            ? () => onCreateLogArchive(match.logDir)
                            : undefined
                        }
                        onDelete={() => deleteMutation.mutateAsync(match.id)}
                        onOpenLogDir={
                          onOpenLogDir && match.logDir
                            ? () => onOpenLogDir(match.logDir)
                            : undefined
                        }
                        onSelectOpponent={
                          effectiveOpponentId === 'all'
                            ? selectOpponent
                            : undefined
                        }
                        onUploadLogArchive={
                          onUploadLogArchive && match.logDir
                            ? () => onUploadLogArchive(match.logDir)
                            : undefined
                        }
                      />
                    ))}
                  </div>
                </section>
              ))}
            </div>
          )}
          {page?.nextCursor ? (
            <Button
              alignSelf="center"
              loading={loadingMore}
              onClick={() => void loadMore()}
              size="sm"
              variant="outline"
            >
              さらに読み込む
            </Button>
          ) : null}
        </section>
      </section>
    </Tabs.Content>
  );
}

function HistoryFilters({
  opponentId,
  opponents,
  outcome,
  period,
  stage,
  onOpponentChange,
  onOutcomeChange,
  onPeriodChange,
  onReset,
  onStageChange,
}: {
  opponentId: string;
  opponents: MatchHistoryOpponent[];
  outcome: 'all' | MatchHistoryOutcome;
  period: PeriodValue;
  stage: string;
  onOpponentChange: (value: string) => void;
  onOutcomeChange: (value: 'all' | MatchHistoryOutcome) => void;
  onPeriodChange: (value: PeriodValue) => void;
  onReset: () => void;
  onStageChange: (value: string) => void;
}) {
  return (
    <div
      className={css({
        alignItems: 'end',
        bg: 'app.card',
        borderColor: 'gray.surface.border',
        borderRadius: 'l2',
        borderWidth: '1px',
        display: 'grid',
        gap: '2',
        gridTemplateColumns: 'repeat(4, minmax(0, 1fr)) auto',
        p: '3',
      })}
    >
      <FilterSelect
        label="期間"
        value={period}
        onChange={(value) => onPeriodChange(value as PeriodValue)}
        options={[
          ['all', '全期間'],
          ['recent10', '直近10戦'],
          ['recent30', '直近30戦'],
          ['recent100', '直近100戦'],
          ['days7', '過去7日'],
          ['days30', '過去30日'],
        ]}
      />
      <FilterSelect
        label="対戦相手"
        value={opponentId}
        onChange={onOpponentChange}
        options={[
          ['all', 'すべて'],
          ...opponents.map(
            (opponent) =>
              [
                opponent.playerId,
                `${opponent.latestName}（${opponent.matches}戦）`,
              ] as [string, string],
          ),
        ]}
      />
      <FilterSelect
        label="ステージ"
        value={stage}
        onChange={onStageChange}
        options={[
          ['all', 'すべて'],
          ...[0, 1, 2, 3, 4].map(
            (value) => [String(value), stageLabel(value)] as [string, string],
          ),
        ]}
      />
      <FilterSelect
        label="履歴の結果"
        value={outcome}
        onChange={(value) =>
          onOutcomeChange(value as 'all' | MatchHistoryOutcome)
        }
        options={[
          ['completed', '完了した対戦'],
          ['all', 'すべて'],
          ['win', '勝利'],
          ['loss', '敗北'],
          ['stopped', '中断'],
        ]}
      />
      <Button
        aria-label="フィルターをリセット"
        onClick={onReset}
        size="sm"
        variant="plain"
      >
        <Funnel size={16} weight="bold" />
        リセット
      </Button>
    </div>
  );
}

function FilterSelect({
  label,
  onChange,
  options,
  value,
}: {
  label: string;
  onChange: (value: string) => void;
  options: [string, string][];
  value: string;
}) {
  return (
    <SelectField
      label={label}
      onChange={onChange}
      options={options.map(([optionValue, optionLabel]) => ({
        label: optionLabel,
        value: optionValue,
      }))}
      value={value}
    />
  );
}

function StatisticsDashboard({
  dashboard,
  stage,
}: {
  dashboard: MatchHistoryDashboard;
  stage: string;
}) {
  const { summary } = dashboard;
  const matchRate = percentage(summary.wins, summary.losses);
  const gameRate = percentage(summary.gameWins, summary.gameLosses);
  return (
    <section className={css({ display: 'grid', gap: '3' })}>
      <div
        className={css({
          display: 'grid',
          gap: '2',
          gridTemplateColumns: 'repeat(4, 1fr)',
        })}
      >
        <StatCard
          label="対戦成績"
          value={`${summary.wins}勝 ${summary.losses}敗`}
          note={`${summary.stopped}中断`}
        />
        <StatCard
          label="対戦勝率"
          value={matchRate}
          note="完了した対戦のみ"
          accent
        />
        <StatCard
          label="現在の調子"
          value={
            summary.streakKind
              ? `${summary.streak}${summary.streakKind === 'win' ? '連勝' : '連敗'}`
              : '—'
          }
          note="最新の完了対戦から"
        />
        <StatCard
          label={
            stage === 'all'
              ? 'ゲーム勝率'
              : `${stageLabel(Number(stage))}の勝率`
          }
          value={gameRate}
          note={`${summary.gameWins}勝 ${summary.gameLosses}敗`}
          accent
        />
      </div>
      <div
        className={css({
          display: 'grid',
          gap: '3',
          gridTemplateColumns: 'minmax(0, 1.65fr) minmax(15rem, 1fr)',
        })}
      >
        <WinRateChart points={dashboard.trend} />
        <StageStatistics stages={dashboard.stages} selectedStage={stage} />
      </div>
    </section>
  );
}

function StatCard({
  accent = false,
  label,
  note,
  value,
}: {
  accent?: boolean;
  label: string;
  note: string;
  value: string;
}) {
  return (
    <div
      className={css({
        bg: 'app.card',
        borderColor: accent ? 'blue.outline.border' : 'gray.surface.border',
        borderRadius: 'l2',
        borderWidth: '1px',
        display: 'grid',
        gap: '1',
        minH: '24',
        p: '3',
      })}
    >
      <span
        className={css({
          color: 'fg.muted',
          fontWeight: 'bold',
          textStyle: 'xs',
        })}
      >
        {label}
      </span>
      <strong
        className={css({
          color: accent ? 'blue.plain.fg' : 'fg.default',
          fontSize: '2xl',
          fontWeight: 'black',
          lineHeight: 'tight',
        })}
      >
        {value}
      </strong>
      <span className={css({ color: 'fg.subtle', textStyle: 'xs' })}>
        {note}
      </span>
    </div>
  );
}

function WinRateChart({ points }: { points: MatchHistoryDashboard['trend'] }) {
  const yAxisTicks = [0, 25, 50, 75, 100];
  const coordinates = points.map((point, index) => ({
    x: points.length <= 1 ? 140 : 4 + (index / (points.length - 1)) * 272,
    y: 92 - (point.rollingWinRate ?? 0) * 80,
    point,
  }));
  return (
    <div className={panelClassName}>
      <div
        className={css({
          alignItems: 'center',
          display: 'flex',
          justifyContent: 'space-between',
        })}
      >
        <div>
          <h3 className={panelTitleClassName}>最近の勝率推移</h3>
          <p className={panelNoteClassName}>直近10対戦の移動勝率</p>
        </div>
        <ChartLineUp
          className={css({ color: 'blue.plain.fg' })}
          size={22}
          weight="bold"
        />
      </div>
      {points.length === 0 ? (
        <EmptyAnalysis message="完了した対戦がまだありません" />
      ) : (
        <div
          className={css({
            display: 'grid',
            gap: '2',
            gridTemplateColumns: '2.25rem minmax(0, 1fr)',
            h: '44',
            w: 'full',
          })}
        >
          <div aria-hidden="true" className={css({ position: 'relative' })}>
            {yAxisTicks.map((value) => (
              <span
                className={css({
                  color: 'fg.muted',
                  fontSize: '[10px]',
                  lineHeight: '[1]',
                  position: 'absolute',
                  right: '0',
                  transform: 'translateY(-50%)',
                })}
                key={value}
                style={{ top: `${92 - (value / 100) * 80}%` }}
              >
                {value}%
              </span>
            ))}
          </div>
          <svg
            aria-label="勝率推移グラフ"
            role="img"
            viewBox="0 0 280 100"
            className={css({ h: 'full', overflow: 'visible', w: 'full' })}
          >
            {yAxisTicks.map((value) => {
              const y = 92 - (value / 100) * 80;
              return (
                <line
                  key={value}
                  stroke="currentColor"
                  className={css({ color: 'gray.surface.border' })}
                  strokeWidth="0.45"
                  x1="4"
                  x2="276"
                  y1={y}
                  y2={y}
                />
              );
            })}
            <polyline
              fill="none"
              points={coordinates.map(({ x, y }) => `${x},${y}`).join(' ')}
              stroke="currentColor"
              className={css({ color: 'blue.solid.bg' })}
              strokeLinecap="round"
              strokeLinejoin="round"
              strokeWidth="2"
            />
            {coordinates.map(({ point, x, y }) => (
              <circle
                key={point.matchId}
                cx={x}
                cy={y}
                r="2.1"
                fill="currentColor"
                className={css({
                  color: point.won ? 'yellow.solid.bg' : 'red.solid.bg',
                })}
              >
                <title>{`${formatDate(point.startedAt)} ${point.opponentName} ${point.won ? '勝利' : '敗北'}・移動勝率${Math.round((point.rollingWinRate ?? 0) * 100)}%`}</title>
              </circle>
            ))}
          </svg>
        </div>
      )}
    </div>
  );
}

function StageStatistics({
  selectedStage,
  stages,
}: {
  selectedStage: string;
  stages: MatchHistoryDashboard['stages'];
}) {
  const visibleStages =
    selectedStage === 'all' ? [0, 1, 2, 3, 4] : [Number(selectedStage)];
  return (
    <div className={panelClassName}>
      <div>
        <h3 className={panelTitleClassName}>ステージ別勝率</h3>
        <p className={panelNoteClassName}>決着したゲームを集計</p>
      </div>
      <div className={css({ display: 'grid', gap: '2.5' })}>
        {visibleStages.map((stage) => {
          const stats = stages.find((candidate) => candidate.stage === stage);
          const wins = stats?.wins ?? 0;
          const losses = stats?.losses ?? 0;
          const games = wins + losses;
          const rate = games === 0 ? 0 : wins / games;
          return (
            <div className={css({ display: 'grid', gap: '1' })} key={stage}>
              <div
                className={css({
                  alignItems: 'baseline',
                  display: 'flex',
                  justifyContent: 'space-between',
                })}
              >
                <strong
                  className={css({ color: 'fg.default', textStyle: 'sm' })}
                >
                  {stageLabel(stage)}
                </strong>
                <span
                  className={css({
                    color: 'fg.muted',
                    fontVariantNumeric: 'tabular-nums',
                    textStyle: 'xs',
                  })}
                >
                  {games
                    ? `${Math.round(rate * 100)}%・${wins}勝${losses}敗`
                    : '—・0戦'}
                </span>
              </div>
              <div
                className={css({
                  bg: 'gray.surface.bg',
                  borderRadius: 'full',
                  h: '2',
                  overflow: 'hidden',
                })}
              >
                <div
                  className={css({
                    bg: 'blue.solid.bg',
                    borderRadius: 'full',
                    h: 'full',
                  })}
                  style={{ width: `${rate * 100}%` }}
                />
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}

function EmptyAnalysis({ message }: { message: string }) {
  return (
    <div
      className={css({
        alignItems: 'center',
        color: 'fg.subtle',
        display: 'flex',
        flex: '1',
        justifyContent: 'center',
        minH: '36',
        textStyle: 'sm',
      })}
    >
      {message}
    </div>
  );
}

function LoadingCard() {
  return (
    <div
      className={css({
        alignItems: 'center',
        bg: 'app.card',
        borderColor: 'gray.surface.border',
        borderRadius: 'l2',
        borderWidth: '1px',
        color: 'fg.muted',
        display: 'flex',
        gap: '2',
        justifyContent: 'center',
        minH: '32',
        textStyle: 'sm',
      })}
    >
      <SpinnerGap className={css({ animation: 'spin' })} size={20} />
      対戦履歴を読み込んでいます
    </div>
  );
}

function DateGroupHeader({ label }: { label: string }) {
  return (
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
      <h3
        className={css({
          fontWeight: 'black',
          lineHeight: 'tight',
          textStyle: 'sm',
        })}
      >
        {label}
      </h3>
      <div
        className={css({ bg: 'gray.surface.border', flex: '1', h: '[1px]' })}
      />
    </div>
  );
}

const historyCardClassName = css({
  bg: 'app.card',
  backdropFilter: 'auto',
  backdropBlur: 'md',
  backdropSaturate: '180%',
  borderColor: 'gray.surface.border',
  borderRadius: 'l2',
  borderWidth: '1px',
  overflow: 'hidden',
});

const panelClassName = css({
  bg: 'app.card',
  borderColor: 'gray.surface.border',
  borderRadius: 'l2',
  borderWidth: '1px',
  display: 'flex',
  flexDirection: 'column',
  gap: '3',
  minH: '56',
  p: '3',
});

const panelTitleClassName = css({
  color: 'fg.default',
  fontWeight: 'black',
  textStyle: 'md',
});
const panelNoteClassName = css({ color: 'fg.muted', textStyle: 'xs' });

function createHistoryFilter(
  period: PeriodValue,
  opponentId: string,
  stage: string,
): MatchHistoryFilter {
  const recentMatches = period.startsWith('recent')
    ? Number(period.replace('recent', ''))
    : null;
  const dayCount = period === 'days7' ? 7 : period === 'days30' ? 30 : null;
  const sinceStartedAt = dayCount
    ? new Date(Date.now() - dayCount * 86_400_000).toISOString()
    : null;
  return {
    recentMatches,
    sinceStartedAt,
    opponentPlayerId: opponentId === 'all' ? null : opponentId,
    stage: stage === 'all' ? null : Number(stage),
    outcome: null,
  };
}

function percentage(wins: number, losses: number) {
  const games = wins + losses;
  return games === 0 ? '—' : `${((wins / games) * 100).toFixed(1)}%`;
}

function groupMatchesByDate(matches: BattleMatchRecord[]) {
  const groups = new Map<string, BattleMatchRecord[]>();
  for (const match of matches) {
    const label = relativeDateLabel(match.startedAt);
    groups.set(label, [...(groups.get(label) ?? []), match]);
  }
  return [...groups.entries()].map(([label, groupMatches]) => ({
    label,
    matches: groupMatches,
  }));
}

function relativeDateLabel(value: string) {
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return '日付不明';
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
  if (diffDays === 0) return '今日';
  if (diffDays === 1) return '昨日';
  if (diffDays < 7) return '今週';
  return date.toLocaleDateString('ja-JP', {
    day: '2-digit',
    month: '2-digit',
    year: 'numeric',
  });
}

function formatDate(value: string) {
  const date = new Date(value);
  return Number.isNaN(date.getTime())
    ? value
    : date.toLocaleDateString('ja-JP', { day: '2-digit', month: '2-digit' });
}
