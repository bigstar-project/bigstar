import { type ReactNode, useEffect, useRef } from 'react';
import { css } from 'styled-system/css';
import * as Toast from '@/components/ui/toast';
import type { StatusKind } from '../types';
import { StatusDot, type StatusTone } from './StatusDot';

export type ActivityStatus = { text: string; kind: StatusKind };

// 進み具合の通知は同じトーストを書き換えるので、最後の表示から数えて消す
const visibleMs = 5000;
const errorVisibleMs = 10000;

const dotTone: Record<StatusKind, StatusTone> = {
  ok: 'success',
  idle: 'gray',
  warn: 'warning',
  error: 'danger',
};

/** 右下に操作の結果を出す。表示中のトーストがあれば、積まずに中身を書き換える */
export function ActivityToasts({
  children,
  status,
}: {
  children: ReactNode;
  status: ActivityStatus | null;
}) {
  return (
    <Toast.Provider limit={3}>
      {children}
      <ActivityStatusSync status={status} />
      <ActivityToaster />
    </Toast.Provider>
  );
}

function ActivityStatusSync({ status }: { status: ActivityStatus | null }) {
  const manager = Toast.useToastManager();
  const toastsRef = useRef(manager.toasts);
  toastsRef.current = manager.toasts;
  const lastIdRef = useRef<string | null>(null);
  const { add, update } = manager;

  useEffect(() => {
    if (!status) return;
    const options = {
      title: status.text,
      type: status.kind,
      priority: status.kind === 'error' ? 'high' : 'low',
      timeout: status.kind === 'error' ? errorVisibleMs : visibleMs,
    } as const;
    const lastId = lastIdRef.current;
    const open = toastsRef.current.some(
      (toast) => toast.id === lastId && toast.transitionStatus !== 'ending',
    );
    if (lastId && open) {
      update(lastId, options);
    } else {
      lastIdRef.current = add(options);
    }
  }, [add, status, update]);

  return null;
}

function ActivityToaster() {
  const { toasts } = Toast.useToastManager();
  return (
    <Toast.Portal>
      <Toast.Viewport aria-label="通知" className={viewportClass}>
        {toasts.map((toast) => (
          <Toast.Root key={toast.id} className={rootClass} toast={toast}>
            {/* タイトルの 1 行目（行の高さ 20px）の中央に点を合わせる */}
            <StatusDot
              className={css({ mt: '1.5' })}
              tone={
                dotTone[(toast.type as StatusKind | undefined) ?? 'idle'] ??
                'gray'
              }
            />
            <Toast.Content>
              <Toast.Title className={css({ textStyle: 'sm' })}>
                {toast.title}
              </Toast.Title>
              {toast.description ? (
                <Toast.Description>{toast.description}</Toast.Description>
              ) : null}
            </Toast.Content>
            <Toast.Close aria-label="閉じる" />
          </Toast.Root>
        ))}
      </Toast.Viewport>
    </Toast.Portal>
  );
}

const viewportClass = css({ bottom: '5', insetInlineEnd: '5' });

const rootClass = css({ overflowWrap: 'anywhere' });
