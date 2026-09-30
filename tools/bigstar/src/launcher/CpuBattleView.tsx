import { Heart, Play, Star, Stop, Trophy } from '@phosphor-icons/react';
import { useState } from 'react';
import { css } from 'styled-system/css';
import type { CpuMatchRules, CpuOpponent, Lives } from '../bindings';
import { SelectField } from '../components/Fields';
import { Button, Tabs } from '../components/park-ui';
import type { SoloTestController } from '../soloTest';
import { LauncherCard } from './LauncherCards';
import { bigStarsOptions, livesOptions, winsOptions } from './options';

export const cpuOpponents = [
  { value: 'beginner', label: 'クリボー' },
  { value: 'combat_v2', label: 'ノコノコ' },
  { value: 'development', label: 'カロン' },
] satisfies { value: CpuOpponent; label: string }[];

export function CpuBattleView({
  controller,
  blocked,
}: {
  controller: SoloTestController;
  blocked: boolean;
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
  const error = controller.error ? String(controller.error) : status.error;
  return (
    <Tabs.Content value="cpu">
      <div
        className={css({
          display: 'grid',
          gap: '4',
          maxW: { base: 'xl', xl: 'mainPanel' },
          mx: 'auto',
          w: 'full',
        })}
      >
        <LauncherCard title="CPU対戦">
          <p>マリオを操作して、CPUのルイージと対戦します。</p>
          <SelectField
            label="対戦相手"
            value={displayed}
            options={cpuOpponents}
            disabled={locked}
            onChange={(value) => setOpponent(value as CpuOpponent)}
          />
          <p className={css({ color: 'fg.muted', textStyle: 'sm' })}>
            クリボー、ノコノコ、カロンの順に手ごわい相手に挑戦できます。
            名前は難易度の目安で、対戦キャラクターはすべてルイージです。
          </p>
          <div
            className={css({
              display: 'grid',
              gap: '3',
              gridTemplateColumns: {
                base: '1fr',
                sm: 'repeat(3, minmax(0, 1fr))',
              },
            })}
          >
            <SelectField
              icon={<Trophy size={18} weight="fill" />}
              label="勝利数"
              options={winsOptions}
              value={String(displayedRules.wins)}
              disabled={locked}
              onChange={(value) =>
                setRules((current) => ({ ...current, wins: Number(value) }))
              }
            />
            <SelectField
              icon={<Star size={18} weight="fill" />}
              label="ビッグスター"
              options={bigStarsOptions}
              value={String(displayedRules.big_stars)}
              disabled={locked}
              onChange={(value) =>
                setRules((current) => ({
                  ...current,
                  big_stars: Number(value),
                }))
              }
            />
            <SelectField
              icon={<Heart size={18} weight="fill" />}
              label="残機"
              options={livesOptions}
              value={displayedRules.lives}
              disabled={locked}
              onChange={(value) =>
                setRules((current) => ({ ...current, lives: value as Lives }))
              }
            />
          </div>
          {blocked ? (
            <output>
              対戦や部屋の待機、ROMの準備を終了すると開始できます。
            </output>
          ) : null}
          {!controller.available && !controller.error ? (
            <p>対戦の開始にはデスクトップアプリが必要です。</p>
          ) : null}
          <div className={css({ display: 'flex', gap: '2' })}>
            <Button
              disabled={!controller.available || blocked || locked}
              onClick={() => {
                controller.stop.reset();
                controller.start.reset();
                controller.startCpu.mutate({ opponent, rules });
              }}
            >
              <Play />
              対戦を始める
            </Button>
            <Button
              variant="outline"
              disabled={!activeOpponent || busy || status.preparing}
              onClick={() => {
                controller.startCpu.reset();
                controller.stop.mutate();
              }}
            >
              <Stop />
              対戦を終了
            </Button>
          </div>
          {status.preparing || busy ? (
            <output>準備しています…</output>
          ) : activeOpponent ? (
            <output>{name}と対戦中</output>
          ) : null}
          {error ? <p role="alert">{error}</p> : null}
        </LauncherCard>
      </div>
    </Tabs.Content>
  );
}
