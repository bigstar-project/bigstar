import { Check, Copy } from '@phosphor-icons/react';
import { useEffect, useId, useRef, useState } from 'react';
import { css } from 'styled-system/css';
import { Button } from '@/components/ui/button';
import * as Collapsible from '@/components/ui/collapsible';
import * as Dialog from '@/components/ui/dialog';
import * as Field from '@/components/ui/field';
import * as Switch from '@/components/ui/switch';
import { Textarea } from '@/components/ui/textarea';
import { Toggle, ToggleGroup } from '@/components/ui/toggle';
import type { FeedbackCategory } from '../types';
import type { FeedbackInput } from './types';

const descriptionMaxLength = 4000;

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

/** 対戦ログの行から開く、問題の報告。その対戦の診断情報を添えて送る */
export function FeedbackDialog({
  matchDate,
  matchSummary,
  onSubmit,
}: {
  /** 「6/21 19:40」 */
  matchDate: string;
  /** 「Rival 戦 3–1」 */
  matchSummary: string;
  onSubmit: (feedback: FeedbackInput) => Promise<string | null>;
}) {
  const [open, setOpen] = useState(false);
  const [category, setCategory] = useState<FeedbackCategory>('other');
  const [description, setDescription] = useState('');
  const [includePerformance, setIncludePerformance] = useState(true);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [reportId, setReportId] = useState<string | null>(null);
  const categoryLabelId = useId();
  const performanceLabelId = useId();
  const performanceDescriptionId = useId();

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
        setError('送信できませんでした');
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
      size="lg"
      onOpenChange={(next) => {
        setOpen(next);
        if (next) {
          setError(null);
          setReportId(null);
        }
      }}
    >
      <Dialog.Trigger
        render={<Button colorPalette="gray" size="xs" variant="plain" />}
      >
        問題を報告
      </Dialog.Trigger>
      <Dialog.Portal>
        <Dialog.Backdrop />
        <Dialog.Popup>
          {reportId ? (
            <SentReport reportId={reportId} />
          ) : (
            <>
              <Dialog.Header className={css({ pr: '10' })}>
                <Dialog.Title>問題を報告</Dialog.Title>
                <Dialog.Description>
                  <span
                    className={css({
                      color: 'fg.default',
                      fontVariantNumeric: 'tabular-nums',
                      fontWeight: 'semibold',
                      pr: '2',
                    })}
                  >
                    {matchDate}
                  </span>
                  {matchSummary} の安全な診断情報を添付します。
                </Dialog.Description>
              </Dialog.Header>
              <Dialog.CloseTrigger aria-label="閉じる" />

              <Dialog.Body className={css({ gap: '5' })}>
                <div
                  className={css({
                    display: 'flex',
                    flexDirection: 'column',
                    gap: '2',
                  })}
                >
                  <span className={labelClass} id={categoryLabelId}>
                    問題の種類
                  </span>
                  <ToggleGroup
                    aria-labelledby={categoryLabelId}
                    className={css({ flexWrap: 'wrap', gap: '1.5' })}
                    colorPalette="gray"
                    value={[category]}
                    onValueChange={(next) => {
                      // 押し直しで選択が外れないように、空の選択は無視する
                      const [picked] = next;
                      if (picked !== undefined) setCategory(picked);
                    }}
                  >
                    {categoryOptions.map((option) => (
                      <Toggle
                        key={option.value}
                        pressedVariant="solid"
                        size="xs"
                        value={option.value}
                        variant="outline"
                      >
                        {option.label}
                      </Toggle>
                    ))}
                  </ToggleGroup>
                </div>

                <Field.Root
                  className={css({ gap: '2' })}
                  invalid={Boolean(error) && !description.trim()}
                >
                  <Field.Label className={labelClass}>発生した問題</Field.Label>
                  <Field.Control
                    render={
                      <Textarea
                        maxLength={descriptionMaxLength}
                        placeholder="何をしていたときに、何が起きたかを入力してください"
                        rows={4}
                        size="sm"
                      />
                    }
                    value={description}
                    onValueChange={setDescription}
                  />
                  <div
                    className={css({
                      display: 'flex',
                      gap: '3',
                      justifyContent: 'space-between',
                    })}
                  >
                    <Field.Description>
                      個人情報やルームコードは入力しないでください
                    </Field.Description>
                    <span
                      aria-hidden="true"
                      className={css({
                        color: 'fg.subtle',
                        fontVariantNumeric: 'tabular-nums',
                        textStyle: 'xs',
                        whiteSpace: 'nowrap',
                      })}
                    >
                      {description.length} / {descriptionMaxLength}
                    </span>
                  </div>
                </Field.Root>

                <div className={css({ borderBottomWidth: '1px' })}>
                  {/* 行のどこを押しても切り替わるように、行全体を Switch.Label にする */}
                  <Switch.Label
                    className={css({
                      alignItems: 'center',
                      borderTopWidth: '1px',
                      display: 'flex',
                      gap: '4',
                      justifyContent: 'space-between',
                      minH: '14',
                      py: '2',
                    })}
                  >
                    <span
                      className={css({
                        display: 'flex',
                        flexDirection: 'column',
                        gap: '0.5',
                      })}
                    >
                      <span className={labelClass} id={performanceLabelId}>
                        パフォーマンスログを含める
                      </span>
                      <span
                        className={css({ color: 'fg.muted', textStyle: 'xs' })}
                        id={performanceDescriptionId}
                      >
                        フレーム時間、CPU時間、音声待ちなどを添付します
                      </span>
                    </span>
                    <Switch.Root
                      aria-describedby={performanceDescriptionId}
                      aria-labelledby={performanceLabelId}
                      checked={includePerformance}
                      colorPalette="gray"
                      onCheckedChange={(checked) =>
                        setIncludePerformance(checked)
                      }
                    >
                      <Switch.Thumb />
                    </Switch.Root>
                  </Switch.Label>
                  <Collapsible.Root className={css({ borderTopWidth: '1px' })}>
                    <Collapsible.Trigger
                      className={css({
                        color: 'fg.muted',
                        fontWeight: 'normal',
                        h: '10',
                        py: '0',
                      })}
                    >
                      添付される内容
                      <span
                        className={css({
                          color: 'success.11',
                          ml: 'auto',
                          textStyle: 'xs',
                        })}
                      >
                        プレイヤー名・IP・部屋コード・ファイルパスは送りません
                      </span>
                      <Collapsible.Indicator />
                    </Collapsible.Trigger>
                    <Collapsible.Panel>
                      <p className={css({ textStyle: 'xs' })}>
                        診断要約、端末環境、アプリのエラー、接続状態、各プロセス出力の末尾
                        {includePerformance ? '、パフォーマンスログ' : ''}
                      </p>
                    </Collapsible.Panel>
                  </Collapsible.Root>
                </div>

                {error ? (
                  <p
                    className={css({ color: 'danger.11', textStyle: 'sm' })}
                    role="alert"
                  >
                    {error}
                  </p>
                ) : null}
              </Dialog.Body>

              <Dialog.Footer>
                <Dialog.Close
                  disabled={busy}
                  render={
                    <Button colorPalette="gray" variant="subtle">
                      キャンセル
                    </Button>
                  }
                />
                <Button
                  colorPalette="amber"
                  loading={busy}
                  onClick={() => void submit()}
                >
                  送信
                </Button>
              </Dialog.Footer>
            </>
          )}
        </Dialog.Popup>
      </Dialog.Portal>
    </Dialog.Root>
  );
}

const labelClass = css({ fontWeight: 'semibold', textStyle: 'sm' });

/** 送信のあと、入力欄の代わりに出す。レポート ID を控えられるようにする */
function SentReport({ reportId }: { reportId: string }) {
  const titleRef = useRef<HTMLHeadingElement>(null);
  const [copied, setCopied] = useState(false);

  // 送信ボタンが消えるので、見出しへ移って送信できたことを読み上げる
  useEffect(() => {
    titleRef.current?.focus();
  }, []);

  return (
    <div
      className={css({
        alignItems: 'center',
        display: 'flex',
        flexDirection: 'column',
        gap: '2.5',
        pb: '1',
        pt: '4',
        textAlign: 'center',
      })}
    >
      <span
        className={css({
          alignItems: 'center',
          bg: 'success.a3',
          borderRadius: 'full',
          boxSize: '12',
          color: 'success.11',
          display: 'flex',
          justifyContent: 'center',
        })}
      >
        <Check size={22} weight="bold" />
      </span>
      <Dialog.Title
        className={css({ mt: '1.5', outline: 'none' })}
        ref={titleRef}
        tabIndex={-1}
      >
        送信しました
      </Dialog.Title>
      <Dialog.Description>
        ご協力ありがとうございます。下記がこの報告のレポートIDです。
      </Dialog.Description>
      <span
        className={css({
          alignItems: 'center',
          bg: 'canvas',
          borderRadius: 'l2',
          borderWidth: '1px',
          display: 'flex',
          gap: '2',
          mt: '1',
          pl: '3',
          pr: '1.5',
          py: '1.5',
        })}
      >
        <code
          className={css({
            fontFamily: 'mono',
            fontWeight: 'semibold',
            textStyle: 'sm',
          })}
        >
          {reportId}
        </code>
        <Button
          aria-label={copied ? 'コピーしました' : 'レポートIDをコピー'}
          colorPalette="gray"
          size="2xs"
          square
          variant="subtle"
          onClick={() => {
            void navigator.clipboard.writeText(reportId).then(() => {
              setCopied(true);
            });
          }}
        >
          {copied ? <Check weight="bold" /> : <Copy />}
        </Button>
      </span>
      <Dialog.Close
        render={
          <Button
            className={css({ mt: '3.5' })}
            colorPalette="gray"
            variant="subtle"
          >
            閉じる
          </Button>
        }
      />
    </div>
  );
}
