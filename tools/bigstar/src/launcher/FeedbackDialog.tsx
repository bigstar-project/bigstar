import { Portal } from '@ark-ui/react';
import { CloudArrowUp } from '@phosphor-icons/react';
import { type ChangeEvent, useState } from 'react';
import { css } from 'styled-system/css';
import { SelectField } from '../components/Fields';
import {
  Button,
  CloseButton,
  Dialog,
  Field,
  Switch,
  Textarea,
} from '../components/ui';
import type { FeedbackCategory } from '../types';
import type { FeedbackInput } from './types';

const categoryOptions: Array<{
  label: string;
  value: FeedbackCategory;
}> = [
  { label: '画面・操作', value: 'gui' },
  { label: '接続', value: 'connection' },
  { label: '動作の重さ・ラグ', value: 'performance' },
  { label: '同期ずれ', value: 'desync' },
  { label: 'クラッシュ', value: 'crash' },
  { label: 'アップデート', value: 'update' },
  { label: 'その他', value: 'other' },
];

export function FeedbackDialog({
  onSubmit,
}: {
  onSubmit: (feedback: FeedbackInput) => Promise<string | null>;
}) {
  const [open, setOpen] = useState(false);
  const [category, setCategory] = useState<FeedbackCategory>('other');
  const [description, setDescription] = useState('');
  const [includePerformance, setIncludePerformance] = useState(true);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [reportId, setReportId] = useState<string | null>(null);

  const submit = async () => {
    const trimmed = description.trim();
    if (!trimmed) {
      setError('発生した問題を入力してください');
      return;
    }
    setBusy(true);
    setError(null);
    setReportId(null);
    try {
      const nextReportId = await onSubmit({
        category,
        description: trimmed,
        includePerformance,
      });
      if (nextReportId) {
        setReportId(nextReportId);
      } else {
        setError('フィードバックを送信できませんでした');
      }
    } catch (submitError) {
      setError(String(submitError));
    } finally {
      setBusy(false);
    }
  };

  return (
    <Dialog.Root
      open={open}
      onOpenChange={(details) => {
        setOpen(details.open);
        if (details.open) {
          setError(null);
          setReportId(null);
        }
      }}
    >
      <Dialog.Trigger asChild>
        <Button size="xs" variant="outline">
          <CloudArrowUp size={16} weight="bold" />
          フィードバック
        </Button>
      </Dialog.Trigger>
      <Portal>
        <Dialog.Backdrop />
        <Dialog.Positioner>
          <Dialog.Content className={css({ maxW: 'lg', w: 'full' })}>
            <Dialog.CloseTrigger>
              <CloseButton />
            </Dialog.CloseTrigger>
            <Dialog.Header>
              <Dialog.Title>フィードバックを送信</Dialog.Title>
              <Dialog.Description>
                選択した対戦の安全な診断情報を添付します。プレイヤー名、IP、
                ルームコード、ファイルパスは送信しません。
              </Dialog.Description>
            </Dialog.Header>
            <Dialog.Body className={css({ display: 'grid', gap: '4' })}>
              <SelectField
                label="問題の種類"
                options={categoryOptions}
                value={category}
                onChange={(value) => setCategory(value as FeedbackCategory)}
              />
              <Field.Root invalid={Boolean(error && !description.trim())}>
                <Field.Label>発生した問題</Field.Label>
                <Textarea
                  maxLength={4000}
                  minH="32"
                  placeholder="何をしていたときに、何が起きたかを入力してください"
                  value={description}
                  onChange={(event: ChangeEvent<HTMLTextAreaElement>) =>
                    setDescription(event.target.value)
                  }
                />
                <Field.HelperText>
                  個人情報やルームコードは入力しないでください（
                  {description.length}/4000）
                </Field.HelperText>
              </Field.Root>
              <Switch.Root
                checked={includePerformance}
                onCheckedChange={(details) =>
                  setIncludePerformance(details.checked)
                }
                className={css({
                  alignItems: 'center',
                  display: 'flex',
                  justifyContent: 'space-between',
                })}
              >
                <Switch.HiddenInput />
                <div>
                  <Switch.Label>パフォーマンスログを含める</Switch.Label>
                  <p className={css({ color: 'fg.muted', textStyle: 'xs' })}>
                    フレーム時間、CPU時間、音声待ちなどを添付します。
                  </p>
                </div>
                <Switch.Control />
              </Switch.Root>
              <div
                className={css({
                  bg: 'gray.surface.bg',
                  borderColor: 'gray.surface.border',
                  borderRadius: 'l2',
                  borderWidth: '1px',
                  color: 'fg.muted',
                  p: '3',
                  textStyle: 'xs',
                })}
              >
                添付対象: 診断要約、端末環境、アプリエラー、接続状態、
                各プロセス出力の末尾
                {includePerformance ? '、パフォーマンスログ' : ''}
              </div>
              {error ? (
                <p className={css({ color: 'red.plain.fg', textStyle: 'sm' })}>
                  {error}
                </p>
              ) : null}
              {reportId ? (
                <div
                  className={css({
                    bg: 'green.subtle.bg',
                    borderColor: 'green.outline.border',
                    borderRadius: 'l2',
                    borderWidth: '1px',
                    color: 'green.subtle.fg',
                    p: '3',
                    textStyle: 'sm',
                  })}
                >
                  送信しました。レポートID: <strong>{reportId}</strong>
                </div>
              ) : null}
            </Dialog.Body>
            <Dialog.Footer>
              <Dialog.ActionTrigger asChild>
                <Button disabled={busy} variant="outline">
                  閉じる
                </Button>
              </Dialog.ActionTrigger>
              <Button
                disabled={busy || Boolean(reportId)}
                loading={busy}
                onClick={() => void submit()}
              >
                送信
              </Button>
            </Dialog.Footer>
          </Dialog.Content>
        </Dialog.Positioner>
      </Portal>
    </Dialog.Root>
  );
}
