import { ArrowLeft, Warning } from '@phosphor-icons/react';
import {
  useInfiniteQuery,
  useMutation,
  useQuery,
  useQueryClient,
} from '@tanstack/react-query';
import { parseAsString, parseAsStringLiteral, useQueryStates } from 'nuqs';
import { useEffect, useId, useLayoutEffect, useMemo, useRef } from 'react';
import { css, cx } from 'styled-system/css';
import { card } from 'styled-system/recipes';
import * as Alert from '@/components/ui/alert';
import { Button } from '@/components/ui/button';
import * as EmptyState from '@/components/ui/empty-state';
import * as Select from '@/components/ui/select';
import { Spinner } from '@/components/ui/spinner';
import * as Tabs from '@/components/ui/tabs';
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
import { stageLabel } from './options';
import { PixelClock } from './PixelIcons';
import type { BattleMatchRecord, FeedbackInput } from './types';

const pageSize = 50;
type PeriodValue =
  | 'recent10'
  | 'recent30'
  | 'recent100'
  | 'days7'
  | 'days30'
  | 'all';
type StageValue = 'all' | '0' | '1' | '2' | '3' | '4';
type OutcomeValue = 'all' | MatchHistoryOutcome;

const defaultFilters = {
  period: 'all',
  opponent: 'all',
  opponentName: null,
  stage: 'all',
  outcome: 'completed',
} as const;

export function HistoryView({
  matches,
  onDeleteMatch,
  onOpenLogDir,
  onUploadLogArchive,
}: {
  matches?: BattleMatchRecord[];
  onDeleteMatch?: (matchId: string) => Promise<void> | void;
  onOpenLogDir?: (logDir: string) => Promise<void> | void;
  onUploadLogArchive?: (
    logDir: string,
    feedback: FeedbackInput,
  ) => Promise<string | null>;
}) {
  const [filters, setFilters] = useQueryStates(
    {
      period: parseAsStringLiteral([
        'recent10',
        'recent30',
        'recent100',
        'days7',
        'days30',
        'all',
      ] as const).withDefault(defaultFilters.period),
      opponent: parseAsString.withDefault(defaultFilters.opponent),
      opponentName: parseAsString,
      stage: parseAsStringLiteral([
        'all',
        '0',
        '1',
        '2',
        '3',
        '4',
      ] as const).withDefault(defaultFilters.stage),
      outcome: parseAsStringLiteral([
        'all',
        'completed',
        'win',
        'loss',
        'stopped',
      ] as const).withDefault(defaultFilters.outcome),
    },
    {
      history: 'push',
      urlKeys: { opponentName: 'name' },
    },
  );
  const {
    period,
    opponent: opponentId,
    opponentName,
    stage,
    outcome,
  } = filters;
  const queryClient = useQueryClient();

  const opponentsQuery = useQuery(matchHistoryOpponentsOptions(matches));
  const opponents = opponentsQuery.data ?? [];
  const selectedOpponent =
    opponents.find((opponent) => opponent.playerId === opponentId) ?? null;
  const effectiveOpponentId =
    opponentId === 'all' || opponentsQuery.isPending || selectedOpponent
      ? opponentId
      : 'all';
  const panelRef = useRef<HTMLDivElement>(null);
  const filterKey = JSON.stringify([
    period,
    effectiveOpponentId,
    stage,
    outcome,
  ]);
  const previousFilterKey = useRef(filterKey);

  useLayoutEffect(() => {
    if (previousFilterKey.current === filterKey) return;
    previousFilterKey.current = filterKey;
    const panel = panelRef.current;
    if (!panel || panel.closest('[hidden], [inert]')) return;
    // 表示対象が変わったら見出し・条件から確認する。window ではなく本文を動かす。
    panel.scrollIntoView({ block: 'start', behavior: 'instant' });
  }, [filterKey]);

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
  const loadMoreRef = useRef<HTMLDivElement>(null);
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
  const filtered =
    period !== defaultFilters.period ||
    effectiveOpponentId !== defaultFilters.opponent ||
    stage !== defaultFilters.stage ||
    outcome !== defaultFilters.outcome;
  const resetFilters = () => {
    void setFilters(defaultFilters);
  };

  const navigateToOpponent = (playerId: string, playerName: string | null) => {
    void setFilters({
      opponent: playerId,
      opponentName: playerId === 'all' ? null : playerName,
    });
  };

  const selectOpponent = (playerId: string, playerName: string) => {
    if (!playerId) return;
    void setFilters({
      opponent: playerId,
      opponentName: playerName,
      outcome: 'completed',
    });
  };

  useEffect(() => {
    const target = loadMoreRef.current;
    if (
      !target ||
      !historyQuery.hasNextPage ||
      typeof IntersectionObserver === 'undefined'
    )
      return;

    const observer = new IntersectionObserver(
      ([entry]) => {
        if (entry?.isIntersecting && !historyQuery.isFetchingNextPage) {
          void historyQuery.fetchNextPage();
        }
      },
      { rootMargin: '300px 0px' },
    );

    observer.observe(target);
    return () => observer.disconnect();
  }, [
    historyQuery.fetchNextPage,
    historyQuery.hasNextPage,
    historyQuery.isFetchingNextPage,
  ]);

  return (
    <Tabs.Panel
      className={panelClass}
      keepMounted
      ref={panelRef}
      value="history"
    >
      {detailName ? (
        <div
          className={css({
            alignItems: 'center',
            display: 'flex',
            gap: '4',
            justifyContent: 'space-between',
          })}
        >
          <h2 className={css({ fontWeight: 'bold', textStyle: 'lg' })}>
            {detailName}との戦績
          </h2>
          <Button
            colorPalette="gray"
            size="xs"
            variant="plain"
            onClick={() => navigateToOpponent('all', null)}
          >
            <ArrowLeft weight="bold" />
            すべての履歴に戻る
          </Button>
        </div>
      ) : null}

      <HistoryFilters
        filtered={filtered}
        opponentId={effectiveOpponentId}
        opponents={opponents}
        outcome={outcome}
        period={period}
        stage={stage}
        onOpponentChange={(value) => {
          navigateToOpponent(
            value,
            opponents.find((opponent) => opponent.playerId === value)
              ?.latestName ?? null,
          );
        }}
        onOutcomeChange={(value) => void setFilters({ outcome: value })}
        onPeriodChange={(value) => void setFilters({ period: value })}
        onReset={resetFilters}
        onStageChange={(value) => void setFilters({ stage: value })}
      />

      {error ? (
        <Alert.Root
          className={css({ alignItems: 'center' })}
          role="alert"
          status="error"
          variant="surface"
        >
          <Alert.Icon>
            <Warning weight="bold" />
          </Alert.Icon>
          <Alert.Content>
            <Alert.Title>戦績を読み込めませんでした</Alert.Title>
          </Alert.Content>
          <Button
            colorPalette="gray"
            size="sm"
            variant="subtle"
            onClick={() => {
              void opponentsQuery.refetch();
              void dashboardQuery.refetch();
              void historyQuery.refetch();
            }}
          >
            再読み込み
          </Button>
        </Alert.Root>
      ) : null}

      {dashboard ? (
        <StatisticsDashboard dashboard={dashboard} stage={stage} />
      ) : null}

      <section
        aria-label="対戦ログ"
        className={css({
          display: 'flex',
          flexDirection: 'column',
          gap: '5',
          mt: '4',
        })}
      >
        <div
          className={css({
            alignItems: 'baseline',
            display: 'flex',
            gap: '2.5',
          })}
        >
          <h2 className={css({ fontWeight: 'bold', textStyle: 'md' })}>
            対戦ログ
          </h2>
          <span
            className={css({
              color: 'fg.subtle',
              fontVariantNumeric: 'tabular-nums',
              textStyle: 'sm',
            })}
          >
            {page ? `${page.total}件` : '読み込み中'}
          </span>
        </div>

        {loading && !page ? (
          <div
            className={css({
              alignItems: 'center',
              color: 'fg.muted',
              display: 'flex',
              gap: '2.5',
              justifyContent: 'center',
              py: '12',
              textStyle: 'sm',
            })}
          >
            <Spinner aria-hidden="true" />
            対戦履歴を読み込んでいます
          </div>
        ) : visibleMatches.length === 0 ? (
          <EmptyState.Root>
            <EmptyState.Icon>
              <PixelClock />
            </EmptyState.Icon>
            {filtered ? (
              <>
                <EmptyState.Title>
                  条件に一致する対戦はありません
                </EmptyState.Title>
                <EmptyState.Actions>
                  <Button
                    colorPalette="gray"
                    size="sm"
                    variant="subtle"
                    onClick={resetFilters}
                  >
                    絞り込みをリセット
                  </Button>
                </EmptyState.Actions>
              </>
            ) : (
              <>
                <EmptyState.Title>まだ対戦の記録がありません</EmptyState.Title>
                <EmptyState.Description>
                  対戦が終わると、ここに結果が並びます。
                </EmptyState.Description>
              </>
            )}
          </EmptyState.Root>
        ) : (
          groupedMatches.map((group) => (
            <DayGroup key={group.key} label={group.label}>
              {group.matches.map((match) => (
                <MatchRecordCollapsible
                  key={match.id}
                  match={match}
                  onDelete={() => deleteMutation.mutateAsync(match.id)}
                  onOpenLogDir={
                    onOpenLogDir && match.logDir
                      ? () => onOpenLogDir(match.logDir)
                      : undefined
                  }
                  onSelectOpponent={
                    effectiveOpponentId === 'all' ? selectOpponent : undefined
                  }
                  onUploadLogArchive={
                    onUploadLogArchive && match.logDir
                      ? (feedback) => onUploadLogArchive(match.logDir, feedback)
                      : undefined
                  }
                />
              ))}
            </DayGroup>
          ))
        )}
        {page?.nextCursor ? (
          <div
            className={css({
              alignItems: 'center',
              display: 'flex',
              h: '10',
              justifyContent: 'center',
            })}
            data-history-load-more=""
            ref={loadMoreRef}
          >
            {loadingMore ? <Spinner label="対戦履歴を読み込み中" /> : null}
          </div>
        ) : null}
      </section>
    </Tabs.Panel>
  );
}

const panelClass = css({
  display: 'flex',
  flexDirection: 'column',
  gap: '5',
  outline: 'none',
});

const periodOptions: { value: PeriodValue; label: string }[] = [
  { value: 'all', label: '全期間' },
  { value: 'recent10', label: '直近10戦' },
  { value: 'recent30', label: '直近30戦' },
  { value: 'recent100', label: '直近100戦' },
  { value: 'days7', label: '過去7日' },
  { value: 'days30', label: '過去30日' },
];

const stageFilterOptions: { value: StageValue; label: string }[] = [
  { value: 'all', label: 'すべて' },
  ...([0, 1, 2, 3, 4] as const).map((value) => ({
    value: String(value) as StageValue,
    label: stageLabel(value),
  })),
];

const outcomeOptions: { value: OutcomeValue; label: string }[] = [
  { value: 'completed', label: '完了した対戦' },
  { value: 'all', label: 'すべて' },
  { value: 'win', label: '勝利' },
  { value: 'loss', label: '敗北' },
  { value: 'stopped', label: '中断' },
];

function HistoryFilters({
  filtered,
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
  filtered: boolean;
  opponentId: string;
  opponents: MatchHistoryOpponent[];
  outcome: OutcomeValue;
  period: PeriodValue;
  stage: StageValue;
  onOpponentChange: (value: string) => void;
  onOutcomeChange: (value: OutcomeValue) => void;
  onPeriodChange: (value: PeriodValue) => void;
  onReset: () => void;
  onStageChange: (value: StageValue) => void;
}) {
  return (
    // 幅が狭くてチップが折り返しても、リセットは右上から動かないよう別の列に置く
    <div
      className={css({
        alignItems: 'start',
        columnGap: '3',
        display: 'grid',
        gridTemplateColumns: 'minmax(0, 1fr) auto',
      })}
    >
      <fieldset
        aria-label="絞り込み"
        // fieldset は既定で中身の幅より縮まないので、折り返せるように最小幅を外す
        className={css({
          display: 'flex',
          flexWrap: 'wrap',
          gap: '2',
          minW: '0',
        })}
      >
        <FilterChip
          label="期間"
          options={periodOptions}
          value={period}
          onChange={onPeriodChange}
        />
        <FilterChip
          label="対戦相手"
          options={[
            { value: 'all', label: 'すべて' },
            ...opponents.map((opponent) => ({
              value: opponent.playerId,
              label: `${opponent.latestName}（${opponent.matches}戦）`,
            })),
          ]}
          value={opponentId}
          onChange={onOpponentChange}
        />
        <FilterChip
          label="コース"
          options={stageFilterOptions}
          value={stage}
          onChange={onStageChange}
        />
        <FilterChip
          label="結果"
          options={outcomeOptions}
          value={outcome}
          onChange={onOutcomeChange}
        />
      </fieldset>
      {/* 条件を変えたときだけ見せる。場所は取っておき、出し入れでチップが折り返し直さないようにする */}
      <Button
        colorPalette="gray"
        size="xs"
        variant="plain"
        style={filtered ? undefined : { visibility: 'hidden' }}
        onClick={onReset}
      >
        リセット
      </Button>
    </div>
  );
}

/** 「期間 全期間 ⌄」のように、名前と今の値を並べた小さな選択欄 */
function FilterChip<Value extends string>({
  label,
  onChange,
  options,
  value,
}: {
  label: string;
  onChange: (value: Value) => void;
  options: { value: Value; label: string }[];
  value: Value;
}) {
  return (
    <Select.Root
      items={options}
      size="xs"
      value={value}
      onValueChange={(next) => {
        if (next !== null) onChange(next);
      }}
    >
      <Select.Trigger
        aria-label={label}
        className={css({ maxW: 'full', pl: '3', w: 'auto' })}
      >
        {/* 名前は aria-label で伝えるので、読み上げでは値だけにする */}
        <span aria-hidden="true" className={css({ color: 'fg.muted' })}>
          {label}
        </span>
        <Select.Value className={css({ fontWeight: 'semibold', maxW: '40' })} />
        <Select.Icon />
      </Select.Trigger>
      <Select.Portal>
        <Select.Positioner alignItemWithTrigger={false} sideOffset={4}>
          <Select.Popup>
            <Select.List>
              {options.map((option) => (
                <Select.Item key={option.value} value={option.value}>
                  <Select.ItemText>{option.label}</Select.ItemText>
                  <Select.ItemIndicator />
                </Select.Item>
              ))}
            </Select.List>
          </Select.Popup>
        </Select.Positioner>
      </Select.Portal>
    </Select.Root>
  );
}

function StatisticsDashboard({
  dashboard,
  stage,
}: {
  dashboard: MatchHistoryDashboard;
  stage: StageValue;
}) {
  const { summary, trend } = dashboard;
  const recent = trend.slice(-10);
  const recentWins = recent.filter((point) => point.won).length;
  return (
    <section aria-label="成績" className={card({ variant: 'raised' }).root}>
      <div
        className={css({
          display: 'grid',
          gridTemplateColumns: 'repeat(3, minmax(0, 1fr))',
        })}
      >
        <StatCell
          label="対戦勝率"
          note={`${summary.wins}勝 ${summary.losses}敗 · 中断 ${summary.stopped}`}
          {...rateValue(summary.wins, summary.losses)}
        />
        <StatCell
          label="直近の連続結果"
          note={
            recent.length > 0
              ? `直近${recent.length}戦で ${recentWins}勝`
              : '完了した対戦を集計'
          }
          unit={
            summary.streakKind
              ? summary.streakKind === 'win'
                ? '連勝'
                : '連敗'
              : ''
          }
          value={summary.streakKind ? String(summary.streak) : '—'}
        />
        <StatCell
          label={
            stage === 'all'
              ? 'ゲーム勝率'
              : `${stageLabel(Number(stage))}の勝率`
          }
          note={`${summary.gameWins}勝 ${summary.gameLosses}敗`}
          {...rateValue(summary.gameWins, summary.gameLosses)}
        />
      </div>
      <div
        className={css({
          borderTopWidth: '1px',
          display: 'grid',
          gridTemplateColumns: 'minmax(0, 2fr) minmax(0, 1fr)',
        })}
      >
        <WinRateChart
          completedMatches={summary.wins + summary.losses}
          points={trend}
        />
        <CourseStatistics selectedStage={stage} stages={dashboard.stages} />
      </div>
    </section>
  );
}

function StatCell({
  label,
  note,
  unit,
  value,
}: {
  label: string;
  note: string;
  unit: string;
  value: string;
}) {
  return (
    <div
      className={css({
        display: 'flex',
        flexDirection: 'column',
        gap: '1.5',
        minW: '0',
        px: '6',
        py: '5',
        '&:not(:first-child)': { borderLeftWidth: '1px' },
      })}
    >
      <span className={css({ color: 'fg.muted', textStyle: 'sm' })}>
        {label}
      </span>
      <span
        className={css({
          alignItems: 'baseline',
          display: 'flex',
          fontVariantNumeric: 'tabular-nums',
          fontWeight: 'semibold',
          textStyle: '3xl',
        })}
      >
        {value}
        {unit ? (
          <span
            className={css({
              color: 'fg.muted',
              fontWeight: 'medium',
              pl: '1',
              textStyle: 'md',
            })}
          >
            {unit}
          </span>
        ) : null}
      </span>
      <span
        className={css({
          color: 'fg.subtle',
          fontVariantNumeric: 'tabular-nums',
          textStyle: 'xs',
          truncate: true,
        })}
      >
        {note}
      </span>
    </div>
  );
}

const chartSectionClass = css({
  display: 'flex',
  flexDirection: 'column',
  gap: '4',
  minW: '0',
  pb: '5',
  pt: '5',
  px: '6',
});

function ChartHeading({ aside, title }: { aside: string; title: string }) {
  return (
    <div
      className={css({
        alignItems: 'baseline',
        display: 'flex',
        gap: '3',
        justifyContent: 'space-between',
      })}
    >
      <h3
        className={css({
          fontWeight: 'bold',
          textStyle: 'sm',
          whiteSpace: 'nowrap',
        })}
      >
        {title}
      </h3>
      <span
        className={css({ color: 'fg.subtle', textStyle: 'xs', truncate: true })}
      >
        {aside}
      </span>
    </div>
  );
}

/**
 * 直近 10 戦の勝率の移り変わり。10 戦に満たないうちの値は点線にする。
 * 下の帯は 1 戦ごとの勝ち負けで、点にカーソルを合わせると相手と日付が出る
 */
function WinRateChart({
  completedMatches,
  points,
}: {
  completedMatches: number;
  points: MatchHistoryDashboard['trend'];
}) {
  const gradientId = `trend-fill-${useId().replace(/[^a-zA-Z0-9_-]/g, '')}`;
  const count = points.length;
  const coordinates = points.map((point, index) => ({
    x: count <= 1 ? 100 : (index / (count - 1)) * 100,
    y: 100 - (point.rollingWinRate ?? 0) * 100,
  }));
  // 表示より前の対戦も、最大 9 戦までは勝率の計算に入っている
  const earlier = Math.min(9, Math.max(0, completedMatches - count));
  const firstFull = Math.max(0, 9 - earlier);
  const warmup = coordinates.slice(0, firstFull + 1);
  const full = coordinates.slice(firstFull);
  const last = coordinates.at(-1);
  const latest = Math.round((points.at(-1)?.rollingWinRate ?? 0) * 100);
  const wins = points.filter((point) => point.won).length;
  const toPoints = (list: { x: number; y: number }[]) =>
    list.map(({ x, y }) => `${x.toFixed(2)},${y.toFixed(2)}`).join(' ');

  return (
    <section className={chartSectionClass}>
      <ChartHeading
        aside="直近10戦の勝率 · 点線は10戦未満"
        title="勝率の推移"
      />
      {count === 0 || !last ? (
        <p
          className={css({
            alignItems: 'center',
            color: 'fg.subtle',
            display: 'flex',
            justifyContent: 'center',
            minH: '36',
            textStyle: 'sm',
          })}
        >
          完了した対戦がまだありません
        </p>
      ) : (
        <div
          className={css({
            columnGap: '2.5',
            display: 'grid',
            // 右の列は最新の勝率を出す場所。線は最新の点より右へ伸びないので、どんな推移でも重ならない。
            // 列の幅は数字に合わせ、余った幅はグラフに回す
            gridTemplateColumns: '2.25rem minmax(0, 1fr) auto',
          })}
        >
          <div
            aria-hidden="true"
            className={css({
              color: 'fg.subtle',
              fontVariantNumeric: 'tabular-nums',
              h: '36',
              position: 'relative',
              textStyle: 'xs',
            })}
          >
            {[100, 50, 0].map((tick) => (
              <span
                key={tick}
                className={css({
                  position: 'absolute',
                  right: '0',
                  transform: 'translateY(-50%)',
                })}
                style={{ top: `${100 - tick}%` }}
              >
                {tick}%
              </span>
            ))}
          </div>
          <div
            aria-label={`直近10戦の勝率の推移。最新は ${latest}%`}
            className={css({ color: 'amber.9', h: '36', position: 'relative' })}
            role="img"
          >
            <span className={cx(gridLineClass, css({ top: '0' }))} />
            <span
              className={cx(
                gridLineClass,
                css({ borderStyle: 'dashed', top: '1/2' }),
              )}
            />
            <span
              className={cx(
                gridLineClass,
                css({ borderColor: 'gray.6', bottom: '0' }),
              )}
            />
            <svg
              aria-hidden="true"
              className={css({
                h: 'full',
                inset: '0',
                overflow: 'visible',
                position: 'absolute',
                w: 'full',
              })}
              preserveAspectRatio="none"
              viewBox="0 0 100 100"
            >
              <defs>
                <linearGradient id={gradientId} x1="0" x2="0" y1="0" y2="1">
                  <stop
                    offset="0"
                    stopColor="currentColor"
                    stopOpacity="0.16"
                  />
                  <stop offset="1" stopColor="currentColor" stopOpacity="0" />
                </linearGradient>
              </defs>
              {full.length > 1 ? (
                <polygon
                  fill={`url(#${gradientId})`}
                  points={`${toPoints(full)} ${full.at(-1)?.x.toFixed(2)},100 ${full[0]?.x.toFixed(2)},100`}
                />
              ) : null}
              {warmup.length > 1 ? (
                <polyline
                  className={css({ color: 'gray.8' })}
                  fill="none"
                  points={toPoints(warmup)}
                  stroke="currentColor"
                  strokeDasharray="3 3"
                  strokeLinejoin="round"
                  strokeWidth="1.5"
                  vectorEffect="non-scaling-stroke"
                />
              ) : null}
              {full.length > 1 ? (
                <polyline
                  fill="none"
                  points={toPoints(full)}
                  stroke="currentColor"
                  strokeLinecap="round"
                  strokeLinejoin="round"
                  strokeWidth="2"
                  vectorEffect="non-scaling-stroke"
                />
              ) : null}
            </svg>
            <span
              className={css({
                bg: 'amber.9',
                borderRadius: 'full',
                boxShadow:
                  '[0 0 0 4px color-mix(in srgb, {colors.amber.9} 18%, transparent)]',
                boxSize: '2.5',
                position: 'absolute',
                transform: 'translate(-50%, -50%)',
              })}
              style={{ left: `${last.x}%`, top: `${last.y}%` }}
            />
          </div>
          <div
            aria-hidden="true"
            className={css({ color: 'amber.9', h: '36', pl: '1' })}
          >
            {/* 列の幅に数字の幅が入るよう、絶対配置ではなく相対配置で点の高さへ動かす */}
            <span
              className={css({
                display: 'block',
                fontVariantNumeric: 'tabular-nums',
                fontWeight: 'semibold',
                position: 'relative',
                textStyle: 'sm',
                transform: 'translateY(-50%)',
                whiteSpace: 'nowrap',
              })}
              style={{ top: `${last.y}%` }}
            >
              {latest}%
            </span>
          </div>
          <span />
          <div
            aria-label={`対戦ごとの勝敗。${count}戦中 ${wins}勝`}
            className={css({ display: 'flex', gap: '0.5', h: '2', mt: '3.5' })}
            role="img"
          >
            {points.map((point) => (
              <span
                key={point.matchId}
                className={css({
                  bg: point.won ? 'gray.10' : 'gray.5',
                  borderRadius: 'xs',
                  flex: '1',
                  minW: '0',
                })}
                title={`${formatDate(point.startedAt)} ${point.opponentName} ${point.won ? '勝利' : '敗北'}・この対戦までの直近10戦 ${Math.round((point.rollingWinRate ?? 0) * 100)}%`}
              />
            ))}
          </div>
          {/* この行の右の列と、次の行の目盛りの列を空ける */}
          <span />
          <span />
          <div
            className={css({
              color: 'fg.subtle',
              display: 'flex',
              fontVariantNumeric: 'tabular-nums',
              justifyContent: 'space-between',
              mt: '2',
              textStyle: 'xs',
            })}
          >
            <span>{count > 1 ? `${count}戦前` : ''}</span>
            <span
              aria-hidden="true"
              className={css({
                alignItems: 'center',
                display: 'flex',
                gap: '3',
              })}
            >
              <LegendSwatch className={css({ bg: 'gray.10' })} label="勝ち" />
              <LegendSwatch className={css({ bg: 'gray.5' })} label="負け" />
            </span>
            <span>最新</span>
          </div>
        </div>
      )}
    </section>
  );
}

const gridLineClass = css({
  borderColor: 'gray.4',
  borderTopWidth: '1px',
  insetX: '0',
  position: 'absolute',
});

function LegendSwatch({
  className,
  label,
}: {
  className: string;
  label: string;
}) {
  return (
    <span className={css({ alignItems: 'center', display: 'flex', gap: '1' })}>
      <span
        className={cx(css({ borderRadius: 'xs', boxSize: '2' }), className)}
      />
      {label}
    </span>
  );
}

/** コースごとのゲーム単位の勝率。いちばん勝てているコースの棒だけ色を付ける */
function CourseStatistics({
  selectedStage,
  stages,
}: {
  selectedStage: StageValue;
  stages: MatchHistoryDashboard['stages'];
}) {
  const visibleStages =
    selectedStage === 'all' ? [0, 1, 2, 3, 4] : [Number(selectedStage)];
  const rows = visibleStages.map((stage) => {
    const stats = stages.find((candidate) => candidate.stage === stage);
    const wins = stats?.wins ?? 0;
    const losses = stats?.losses ?? 0;
    const games = wins + losses;
    return { stage, wins, losses, rate: games === 0 ? null : wins / games };
  });
  const bestRate = Math.max(...rows.map((row) => row.rate ?? -1));

  return (
    <section className={cx(chartSectionClass, css({ borderLeftWidth: '1px' }))}>
      <ChartHeading aside="ゲーム単位" title="コース別勝率" />
      <div
        className={css({
          display: 'flex',
          flexDirection: 'column',
          gap: '3',
          pt: '1',
        })}
      >
        {rows.map((row) => (
          <div
            key={row.stage}
            className={css({
              alignItems: 'center',
              columnGap: '2.5',
              display: 'grid',
              fontVariantNumeric: 'tabular-nums',
              gridTemplateColumns: {
                base: '2rem minmax(0, 1fr) 2.25rem',
                lg: '2rem minmax(0, 1fr) 2.25rem 2.5rem',
              },
              textStyle: 'sm',
            })}
          >
            <span>{stageLabel(row.stage)}</span>
            <span
              className={css({
                bg: 'gray.4',
                borderRadius: 'full',
                display: 'flex',
                h: '1.5',
              })}
            >
              <span
                className={css({
                  bg:
                    row.rate !== null && row.rate === bestRate
                      ? 'amber.9'
                      : 'gray.8',
                  borderRadius: 'full',
                })}
                style={{ width: `${(row.rate ?? 0) * 100}%` }}
              />
            </span>
            <span className={css({ fontWeight: 'semibold', textAlign: 'end' })}>
              {row.rate === null ? '—' : `${Math.round(row.rate * 100)}%`}
            </span>
            <span
              className={css({
                color: 'fg.subtle',
                display: { base: 'none', lg: 'block' },
                textAlign: 'end',
                textStyle: 'xs',
              })}
            >
              {row.wins}-{row.losses}
            </span>
          </div>
        ))}
      </div>
    </section>
  );
}

function DayGroup({
  children,
  label,
}: {
  children: React.ReactNode;
  label: string;
}) {
  return (
    <section
      className={css({ display: 'flex', flexDirection: 'column', gap: '2.5' })}
    >
      <h3
        className={css({
          color: 'fg.muted',
          fontWeight: 'semibold',
          pl: '0.5',
          textStyle: 'sm',
        })}
      >
        {label}
      </h3>
      <div className={card({ variant: 'raised' }).root}>{children}</div>
    </section>
  );
}

function rateValue(wins: number, losses: number) {
  const games = wins + losses;
  return games === 0
    ? { unit: '', value: '—' }
    : { unit: '%', value: ((wins / games) * 100).toFixed(1) };
}

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

/** 遊んだ日ごとにまとめる。一覧は新しい順なので、まとまりも新しい日から並ぶ */
function groupMatchesByDate(matches: BattleMatchRecord[]) {
  const groups = new Map<
    string,
    { key: string; label: string; matches: BattleMatchRecord[] }
  >();
  for (const match of matches) {
    const date = new Date(match.startedAt);
    const valid = !Number.isNaN(date.getTime());
    const key = valid
      ? `${date.getFullYear()}-${date.getMonth()}-${date.getDate()}`
      : 'unknown';
    const group = groups.get(key) ?? {
      key,
      label: valid ? dayLabel(date) : '日付不明',
      matches: [],
    };
    group.matches.push(match);
    groups.set(key, group);
  }
  return [...groups.values()];
}

/** 「6月21日（日）」。今年でなければ年も付ける */
function dayLabel(date: Date) {
  const weekday = date.toLocaleDateString('ja-JP', { weekday: 'short' });
  const year =
    date.getFullYear() === new Date().getFullYear()
      ? ''
      : `${date.getFullYear()}年`;
  return `${year}${date.getMonth() + 1}月${date.getDate()}日（${weekday}）`;
}

function formatDate(value: string) {
  const date = new Date(value);
  return Number.isNaN(date.getTime())
    ? value
    : date.toLocaleDateString('ja-JP', { day: '2-digit', month: '2-digit' });
}
