import { WarningCircle } from '@phosphor-icons/react';
import { useEffect, useState } from 'react';
import { css } from 'styled-system/css';
import * as Alert from '@/components/ui/alert';
import { Button } from '@/components/ui/button';
import { Spinner } from '@/components/ui/spinner';
import type { RomGenerationError } from './types';

export function RomPreparationBanner({
  busy,
  error,
  onRetry,
  onSelectRom,
}: {
  busy: boolean;
  error: RomGenerationError | null;
  onRetry: () => Promise<void>;
  onSelectRom: () => Promise<void>;
}) {
  const [showProgress, setShowProgress] = useState(false);
  useEffect(() => {
    if (!busy) {
      setShowProgress(false);
      return;
    }
    // 再利用の確認だけで終わる起動では、バナーを点滅させない。
    const timer = window.setTimeout(() => setShowProgress(true), 400);
    return () => window.clearTimeout(timer);
  }, [busy]);

  if (busy ? !showProgress : !error) return null;
  return (
    <Alert.Root
      role={busy ? 'status' : 'alert'}
      status={busy ? 'neutral' : 'error'}
      variant="surface"
    >
      <Alert.Icon className={css({ mt: '0.5' })}>
        {busy ? (
          <Spinner aria-hidden="true" size="sm" />
        ) : (
          <WarningCircle aria-hidden="true" size={20} />
        )}
      </Alert.Icon>
      <Alert.Content>
        <Alert.Title>
          {busy
            ? '対戦データを準備しています'
            : '対戦データを準備できませんでした'}
        </Alert.Title>
        <Alert.Description>
          {busy
            ? '完了すると対戦できます。準備中も履歴や設定を確認できます。'
            : '対戦を始めるには、準備を完了してください。'}
        </Alert.Description>
        {!busy && error ? (
          <>
            <p
              className={css({
                color: 'fg.muted',
                overflowWrap: 'anywhere',
                textStyle: 'sm',
              })}
            >
              {error.message}
            </p>
            <div
              className={css({
                display: 'flex',
                flexWrap: 'wrap',
                gap: '2',
                mt: '2',
              })}
            >
              {error.sourceRom ? (
                <Button
                  size="sm"
                  colorPalette="gray"
                  variant="subtle"
                  onClick={() => void onRetry()}
                >
                  再試行
                </Button>
              ) : null}
              <Button
                size="sm"
                colorPalette="gray"
                variant="plain"
                onClick={() => void onSelectRom()}
              >
                ROMを選び直す
              </Button>
            </div>
          </>
        ) : null}
      </Alert.Content>
    </Alert.Root>
  );
}
