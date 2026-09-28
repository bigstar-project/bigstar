import { Play, Stop } from '@phosphor-icons/react';
import { useState } from 'react';
import { css } from 'styled-system/css';
import type { CpuOpponent } from '../bindings';
import { SelectField } from '../components/Fields';
import { Button, Tabs } from '../components/ui';
import type { SoloTestController } from '../soloTest';
import { LauncherCard } from './LauncherCards';

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
  const { status, busy } = controller;
  const locked = busy || status.active || status.preparing;
  const activeOpponent = status.active ? status.config?.cpu_opponent : null;
  const displayed = activeOpponent ?? opponent;
  const name = cpuOpponents.find((entry) => entry.value === displayed)?.label;
  const error = controller.error ? String(controller.error) : status.error;
  return (
    <Tabs.Content
      value="cpu"
      className={css({ overflowY: 'auto', h: 'full', p: '4' })}
    >
      <div
        className={css({ display: 'grid', gap: '4', maxW: '3xl', mx: 'auto' })}
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
          <p>草原・スター5個・3本先取・残機無制限</p>
          <p className={css({ color: 'fg.muted', textStyle: 'sm' })}>
            キーボードやゲームパッドの操作設定は、設定画面から変更できます。
          </p>
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
                controller.startCpu.mutate(opponent);
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
