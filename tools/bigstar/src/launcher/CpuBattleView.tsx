import { Info, Play, Warning } from '@phosphor-icons/react';
import { type ReactNode, useId, useState } from 'react';
import { css, cx } from 'styled-system/css';
import { card } from 'styled-system/recipes';
import * as Alert from '@/components/ui/alert';
import { Button } from '@/components/ui/button';
import * as RadioGroup from '@/components/ui/radio-group';
import * as Tabs from '@/components/ui/tabs';
import type { CpuMatchRules, CpuOpponent } from '../bindings';
import type { SoloTestController } from '../soloTest';
import { PixelRobot } from './PixelIcons';
import { roomRuleParts } from './roomRules';
import { MatchRuleFields } from './SegmentedField';
import { StatusDot } from './StatusDot';

/** 強さの段階の数。今後もっと強い CPU を足せるよう、今の相手は下のほうに置く */
const maxStrength = 10;

export const cpuOpponents = [
  { value: 'beginner', label: 'クリボー', strength: 1 },
  { value: 'combat_v2', label: 'ノコノコ', strength: 2 },
  { value: 'development', label: 'カロン', strength: 3 },
] satisfies {
  value: CpuOpponent;
  label: string;
  strength: number;
}[];

/** 開始できないときに、原因の画面へ移るボタン */
export type CpuBlockedAction = { label: string; onClick: () => void };

export function CpuBattleView({
  blocked,
  blockedAction = null,
  controller,
}: {
  blocked: boolean;
  blockedAction?: CpuBlockedAction | null;
  controller: SoloTestController;
}) {
  const [opponent, setOpponent] = useState<CpuOpponent>('beginner');
  const [rules, setRules] = useState<CpuMatchRules>({
    wins: 3,
    big_stars: 10,
    lives: 'endless',
  });
  const { status, busy } = controller;
  const locked = busy || status.active || status.preparing;
  const activeOpponent = status.active ? status.config?.cpu_opponent : null;
  const displayed = activeOpponent ?? opponent;
  const displayedRules = activeOpponent
    ? (status.config?.cpu_rules ?? rules)
    : rules;
  const name = cpuOpponents.find((entry) => entry.value === displayed)?.label;
  const preparing =
    status.preparing ||
    controller.start.isPending ||
    controller.startCpu.isPending;
  const showSession = preparing || Boolean(activeOpponent);
  const error = controller.error ? String(controller.error) : status.error;
  const opponentHeadingId = useId();
  // CPU 対戦はコースを選ばないので、先頭のコースの項目は外す
  const ruleParts = roomRuleParts({
    bigStars: displayedRules.big_stars,
    courseMode: 'random',
    lives: displayedRules.lives,
    stageCount: 0,
    wins: displayedRules.wins,
  }).slice(1);

  return (
    <Tabs.Panel className={panelClass} keepMounted value="cpu">
      {showSession ? (
        <SessionCard
          detail={[
            preparing
              ? 'melonDS を起動しています'
              : 'melonDS のウィンドウでプレイしてください',
            ...ruleParts,
          ].join(' · ')}
          stopDisabled={preparing || !activeOpponent || busy}
          title={preparing ? '準備しています…' : `${name}と対戦中`}
          tone={preparing ? 'gray' : 'success'}
          onStop={() => {
            controller.startCpu.reset();
            controller.stop.mutate();
          }}
        />
      ) : blocked ? (
        <Alert.Root
          className={css({ alignItems: 'center', py: '3' })}
          role="note"
          status="neutral"
          variant="surface"
        >
          <Alert.Icon>
            <Info weight="bold" />
          </Alert.Icon>
          <Alert.Content>
            <Alert.Description>
              対戦や部屋の待機、ROMの準備を終了すると開始できます。
            </Alert.Description>
          </Alert.Content>
          {blockedAction ? (
            <Button
              colorPalette="gray"
              size="sm"
              variant="subtle"
              onClick={blockedAction.onClick}
            >
              {blockedAction.label}
            </Button>
          ) : null}
        </Alert.Root>
      ) : null}

      {error ? (
        <Alert.Root role="alert" status="error" variant="surface">
          <Alert.Icon>
            <Warning weight="bold" />
          </Alert.Icon>
          <Alert.Content>
            <Alert.Title>CPU対戦でエラーが起きました</Alert.Title>
            <Alert.Description>{error}</Alert.Description>
          </Alert.Content>
        </Alert.Root>
      ) : null}

      <Section
        aside={`強さは${maxStrength}段階`}
        labelId={opponentHeadingId}
        title="対戦相手"
      >
        <RadioGroup.Root
          aria-labelledby={opponentHeadingId}
          card
          className={css({
            gap: '3',
            gridTemplateColumns: 'repeat(3, minmax(0, 1fr))',
          })}
          colorPalette="gray"
          disabled={locked}
          size="xs"
          value={displayed}
          variant="outline"
          onValueChange={(value) => setOpponent(value as CpuOpponent)}
        >
          {cpuOpponents.map((entry) => (
            <OpponentCard
              key={entry.value}
              label={entry.label}
              selected={entry.value === displayed}
              strength={entry.strength}
              value={entry.value}
            />
          ))}
        </RadioGroup.Root>
      </Section>

      <Section title="ルール">
        <MatchRuleFields
          disabled={locked}
          value={{
            bigStars: displayedRules.big_stars,
            lives: displayedRules.lives,
            wins: displayedRules.wins,
          }}
          onChange={(patch) =>
            setRules((current) => ({
              big_stars: patch.bigStars ?? current.big_stars,
              lives: patch.lives ?? current.lives,
              wins: patch.wins ?? current.wins,
            }))
          }
        />
      </Section>

      {showSession ? null : (
        <div
          className={css({ alignItems: 'center', display: 'flex', gap: '4' })}
        >
          <Button
            colorPalette="amber"
            disabled={!controller.available || blocked || locked}
            onClick={() => {
              controller.stop.reset();
              controller.start.reset();
              controller.startCpu.mutate({ opponent, rules });
            }}
          >
            <Play weight="fill" />
            対戦を始める
          </Button>
          <span className={css({ color: 'fg.subtle', textStyle: 'sm' })}>
            {!controller.available && !controller.error
              ? '対戦の開始にはデスクトップアプリが必要です'
              : 'melonDS が起動します'}
          </span>
        </div>
      )}
    </Tabs.Panel>
  );
}

const panelClass = css({
  display: 'flex',
  flexDirection: 'column',
  gap: '8',
  outline: 'none',
});

function Section({
  aside,
  children,
  labelId,
  title,
}: {
  aside?: string;
  children: ReactNode;
  labelId?: string;
  title: string;
}) {
  return (
    <section
      className={css({ display: 'flex', flexDirection: 'column', gap: '3' })}
    >
      <div
        className={css({
          alignItems: 'baseline',
          display: 'flex',
          justifyContent: 'space-between',
        })}
      >
        <h2
          className={css({ fontWeight: 'bold', textStyle: 'md' })}
          id={labelId}
        >
          {title}
        </h2>
        {aside ? (
          <span className={css({ color: 'fg.subtle', textStyle: 'xs' })}>
            {aside}
          </span>
        ) : null}
      </div>
      {children}
    </section>
  );
}

function OpponentCard({
  label,
  selected,
  strength,
  value,
}: {
  label: string;
  selected: boolean;
  strength: number;
  value: CpuOpponent;
}) {
  return (
    <RadioGroup.Label>
      <span
        className={css({
          display: 'flex',
          flexDirection: 'column',
          gap: '3.5',
        })}
      >
        <span
          className={css({
            alignItems: 'center',
            display: 'flex',
            justifyContent: 'space-between',
          })}
        >
          <span className={css({ fontWeight: 'bold', textStyle: 'md' })}>
            {label}
          </span>
          <RadioGroup.Item value={value}>
            <RadioGroup.Indicator />
          </RadioGroup.Item>
        </span>
        <span
          className={css({ alignItems: 'center', display: 'flex', gap: '2.5' })}
        >
          <StrengthPips selected={selected} strength={strength} />
          <span
            className={css({
              color: 'fg.muted',
              fontVariantNumeric: 'tabular-nums',
              textStyle: 'sm',
            })}
          >
            強さ {strength}
          </span>
        </span>
      </span>
    </RadioGroup.Label>
  );
}

/** 強さを 10 段の目盛りで示す。選んでいるカードでは目盛りも明るくする */
function StrengthPips({
  selected,
  strength,
}: {
  selected: boolean;
  strength: number;
}) {
  return (
    <span aria-hidden="true" className={css({ display: 'flex', gap: '0.5' })}>
      {Array.from({ length: maxStrength }, (_, index) => index + 1).map(
        (step) => (
          <span
            key={step}
            className={css({
              bg:
                step > strength ? 'gray.4' : selected ? 'fg.default' : 'gray.9',
              borderRadius: 'xs',
              h: '1.5',
              w: '2.5',
            })}
          />
        ),
      )}
    </span>
  );
}

function SessionCard({
  detail,
  onStop,
  stopDisabled,
  title,
  tone,
}: {
  detail: string;
  onStop: () => void;
  stopDisabled: boolean;
  title: string;
  tone: 'gray' | 'success';
}) {
  return (
    <div
      className={cx(
        card({ variant: 'raised' }).root,
        css({
          alignItems: 'center',
          flexDirection: 'row',
          gap: '4.5',
          px: '5',
          py: '4.5',
        }),
      )}
    >
      <span
        className={css({
          alignItems: 'center',
          bg: 'gray.3',
          borderRadius: 'l3',
          borderWidth: '1px',
          boxSize: '12',
          color: 'fg.muted',
          display: 'flex',
          flexShrink: '0',
          justifyContent: 'center',
        })}
      >
        <PixelRobot size={27} />
      </span>
      {/* 準備中から対戦中に変わったことを読み上げる。終了ボタンは含めない */}
      <output
        className={css({
          display: 'flex',
          flexDirection: 'column',
          flexGrow: '1',
          gap: '1',
          minW: '0',
        })}
      >
        <span
          className={css({
            alignItems: 'center',
            color: tone === 'success' ? 'success.11' : 'fg.default',
            display: 'flex',
            fontWeight: 'bold',
            gap: '2',
            textStyle: 'md',
          })}
        >
          <StatusDot tone={tone} />
          {title}
        </span>
        <span className={css({ color: 'fg.muted', textStyle: 'sm' })}>
          {detail}
        </span>
      </output>
      <Button
        colorPalette="danger"
        disabled={stopDisabled}
        size="sm"
        variant="outline"
        onClick={onStop}
      >
        対戦を終了
      </Button>
    </div>
  );
}
