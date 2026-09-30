import { type ReactNode, useEffect, useRef } from 'react';
import { css, cx } from 'styled-system/css';
import * as Toast from '@/components/ui/toast';
import type { StatusKind } from '../types';

export type ActivityStatus = { text: string; kind: StatusKind };

// 進み具合の通知は同じトーストを書き換えるので、最後の表示から数えて消す
const visibleMs = 5000;
const errorVisibleMs = 10000;

const dotClass: Record<StatusKind, string> = {
  ok: css({ bg: 'success.9' }),
  idle: css({ bg: 'gray.11' }),
  warn: css({ bg: 'warning.9' }),
  error: css({ bg: 'danger.9' }),
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
            <span
              aria-hidden="true"
              className={cx(
                dotClass[(toast.type as StatusKind | undefined) ?? 'idle'] ??
                  dotClass.idle,
                css({
                  borderRadius: 'full',
                  boxSize: '2',
                  flexShrink: '0',
                  mt: '[7px]',
                }),
              )}
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

const rootClass = css({
  bg: 'gray.3',
  borderColor: 'gray.5',
  borderRadius: 'l3',
  boxShadow: '[0 12px 32px rgba(0, 0, 0, 0.4)]',
  overflowWrap: 'anywhere',
  py: '3',
  pl: '4',
  pr: '3.5',
});
