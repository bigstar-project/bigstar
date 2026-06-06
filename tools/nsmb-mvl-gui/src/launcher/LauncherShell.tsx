import { Flag, FlagCheckered, Gear, Wrench } from '@phosphor-icons/react';
import type { ReactNode } from 'react';
import { css, cx } from 'styled-system/css';
import { token } from 'styled-system/tokens';
import launcherBg from '../assets/launcher-bg.png';
import { StatusPill } from '../components/StatusPill';
import { Button, Tabs } from '../components/ui';
import type { StatusKind } from '../types';
import type { UpdateStatus, View } from './types';

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

export function LauncherShell({
  activeView,
  activityStatus,
  children,
  connectionStatus,
  onCheckForUpdate,
  onViewChange,
  updateBusy,
  updateStatus,
}: {
  activeView: View;
  activityStatus: { text: string; kind: StatusKind } | null;
  children: ReactNode;
  connectionStatus: { text: string; kind: StatusKind };
  onCheckForUpdate: () => void;
  onViewChange: (view: View) => void;
  updateBusy: boolean;
  updateStatus: UpdateStatus;
}) {
  return (
    <Tabs.Root
      className={css({
        backgroundImage: `linear-gradient(180deg, rgba(3, 10, 20, 0.36) 0%, rgba(3, 10, 20, 0.22) 42%, rgba(3, 10, 20, 0.58) 100%), url(${launcherBg})`,
        backgroundAttachment: 'fixed',
        backgroundPosition: 'center',
        backgroundRepeat: 'no-repeat',
        backgroundSize: 'cover',
        color: 'fg.default',
        minH: 'screen',
        w: 'full',
      })}
      orientation="vertical"
      value={activeView}
      onValueChange={(details) => onViewChange(details.value as View)}
    >
      <main
        className={css({
          display: 'grid',
          gridTemplateColumns: `${token('sizes.sidebar')} minmax(0, 1fr)`,
          minH: 'screen',
          w: 'full',
          '@media (max-width: 1280px)': {
            gridTemplateColumns: `${token('sizes.sidebarCompact')} minmax(0, 1fr)`,
          },
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
            h: 'screen',
            px: '4',
            py: '6',
            position: 'sticky',
            top: '0',
            '@media (max-width: 1280px)': {
              px: '3',
            },
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
                gap: '8',
              })}
            >
              <div
                className={css({
                  display: 'grid',
                  gap: '1',
                  px: '2',
                  '@media (max-width: 1280px)': {
                    justifyItems: 'center',
                  },
                })}
              >
                <div
                  className={css({
                    color: 'fg.default',
                    fontWeight: 'black',
                    lineHeight: 'none',
                    textStyle: '3xl',
                    '@media (max-width: 1280px)': {
                      textStyle: 'xl',
                    },
                  })}
                >
                  NSMB
                </div>
                <div
                  className={css({
                    color: 'fg.default',
                    fontWeight: 'black',
                    lineHeight: 'none',
                    textStyle: '3xl',
                    '@media (max-width: 1280px)': {
                      textStyle: 'xl',
                    },
                  })}
                >
                  <span className={css({ color: 'red.plain.fg' })}>M</span>
                  <span className={css({ color: 'blue.plain.fg' })}>v</span>
                  <span className={css({ color: 'green.plain.fg' })}>L</span>
                </div>
                <div
                  className={css({
                    color: 'blue.plain.fg',
                    fontWeight: 'bold',
                    opacity: '0.8',
                    textStyle: 'xs',
                    '@media (max-width: 1280px)': {
                      display: 'none',
                    },
                  })}
                >
                  Mario vs Luigi Online
                </div>
              </div>

              <Tabs.List
                className={css({
                  display: 'grid',
                  gap: '3',
                })}
              >
                <Tabs.Trigger
                  aria-label="対戦"
                  className={css({
                    alignItems: 'center',
                    borderColor: 'transparent',
                    borderRadius: 'l2',
                    borderWidth: '1px',
                    color: 'fg.muted',
                    display: 'flex',
                    fontWeight: 'black',
                    gap: '3',
                    minH: '14',
                    outline: 'none',
                    px: '3',
                    textAlign: 'left',
                    transition: 'common',
                    _hover: {
                      bg: 'blue.subtle.bg',
                      borderColor: 'blue.outline.border',
                    },
                    '&[data-selected]': {
                      bg: 'blue.subtle.bg',
                      borderColor: 'blue.solid.bg',
                      color: 'fg.default',
                    },
                    '&[data-selected] svg': {
                      color: 'yellow.plain.fg',
                    },
                    '@media (max-width: 1280px)': {
                      justifyContent: 'center',
                    },
                  })}
                  value="battle"
                >
                  <FlagCheckered
                    className={css({
                      color: 'fg.muted',
                      flexShrink: '0',
                    })}
                    size={28}
                    weight="fill"
                  />
                  <span
                    className={css({
                      textStyle: 'md',
                      '@media (max-width: 1280px)': {
                        display: 'none',
                      },
                    })}
                  >
                    対戦
                  </span>
                </Tabs.Trigger>
                <Tabs.Trigger
                  aria-label="設定"
                  className={css({
                    alignItems: 'center',
                    borderColor: 'transparent',
                    borderRadius: 'l2',
                    borderWidth: '1px',
                    color: 'fg.muted',
                    display: 'flex',
                    fontWeight: 'black',
                    gap: '3',
                    minH: '14',
                    outline: 'none',
                    px: '3',
                    textAlign: 'left',
                    transition: 'common',
                    _hover: {
                      bg: 'blue.subtle.bg',
                      borderColor: 'blue.outline.border',
                    },
                    '&[data-selected]': {
                      bg: 'blue.subtle.bg',
                      borderColor: 'blue.solid.bg',
                      color: 'fg.default',
                    },
                    '&[data-selected] svg': {
                      color: 'yellow.plain.fg',
                    },
                    '@media (max-width: 1280px)': {
                      justifyContent: 'center',
                    },
                  })}
                  value="settings"
                >
                  <Gear
                    className={css({
                      color: 'fg.muted',
                      flexShrink: '0',
                    })}
                    size={28}
                    weight="fill"
                  />
                  <span
                    className={css({
                      textStyle: 'md',
                      '@media (max-width: 1280px)': {
                        display: 'none',
                      },
                    })}
                  >
                    設定
                  </span>
                </Tabs.Trigger>
              </Tabs.List>
            </div>
            <Button
              type="button"
              className={cx(
                css({
                  fontWeight: 'black',
                  maxW: 'full',
                  '@media (max-width: 1280px)': {
                    minW: '14',
                  },
                }),
                updateButtonClass(updateStatus),
              )}
              disabled={updateBusy}
              title={
                updateStatus.version ? `v${updateStatus.version}` : '更新を確認'
              }
              onClick={onCheckForUpdate}
            >
              <Wrench
                className={css({ flexShrink: '0' })}
                size={20}
                weight="bold"
              />
              <span
                className={css({
                  textStyle: 'md',
                  '@media (max-width: 1280px)': {
                    display: 'none',
                  },
                })}
              >
                {updateButtonLabel(updateStatus)}
              </span>
            </Button>
          </div>
        </aside>

        <div
          className={css({
            backgroundImage:
              'linear-gradient(180deg, rgba(7, 17, 31, 0.5) 0%, rgba(10, 21, 38, 0.38) 58%, rgba(6, 11, 20, 0.58) 100%)',
            minW: '0',
          })}
        >
          <div
            className={css({
              display: 'grid',
              gap: '6',
              maxW: 'contentMax',
              mx: 'auto',
              px: '7',
              py: '7',
              w: 'full',
              '@media (max-width: 1280px)': {
                px: '5',
              },
              '@media (max-width: 720px)': {
                px: '4',
              },
            })}
          >
            <header
              className={css({
                alignItems: 'flex-start',
                display: 'flex',
                gap: '5',
                justifyContent: 'space-between',
                '@media (max-width: 720px)': {
                  display: 'grid',
                },
              })}
            >
              <div
                className={css({
                  display: 'grid',
                  gap: '1',
                })}
              >
                <div
                  className={css({
                    alignItems: 'center',
                    display: 'flex',
                    gap: '3',
                  })}
                >
                  {activeView === 'battle' ? (
                    <Flag
                      className={css({ color: 'red.plain.fg' })}
                      size={36}
                      weight="fill"
                    />
                  ) : (
                    <Gear
                      className={css({ color: 'fg.muted' })}
                      size={36}
                      weight="fill"
                    />
                  )}
                  <h1
                    className={css({
                      color: 'fg.default',
                      fontWeight: 'black',
                      textStyle: '3xl',
                    })}
                  >
                    {activeView === 'battle' ? '対戦' : '設定'}
                  </h1>
                </div>
                <p
                  className={css({
                    color: 'fg.muted',
                    fontWeight: 'semibold',
                    textStyle: 'sm',
                  })}
                >
                  {activeView === 'battle'
                    ? 'オンラインでライバルと対戦しよう！'
                    : 'オンライン対戦の環境を整えましょう'}
                </p>
              </div>
              <div
                className={css({
                  alignItems: 'center',
                  display: 'flex',
                  gap: '3',
                  '@media (max-width: 720px)': {
                    flexWrap: 'wrap',
                  },
                })}
              >
                <StatusPill kind={connectionStatus.kind}>
                  {connectionStatus.text}
                </StatusPill>
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
