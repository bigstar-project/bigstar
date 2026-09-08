import { Flask, FolderOpen, Play, Stop } from '@phosphor-icons/react';
import { useState } from 'react';
import { css } from 'styled-system/css';
import type { SoloControl, SoloTestRequest } from '../bindings';
import { NumberField, SelectField, TextField } from '../components/Fields';
import { Button, Switch, Tabs } from '../components/ui';
import { generateSeed } from '../form';
import { type SoloTestController, soloPresets } from '../soloTest';
import { openLogDir } from '../tauriClient';
import { LauncherCard } from './LauncherCards';

const grid = css({
  display: 'grid',
  gap: '3',
  gridTemplateColumns: { base: '1fr', lg: '1fr 1fr' },
});
export function SoloTestView({
  controller,
  blocked,
}: {
  controller: SoloTestController;
  blocked: boolean;
}) {
  const [preset, setPreset] = useState('wan');
  const [request, setRequest] = useState<SoloTestRequest>(() => ({
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
    <Tabs.Content
      value="solo-test"
      className={css({ overflowY: 'auto', h: 'full', p: '4' })}
    >
      <div
        className={css({ display: 'grid', gap: '4', maxW: '5xl', mx: 'auto' })}
      >
        <LauncherCard
          title="ひとり検証モード"
          icon={<Flask size={22} />}
          badge="ローカルビルド専用"
        >
          <p>
            1台のPCでマリオとルイージを起動し、疑似WAN環境で手動プレイを確認します。
          </p>
          <p className={css({ color: 'fg.muted', textStyle: 'sm' })}>
            操作する側のゲーム画面を選んで遊んでください。もう片側は入力なしで待機します。「両方」では同じゲームパッドが両側に反応する場合があります。
          </p>
          {!controller.available && !controller.error ? (
            <p>
              実際の起動にはローカルビルドしたデスクトップアプリが必要です。
            </p>
          ) : null}
          {blocked ? (
            <output>
              通常対戦・部屋の待機・ROMの準備を終了すると開始できます。
            </output>
          ) : null}
        </LauncherCard>
        <fieldset
          disabled={locked}
          className={css({
            display: 'grid',
            gap: '4',
            border: 'none',
            p: '0',
            m: '0',
            minW: '0',
          })}
        >
          <LauncherCard title="プレイ設定">
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
            <Switch.Root
              checked={displayed.rollback_enabled}
              disabled={locked}
              onCheckedChange={({ checked }) =>
                update('rollback_enabled', checked)
              }
            >
              <Switch.HiddenInput />
              <Switch.Control>
                <Switch.Thumb />
              </Switch.Control>
              <Switch.Label>ロールバックを有効にする</Switch.Label>
            </Switch.Root>
            <p className={css({ color: 'fg.muted', textStyle: 'sm' })}>
              スター10個・残機無制限・1本先取。設定を変更する場合は一度停止してください。
            </p>
          </LauncherCard>
          <LauncherCard title="疑似WAN">
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
                <div key={role} className={css({ display: 'grid', gap: '2' })}>
                  <h3>
                    {role === 'host'
                      ? 'マリオ → ルイージ'
                      : 'ルイージ → マリオ'}
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
            <p className={css({ color: 'fg.muted', textStyle: 'sm' })}>
              遅延・揺らぎの単位はフレーム（約16.7ms）。揺らぎは0〜指定値を追加します。間引きは0で無効、2以上で指定します。入力パケットの送信を再現する機能で、実際のインターネット回線とは異なります。
            </p>
          </LauncherCard>
        </fieldset>
        <LauncherCard
          title={
            status.active
              ? '検証中'
              : status.preparing || busy
                ? '処理中'
                : '停止中'
          }
        >
          <div className={grid}>
            <p>
              マリオ：
              {status.active ? `起動中（PID ${status.host_pid}）` : '停止'}
            </p>
            <p>
              ルイージ：
              {status.active ? `起動中（PID ${status.client_pid}）` : '停止'}
            </p>
          </div>
          <div className={css({ display: 'flex', flexWrap: 'wrap', gap: '2' })}>
            <Button
              disabled={!controller.available || blocked || locked}
              onClick={() => {
                controller.stop.reset();
                controller.start.mutate(request);
              }}
            >
              <Play />
              2つのゲームを開始
            </Button>
            <Button
              variant="outline"
              disabled={!status.active || busy || status.preparing}
              onClick={() => {
                controller.start.reset();
                controller.stop.mutate();
              }}
            >
              <Stop />
              両方を停止
            </Button>
            <Button
              variant="outline"
              disabled={!status.log_dir}
              onClick={() => {
                if (status.log_dir)
                  void openLogDir(status.log_dir).catch((e: unknown) =>
                    setLogError(String(e)),
                  );
              }}
            >
              <FolderOpen />
              ログを開く
            </Button>
          </div>
          {error || logError ? <p role="alert">{error || logError}</p> : null}
          <p className={css({ color: 'fg.muted', textStyle: 'sm' })}>
            片側を閉じると両側を停止します。回線設定・両側の入力・診断ログは検証ごとのフォルダーへ保存されます。
          </p>
        </LauncherCard>
      </div>
    </Tabs.Content>
  );
}
