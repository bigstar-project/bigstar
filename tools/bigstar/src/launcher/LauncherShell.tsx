import { Brain, Flask } from '@phosphor-icons/react';
import type { ReactNode } from 'react';
import { useHotkeys } from 'react-hotkeys-hook';
import { css, cx } from 'styled-system/css';
import { Badge } from '@/components/ui/badge';
import { Kbd } from '@/components/ui/kbd';
import * as Tabs from '@/components/ui/tabs';
import launcherBg from '../assets/launcher-bg.png';
import { currentEditionConfig } from '../buildProfile';
import { AppTitlebar } from '../components/AppTitlebar';
import { type ActivityStatus, ActivityToasts } from './ActivityToasts';
import { PageHeader } from './PageHeader';
import {
  PixelClock,
  PixelRobot,
  PixelSliders,
  PixelStar,
  PixelVersus,
} from './PixelIcons';
import {
  RomCard,
  SessionCard,
  SidebarFooter,
  type SidebarSession,
  UpdateCard,
} from './SidebarStatus';
import type { UpdateStatus, View } from './types';

const viewOrder: View[] = ['battle', 'cpu', 'history', 'settings'];
const viewShortcuts = viewOrder.map((_, index) => `ctrl+${index + 1}`);

function viewTitle(view: View) {
  if (view === 'cpu') return 'CPU対戦';
  if (view === 'solo-test') return 'ひとり検証';
  if (view === 'battle') return '対戦';
  if (view === 'ai') return 'AI';
  if (view === 'history') return '履歴';
  return '設定';
}

export function LauncherShell({
  activeView,
  activityStatus,
  aiDevToolsEnabled = true,
  soloTestEnabled = false,
  children,
  inert = false,
  layout = 'panel',
  onCheckForUpdate,
  onViewChange,
  playerName = '',
  roomCount = 0,
  romStatus,
  session = null,
  updateBusy,
  updateStatus,
}: {
  activeView: View;
  activityStatus: ActivityStatus | null;
  aiDevToolsEnabled?: boolean;
  soloTestEnabled?: boolean;
  children: ReactNode;
  /** 初回セットアップ中は、タイトルバー以外を操作できなくする */
  inert?: boolean;
  /** page: Kiso で組んだ画面。背景を無地にして、本文を 808px の列に収める */
  layout?: 'panel' | 'page';
  onCheckForUpdate: () => void;
  onViewChange: (view: View) => void;
  playerName?: string;
  /** 募集中の部屋の数。0 なら対戦タブに数を出さない */
  roomCount?: number;
  romStatus: ActivityStatus | null;
  session?: SidebarSession | null;
  updateBusy: boolean;
  updateStatus: UpdateStatus;
}) {
  const edition = currentEditionConfig();
  const pageLayout = layout === 'page';
  const inertProps = inert
    ? ({ 'aria-hidden': true, inert: true } as const)
    : undefined;

  useHotkeys(
    viewShortcuts,
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
    <ActivityToasts status={activityStatus}>
      <Tabs.Root
        className={css({
          display: 'grid',
          gap: '0',
          gridTemplateColumns: '[token(sizes.sidebar) minmax(0, 1fr)]',
          h: 'full',
          overflow: 'hidden',
        })}
        orientation="vertical"
        value={activeView}
        // line だとリストの左に線が出るので、背景で選択を示す subtle にする
        variant="subtle"
        onValueChange={(value) => onViewChange(value as View)}
      >
        <aside
          className={css({
            bg: 'app.sidebar',
            borderRightColor: 'gray.3',
            borderRightWidth: '1px',
            display: 'flex',
            flexDirection: 'column',
            h: 'full',
            minH: '0',
            overflowY: 'auto',
            pb: '4',
            pt: '5',
            px: '3',
          })}
          {...inertProps}
        >
          <div
            className={css({
              alignItems: 'center',
              display: 'flex',
              gap: '2.5',
              pt: '0.5',
              px: '2.5',
              userSelect: 'none',
              '& > *': { pointerEvents: 'none' },
            })}
            data-tauri-drag-region
          >
            <PixelStar
              className={css({ color: 'amber.9', flexShrink: '0' })}
              eyeColor="var(--colors-app-sidebar)"
            />
            <span
              className={css({
                display: 'flex',
                flexDirection: 'column',
                gap: '1',
              })}
              data-testid="brand"
            >
              <span
                className={css({
                  fontFamily: 'display',
                  fontSize: 'xl',
                  letterSpacing: 'wider',
                  lineHeight: 'none',
                })}
              >
                BIGSTAR
              </span>
              {edition.edition === 'insiders' ? (
                <span
                  className={css({
                    color: 'amber.9',
                    fontFamily: 'mono',
                    // 2xs（8px）だと読みにくいので、ロゴのこの文字だけトークン外の大きさにする
                    fontSize: '[9.5px]',
                    fontWeight: 'semibold',
                    letterSpacing: 'widest',
                    lineHeight: 'none',
                    textTransform: 'uppercase',
                  })}
                  data-testid="edition-badge"
                >
                  {edition.badge}
                </span>
              ) : null}
            </span>
          </div>

          <Tabs.List
            aria-label="メインメニュー"
            className={css({ gap: '0.5', mt: '8' })}
          >
            <NavTab
              icon={<PixelVersus />}
              label="対戦"
              shortcut={1}
              value="battle"
            >
              {roomCount > 0 ? (
                // タブの名前は「対戦」のままにし、部屋の数は対戦画面で読み上げる
                <Badge
                  aria-hidden="true"
                  colorPalette="gray"
                  data-nav-count
                  shape="full"
                >
                  {roomCount}
                </Badge>
              ) : null}
            </NavTab>
            <NavTab
              icon={<PixelRobot />}
              label="CPU対戦"
              shortcut={2}
              value="cpu"
            />
            <NavTab
              ariaLabel="対戦履歴"
              icon={<PixelClock />}
              label="履歴"
              shortcut={3}
              value="history"
            />
            <NavTab
              icon={<PixelSliders />}
              label="設定"
              shortcut={4}
              value="settings"
            />
            {/* ローカル限定のタブは、通常タブと区別できるよう設定の下に置く。 */}
            {soloTestEnabled ? (
              <NavTab
                icon={<Flask size={18} weight="fill" />}
                label="ひとり検証"
                value="solo-test"
              />
            ) : null}
            {aiDevToolsEnabled ? (
              <NavTab
                icon={<Brain size={18} weight="fill" />}
                label="AI"
                value="ai"
              />
            ) : null}
            <Tabs.Indicator />
          </Tabs.List>

          <div className={css({ flexGrow: '1', minH: '6' })} />

          {session ? (
            <SessionCard onViewChange={onViewChange} session={session} />
          ) : null}
          {romStatus ? <RomCard /> : null}
          <UpdateCard
            busy={updateBusy}
            onCheckForUpdate={onCheckForUpdate}
            updateStatus={updateStatus}
          />
          <SidebarFooter
            busy={updateBusy}
            onCheckForUpdate={onCheckForUpdate}
            playerName={playerName}
            updateStatus={updateStatus}
          />
        </aside>

        <div
          className={cx(
            css({
              display: 'flex',
              flexDirection: 'column',
              h: 'full',
              minH: '0',
              minW: '0',
            }),
            pageLayout ? css({ bg: 'canvas' }) : undefined,
          )}
          style={
            pageLayout
              ? undefined
              : {
                  backgroundImage: `linear-gradient(180deg, rgba(7, 17, 31, 0.62) 0%, rgba(10, 21, 38, 0.5) 58%, rgba(6, 11, 20, 0.7) 100%), url(${launcherBg})`,
                  backgroundPosition: 'center',
                  backgroundRepeat: 'no-repeat',
                  backgroundSize: 'cover',
                }
          }
        >
          <AppTitlebar />
          <div
            className={css({ flex: '1', minH: '0', overflowY: 'auto' })}
            {...inertProps}
          >
            <div
              className={
                pageLayout
                  ? css({
                      // 余白の内側で 808px を確保する
                      boxSizing: 'content-box',
                      display: 'grid',
                      gap: '6',
                      maxW: 'page',
                      mx: 'auto',
                      pb: '10',
                      pt: '3',
                      px: { base: '6', lg: '12' },
                    })
                  : css({
                      display: 'grid',
                      gap: '4',
                      maxW: 'contentMax',
                      mx: 'auto',
                      pb: '4',
                      pt: '3',
                      px: { base: '3', md: '4', xl: '5' },
                      w: 'full',
                    })
              }
            >
              {/* 対戦画面は見出しの横に部屋を作るボタンを置くので、自分で見出しを出す */}
              {activeView === 'battle' ? null : (
                <PageHeader title={viewTitle(activeView)} />
              )}
              {children}
            </div>
          </div>
        </div>
      </Tabs.Root>
    </ActivityToasts>
  );
}

function NavTab({
  ariaLabel,
  children,
  icon,
  label,
  shortcut,
  value,
}: {
  ariaLabel?: string;
  children?: ReactNode;
  icon: ReactNode;
  label: string;
  shortcut?: number;
  value: View;
}) {
  return (
    <Tabs.Tab
      aria-label={ariaLabel ?? label}
      className={navTabClass}
      value={value}
    >
      <span className={css({ display: 'flex', flexShrink: '0' })}>{icon}</span>
      <span className={css({ flexGrow: '1', textAlign: 'left' })}>{label}</span>
      {children}
      {shortcut ? (
        <Kbd className={shortcutClass} data-nav-shortcut size="sm">
          Ctrl+{shortcut}
        </Kbd>
      ) : null}
    </Tabs.Tab>
  );
}

// 選択中の背景は subtle の Tabs.Indicator が描く
const navTabClass = css({
  fontSize: 'sm',
  fontWeight: 'medium',
  gap: '3',
  h: '10',
  justifyContent: 'flex-start',
  px: '3',
  transition: 'colors',
  w: 'full',
  '& svg': { color: 'fg.subtle' },
  _hover: { bg: 'gray.a2', color: 'fg.default' },
  _selected: { fontWeight: 'semibold', '& svg': { color: 'amber.9' } },
  '&:hover [data-nav-count]': { display: 'none' },
  '&:hover [data-nav-shortcut]': { display: 'inline-flex' },
});

const shortcutClass = css({
  display: 'none',
  pointerEvents: 'none',
});
