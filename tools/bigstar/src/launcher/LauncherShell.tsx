import {
  Brain,
  ClockCounterClockwise,
  Flag,
  FlagCheckered,
  Gear,
  Wrench,
} from '@phosphor-icons/react';
import type { ReactNode } from 'react';
import { useHotkeys } from 'react-hotkeys-hook';
import { css, cx } from 'styled-system/css';
import { token } from 'styled-system/tokens';
import launcherBg from '../assets/launcher-bg.png';
import { StatusPill } from '../components/StatusPill';
import { Button, Kbd, Tabs } from '../components/ui';
import type { StatusKind } from '../types';
import type { UpdateStatus, View } from './types';

const currentAppVersion = __BIGSTAR_GUI_VERSION__;
const viewOrder: View[] = ['battle', 'history', 'settings'];

const sidebarTabClass = css({
  borderRadius: 'l2',
  justifyContent: 'flex-start',
  textAlign: 'left',
  transition: 'colors',
  transitionProperty: 'colors',
  w: 'full',
  _hover: {
    bg: 'blue.outline.bg.hover',
  },
  '&:hover [data-sidebar-shortcut]': {
    opacity: '1',
  },
  '&[data-selected] svg': {
    color: 'yellow.plain.fg',
  },
  '&[data-selected]': {
    color: 'fg.default',
  },
});

const sidebarShortcutClass = css({
  color: 'fg.subtle',
  ml: 'auto',
  opacity: '0',
  pointerEvents: 'none',
  transition: 'common',
});

function updateButtonLabel(updateStatus: UpdateStatus) {
  if (updateStatus.phase === 'checking') {
    return '確認中';
  }
  if (updateStatus.phase === 'available') {
    return '更新あり';
  }
  if (updateStatus.phase === 'downloading') {
    return '取得中';
  }
  if (updateStatus.phase === 'installed') {
    return '再起動中';
  }
  if (updateStatus.phase === 'error') {
    return '更新失敗';
  }
  return '更新確認';
}

function updateButtonClass(updateStatus: UpdateStatus) {
  if (updateStatus.phase === 'available') {
    return css({
      bg: 'yellow.solid.bg',
      borderColor: 'yellow.outline.border',
      color: 'gray.1',
      _hover: { bg: 'yellow.solid.bg.hover' },
    });
  }
  if (updateStatus.phase === 'error') {
    return css({
      bg: 'red.subtle.bg',
      borderColor: 'red.outline.border',
      color: 'red.subtle.fg',
    });
  }
  if (
    updateStatus.phase === 'checking' ||
    updateStatus.phase === 'downloading'
  ) {
    return css({
      bg: 'blue.subtle.bg',
      borderColor: 'blue.outline.border',
      color: 'blue.subtle.fg',
    });
  }
  return css({
    bg: 'gray.surface.bg',
    borderColor: 'gray.surface.border',
    color: 'fg.default',
  });
}

function viewTitle(view: View) {
  if (view === 'battle') {
    return '対戦';
  }
  if (view === 'ai') {
    return 'AI';
  }
  if (view === 'history') {
    return '対戦履歴';
  }
  return '設定';
}

function viewIcon(view: View) {
  if (view === 'battle') {
    return (
      <Flag
        className={css({ color: 'blue.plain.fg' })}
        size={28}
        weight="fill"
      />
    );
  }
  if (view === 'ai') {
    return (
      <Brain
        className={css({ color: 'yellow.plain.fg' })}
        size={28}
        weight="fill"
      />
    );
  }
  if (view === 'history') {
    return (
      <ClockCounterClockwise
        className={css({ color: 'blue.plain.fg' })}
        size={28}
        weight="fill"
      />
    );
  }
  return (
    <Gear className={css({ color: 'blue.plain.fg' })} size={28} weight="fill" />
  );
}

export function LauncherShell({
  activeView,
  activityStatus,
  aiDevToolsEnabled = true,
  children,
  connectionStatus,
  onCheckForUpdate,
  onViewChange,
  romStatus,
  updateBusy,
  updateStatus,
}: {
  activeView: View;
  activityStatus: { text: string; kind: StatusKind } | null;
  aiDevToolsEnabled?: boolean;
  children: ReactNode;
  connectionStatus: { text: string; kind: StatusKind };
  onCheckForUpdate: () => void;
  onViewChange: (view: View) => void;
  romStatus: { text: string; kind: StatusKind } | null;
  updateBusy: boolean;
  updateStatus: UpdateStatus;
}) {
  useHotkeys(
    ['ctrl+1', 'ctrl+2', 'ctrl+3'],
    (event) => {
      const view = viewOrder[Number(event.key) - 1];
      if (view) onViewChange(view);
    },
    { enableOnFormTags: false, preventDefault: true },
    [onViewChange],
  );

  useHotkeys(
    ['ctrl+tab', 'ctrl+shift+tab'],
    (event) => {
      const currentIndex = viewOrder.indexOf(activeView);
      const direction = event.shiftKey ? -1 : 1;
      const nextIndex =
        (currentIndex + direction + viewOrder.length) % viewOrder.length;
      onViewChange(viewOrder[nextIndex]);
    },
    { enableOnFormTags: false, preventDefault: true },
    [activeView, onViewChange],
  );

  return (
    <Tabs.Root
      colorPalette="gray"
      className={css({
        backgroundImage: `linear-gradient(180deg, rgba(3, 10, 20, 0.36) 0%, rgba(3, 10, 20, 0.22) 42%, rgba(3, 10, 20, 0.58) 100%), url(${launcherBg})`,
        backgroundAttachment: 'fixed',
        backgroundPosition: 'center',
        backgroundRepeat: 'no-repeat',
        backgroundSize: 'cover',
        color: 'fg.default',
        h: 'full',
        overflow: 'hidden',
        w: 'full',
      })}
      orientation="vertical"
      size="md"
      value={activeView}
      variant="subtle"
      onValueChange={(details) => onViewChange(details.value as View)}
    >
      <main
        className={css({
          display: 'grid',
          gridTemplateColumns: `${token('sizes.sidebar')} minmax(0, 1fr)`,
          h: 'full',
          minH: '0',
          w: 'full',
        })}
        style={{
          backgroundAttachment: 'fixed',
          backgroundImage: `linear-gradient(180deg, rgba(3, 10, 20, 0.36) 0%, rgba(3, 10, 20, 0.22) 42%, rgba(3, 10, 20, 0.58) 100%), url(${launcherBg})`,
          backgroundPosition: 'center',
          backgroundRepeat: 'no-repeat',
          backgroundSize: 'cover',
        }}
      >
        <aside
          className={css({
            backdropBlur: 'sm',
            backdropFilter: 'auto',
            bg: 'app.sidebar',
            borderRightWidth: '1px',
            display: 'grid',
            h: 'full',
            px: '3',
            py: '4',
          })}
        >
          <div
            className={css({
              alignContent: 'space-between',
              display: 'grid',
            })}
          >
            <div
              className={css({
                display: 'grid',
                gap: '5',
              })}
            >
              <div
                className={css({
                  display: 'grid',
                  gap: '1',
                  px: '2',
                })}
              >
                <div
                  className={css({
                    color: 'fg.default',
                    fontWeight: 'black',
                    lineHeight: 'none',
                    textStyle: '2xl',
                  })}
                >
                  BIG
                </div>
                <div
                  className={css({
                    color: 'fg.default',
                    fontWeight: 'black',
                    lineHeight: 'none',
                    textStyle: '2xl',
                  })}
                >
                  <span className={css({ color: 'yellow.plain.fg' })}>
                    STAR
                  </span>
                </div>
                <div
                  className={css({
                    color: 'blue.plain.fg',
                    fontWeight: 'bold',
                    opacity: '0.8',
                    textStyle: 'xs',
                  })}
                >
                  ONLINE
                </div>
              </div>

              <Tabs.List
                className={css({
                  display: 'grid',
                  gap: '2',
                  position: 'relative',
                })}
              >
                <Tabs.Trigger
                  aria-label="対戦"
                  className={sidebarTabClass}
                  value="battle"
                >
                  <FlagCheckered
                    className={css({
                      flexShrink: '0',
                    })}
                    size={22}
                    weight="fill"
                  />
                  <span
                    className={css({
                      textStyle: 'sm',
                    })}
                  >
                    対戦
                  </span>
                  <Kbd
                    className={sidebarShortcutClass}
                    colorPalette="gray"
                    data-sidebar-shortcut
                    size="sm"
                    variant="surface"
                  >
                    Ctrl+1
                  </Kbd>
                </Tabs.Trigger>
                {aiDevToolsEnabled ? (
                  <Tabs.Trigger
                    aria-label="AI"
                    className={sidebarTabClass}
                    value="ai"
                  >
                    <Brain
                      className={css({
                        flexShrink: '0',
                      })}
                      size={22}
                      weight="fill"
                    />
                    <span
                      className={css({
                        textStyle: 'sm',
                      })}
                    >
                      AI
                    </span>
                  </Tabs.Trigger>
                ) : null}
                <Tabs.Trigger
                  aria-label="対戦履歴"
                  className={sidebarTabClass}
                  value="history"
                >
                  <ClockCounterClockwise
                    className={css({
                      flexShrink: '0',
                    })}
                    size={22}
                    weight="fill"
                  />
                  <span
                    className={css({
                      textStyle: 'sm',
                    })}
                  >
                    履歴
                  </span>
                  <Kbd
                    className={sidebarShortcutClass}
                    colorPalette="gray"
                    data-sidebar-shortcut
                    size="sm"
                    variant="surface"
                  >
                    Ctrl+2
                  </Kbd>
                </Tabs.Trigger>
                <Tabs.Trigger
                  aria-label="設定"
                  className={sidebarTabClass}
                  value="settings"
                >
                  <Gear
                    className={css({
                      flexShrink: '0',
                    })}
                    size={22}
                    weight="fill"
                  />
                  <span
                    className={css({
                      textStyle: 'sm',
                    })}
                  >
                    設定
                  </span>
                  <Kbd
                    className={sidebarShortcutClass}
                    colorPalette="gray"
                    data-sidebar-shortcut
                    size="sm"
                    variant="surface"
                  >
                    Ctrl+3
                  </Kbd>
                </Tabs.Trigger>
                <Tabs.Indicator className={css({ bg: 'blue.subtle.bg' })} />
              </Tabs.List>
            </div>
            <div
              className={css({
                display: 'grid',
                gap: '2',
                justifyItems: 'stretch',
              })}
            >
              <Button
                type="button"
                className={cx(
                  css({
                    fontWeight: 'black',
                    maxW: 'full',
                  }),
                  updateButtonClass(updateStatus),
                )}
                disabled={updateBusy}
                title={
                  updateStatus.version
                    ? `v${updateStatus.version}`
                    : '更新を確認'
                }
                onClick={onCheckForUpdate}
              >
                <Wrench
                  className={css({ flexShrink: '0' })}
                  size={16}
                  weight="bold"
                />
                <span
                  className={css({
                    textStyle: 'sm',
                  })}
                >
                  {updateButtonLabel(updateStatus)}
                </span>
              </Button>
              <div
                className={css({
                  color: 'fg.muted',
                  display: 'flex',
                  fontWeight: 'bold',
                  justifyContent: 'center',
                  textStyle: 'xs',
                })}
                title={`現在のバージョン v${currentAppVersion}`}
              >
                <span>v{currentAppVersion}</span>
              </div>
            </div>
          </div>
        </aside>

        <div
          className={css({
            backgroundImage:
              'linear-gradient(180deg, rgba(7, 17, 31, 0.5) 0%, rgba(10, 21, 38, 0.38) 58%, rgba(6, 11, 20, 0.58) 100%)',
            h: 'full',
            minW: '0',
            overflowY: 'auto',
          })}
        >
          <div
            className={css({
              display: 'grid',
              gap: '4',
              maxW: 'contentMax',
              mx: 'auto',
              px: { base: '3', md: '4', xl: '5' },
              py: '4',
              w: 'full',
            })}
          >
            <header
              className={css({
                alignItems: 'flex-start',
                display: { base: 'grid', md: 'flex' },
                gap: '3',
                justifyContent: 'space-between',
              })}
            >
              <div>
                <div
                  className={css({
                    alignItems: 'center',
                    display: 'flex',
                    gap: '2.5',
                  })}
                >
                  {viewIcon(activeView)}
                  <h1
                    className={css({
                      color: 'fg.default',
                      fontWeight: 'bold',
                      textStyle: '2xl',
                    })}
                  >
                    {viewTitle(activeView)}
                  </h1>
                </div>
              </div>
              <div
                className={css({
                  alignItems: 'center',
                  display: 'flex',
                  flexWrap: { base: 'wrap', md: 'nowrap' },
                  gap: '3',
                  justifyContent: { md: 'flex-end' },
                })}
              >
                <StatusPill kind={connectionStatus.kind}>
                  {connectionStatus.text}
                </StatusPill>
                {romStatus ? (
                  <StatusPill kind={romStatus.kind} loading>
                    {romStatus.text}
                  </StatusPill>
                ) : null}
                {activityStatus ? (
                  <StatusPill kind={activityStatus.kind}>
                    {activityStatus.text}
                  </StatusPill>
                ) : null}
              </div>
            </header>

            {children}
          </div>
        </div>
      </main>
    </Tabs.Root>
  );
}
