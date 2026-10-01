import { FolderOpen, Info, Play, Stop, Warning } from '@phosphor-icons/react';
import { type ReactNode, useState } from 'react';
import { css } from 'styled-system/css';
import * as Alert from '@/components/ui/alert';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import * as Card from '@/components/ui/card';
import * as Switch from '@/components/ui/switch';
import * as Tabs from '@/components/ui/tabs';
import type { SoloControl, SoloTestRequest } from '../bindings';
import { NumberField, SelectField, TextField } from '../components/Fields';
import { generateSeed } from '../form';
import { type SoloTestController, soloPresets } from '../soloTest';
import { openLogDir } from '../tauriClient';
import { StatusDot } from './StatusDot';

const grid = css({
  display: 'grid',
  gap: '4',
  gridTemplateColumns: 'repeat(2, minmax(0, 1fr))',
});

const noteClass = css({ color: 'fg.subtle', textStyle: 'xs' });

export function SoloTestView({
  controller,
  blocked,
}: {
  controller: SoloTestController;
  blocked: boolean;
}) {
  const [preset, setPreset] = useState('wan');
  const [request, setRequest] = useState<SoloTestRequest>(() => ({
    cpu_opponent: null,
    cpu_rules: null,
    stage: 3,
    controlled_player: 'mario',
    rollback_enabled: true,
    input_delay_frames: 2,
    match_seed: String(generateSeed()),
    host: { ...soloPresets.wan.host },
    client: { ...soloPresets.wan.client },
  }));
  const [logError, setLogError] = useState('');
  const { status, busy } = controller;
  const locked = busy || status.active || status.preparing;
  const displayed = status.active && status.config ? status.config : request;
  const error = controller.error ? String(controller.error) : status.error;
  const update = <K extends keyof SoloTestRequest>(
    key: K,
    value: SoloTestRequest[K],
  ) => setRequest((current) => ({ ...current, [key]: value }));

  return (
    <Tabs.Panel
      className={css({
        display: 'flex',
        flexDirection: 'column',
        gap: '8',
        outline: 'none',
      })}
      keepMounted
      value="solo-test"
    >
      <div
        className={css({ display: 'flex', flexDirection: 'column', gap: '2' })}
      >
        <div
          className={css({ alignItems: 'center', display: 'flex', gap: '2' })}
        >
          <p className={css({ fontWeight: 'semibold', textStyle: 'sm' })}>
            1台のPCでマリオとルイージを起動し、疑似WAN環境で手動プレイを確認します。
          </p>
          <Badge colorPalette="gray">ローカルビルド専用</Badge>
        </div>
        <p className={css({ color: 'fg.muted', textStyle: 'sm' })}>
          操作する側のゲーム画面を選んで遊んでください。もう片側は入力なしで待機します。「両方」では同じゲームパッドが両側に反応する場合があります。
        </p>
      </div>

      {blocked ? (
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
              通常対戦・部屋の待機・ROMの準備を終了すると開始できます。
            </Alert.Description>
          </Alert.Content>
        </Alert.Root>
      ) : null}

      {error || logError ? (
        <Alert.Root role="alert" status="error" variant="surface">
          <Alert.Icon>
            <Warning weight="bold" />
          </Alert.Icon>
          <Alert.Content>
            <Alert.Title>ひとり検証でエラーが起きました</Alert.Title>
            <Alert.Description>{error || logError}</Alert.Description>
          </Alert.Content>
        </Alert.Root>
      ) : null}

      <fieldset
        disabled={locked}
        className={css({
          border: 'none',
          display: 'flex',
          flexDirection: 'column',
          gap: '8',
          m: '0',
          minW: '0',
          p: '0',
        })}
      >
        <Section
          aside="設定を変更する場合は一度停止してください"
          title="プレイ設定"
        >
          <div className={grid}>
            <SelectField
              disabled={locked}
              label="コース"
              value={String(displayed.stage)}
              options={['草原', '地下', '雪', '土管', '城'].map(
                (label, index) => ({ value: String(index), label }),
              )}
              onChange={(value) => update('stage', Number(value))}
            />
            <SelectField
              disabled={locked}
              label="操作する側"
              value={displayed.controlled_player}
              options={[
                { value: 'mario', label: 'マリオ（ルイージは待機）' },
                { value: 'luigi', label: 'ルイージ（マリオは待機）' },
                { value: 'both', label: '両方を手動操作' },
              ]}
              onChange={(value) =>
                update('controlled_player', value as SoloControl)
              }
            />
            <NumberField
              label="入力遅延（フレーム）"
              min={0}
              max={16}
              value={displayed.input_delay_frames}
              onChange={(value) => update('input_delay_frames', value)}
            />
            <TextField
              label="マッチシード（再現用）"
              value={displayed.match_seed}
              onChange={(value) => update('match_seed', value)}
            />
          </div>
          <Switch.Label
            className={css({
              alignItems: 'center',
              display: 'flex',
              gap: '3',
              textStyle: 'sm',
            })}
          >
            <Switch.Root
              checked={displayed.rollback_enabled}
              colorPalette="gray"
              disabled={locked}
              onCheckedChange={(checked) => update('rollback_enabled', checked)}
            >
              <Switch.Thumb />
            </Switch.Root>
            ロールバックを有効にする
          </Switch.Label>
          <p className={noteClass}>スター10個・残機無制限・1本先取。</p>
        </Section>

        <Section title="疑似WAN">
          <SelectField
            disabled={locked}
            label="回線プリセット"
            value={preset}
            options={[
              ...Object.entries(soloPresets).map(([value, entry]) => ({
                value,
                label: entry.label,
              })),
              { value: 'custom', label: 'カスタム' },
            ]}
            onChange={(value) => {
              setPreset(value);
              if (value in soloPresets) {
                const next = soloPresets[value as keyof typeof soloPresets];
                setRequest((current) => ({
                  ...current,
                  host: { ...next.host },
                  client: { ...next.client },
                }));
              }
            }}
          />
          <div className={grid}>
            {(['host', 'client'] as const).map((role) => (
              <div
                key={role}
                className={css({
                  display: 'flex',
                  flexDirection: 'column',
                  gap: '3',
                })}
              >
                <h3
                  className={css({ fontWeight: 'semibold', textStyle: 'sm' })}
                >
                  {role === 'host' ? 'マリオ → ルイージ' : 'ルイージ → マリオ'}
                </h3>
                {(
                  [
                    { key: 'delay_frames', label: '送信遅延', max: 60 },
                    { key: 'jitter_frames', label: '追加の揺らぎ', max: 60 },
                    {
                      key: 'drop_every',
                      label: '間引き（Nパケットごと）',
                      max: 65535,
                    },
                  ] as const
                ).map((field) => (
                  <NumberField
                    key={field.key}
                    label={`${role === 'host' ? 'マリオ側' : 'ルイージ側'} ${field.label}`}
                    min={0}
                    max={field.max}
                    value={displayed[role][field.key]}
                    onChange={(value) => {
                      setPreset('custom');
                      update(role, { ...request[role], [field.key]: value });
                    }}
                  />
                ))}
              </div>
            ))}
          </div>
          <p className={noteClass}>
            遅延・揺らぎの単位はフレーム（約16.7ms）。揺らぎは0〜指定値を追加します。間引きは0で無効、2以上で指定します。入力パケットの送信を再現する機能で、実際のインターネット回線とは異なります。
          </p>
        </Section>
      </fieldset>

      <Section
        title={
          status.active
            ? '検証中'
            : status.preparing || busy
              ? '処理中'
              : '停止中'
        }
      >
        <div className={grid}>
          <ProcessStatus
            label="マリオ"
            pid={status.host_pid}
            running={status.active}
          />
          <ProcessStatus
            label="ルイージ"
            pid={status.client_pid}
            running={status.active}
          />
        </div>
        <p className={noteClass}>
          片側を閉じると両側を停止します。回線設定・両側の入力・診断ログは検証ごとのフォルダーへ保存されます。
        </p>
      </Section>

      <div
        className={css({
          alignItems: 'center',
          display: 'flex',
          flexWrap: 'wrap',
          gap: '3',
        })}
      >
        <Button
          colorPalette="amber"
          disabled={!controller.available || blocked || locked}
          onClick={() => {
            controller.stop.reset();
            controller.start.mutate(request);
          }}
        >
          <Play weight="fill" />
          2つのゲームを開始
        </Button>
        <Button
          colorPalette="gray"
          disabled={!status.active || busy || status.preparing}
          variant="subtle"
          onClick={() => {
            controller.start.reset();
            controller.stop.mutate();
          }}
        >
          <Stop weight="fill" />
          両方を停止
        </Button>
        <Button
          colorPalette="gray"
          disabled={!status.log_dir}
          variant="subtle"
          onClick={() => {
            if (status.log_dir)
              void openLogDir(status.log_dir).catch((e: unknown) =>
                setLogError(String(e)),
              );
          }}
        >
          <FolderOpen weight="bold" />
          ログを開く
        </Button>
        {!controller.available && !controller.error ? (
          <span className={css({ color: 'fg.subtle', textStyle: 'sm' })}>
            実際の起動にはローカルビルドしたデスクトップアプリが必要です
          </span>
        ) : null}
      </div>
    </Tabs.Panel>
  );
}

function Section({
  aside,
  children,
  title,
}: {
  aside?: string;
  children: ReactNode;
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
        <h2 className={css({ fontWeight: 'bold', textStyle: 'md' })}>
          {title}
        </h2>
        {aside ? (
          <span className={css({ color: 'fg.subtle', textStyle: 'xs' })}>
            {aside}
          </span>
        ) : null}
      </div>
      <Card.Root variant="raised">
        <Card.Body className={css({ gap: '4', p: '5' })}>{children}</Card.Body>
      </Card.Root>
    </section>
  );
}

function ProcessStatus({
  label,
  pid,
  running,
}: {
  label: string;
  pid: number | null;
  running: boolean;
}) {
  return (
    <div
      className={css({
        alignItems: 'center',
        display: 'flex',
        gap: '2',
        textStyle: 'sm',
      })}
    >
      <StatusDot pulse={running} tone={running ? 'success' : 'gray'} />
      <span className={css({ fontWeight: 'semibold' })}>{label}</span>
      <span className={css({ color: 'fg.muted' })}>
        {running ? `起動中（PID ${pid}）` : '停止'}
      </span>
    </div>
  );
}
