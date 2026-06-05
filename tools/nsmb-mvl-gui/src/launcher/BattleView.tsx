import {
  Broadcast,
  Crown,
  Flag,
  Heart,
  Play,
  RadioButton,
  Star,
  Stop,
  Trophy,
  Users,
} from '@phosphor-icons/react';
import { css, cx } from 'styled-system/css';
import { token } from 'styled-system/tokens';
import playerLBadge from '../assets/player-l.png';
import playerMBadge from '../assets/player-m.png';
import { RoleButton, SelectField, TextField } from '../components/Fields';
import { SummaryItem } from '../components/SummaryItem';
import { Button, Tabs } from '../components/ui';
import { WebRtcDiagnosticsPanel } from '../components/WebRtcDiagnosticsPanel';
import type { CourseMode, FormState, Lives } from '../types';
import { InfoPanel, LauncherCard, SmallInfoCard } from './LauncherCards';
import {
  bigStarsOptions,
  courseOptions,
  livesOptions,
  winsOptions,
} from './options';
import type {
  DiagnosticsState,
  LauncherActions,
  LauncherSummary,
  UpdateFormField,
} from './types';

export function BattleView({
  actions,
  diagnostics,
  form,
  lastLogDir,
  summary,
  updateField,
}: {
  actions: Pick<
    LauncherActions,
    'copyRoomCode' | 'openLogDir' | 'startMatch' | 'stopMatch'
  >;
  diagnostics: DiagnosticsState;
  form: FormState;
  lastLogDir: string;
  summary: LauncherSummary;
  updateField: UpdateFormField;
}) {
  return (
    <Tabs.Content value="battle">
      <form
        className={css({
          display: 'grid',
          gap: '5',
          gridTemplateColumns: `minmax(0, 1fr) ${token('sizes.diagnostics')}`,
          '@media (max-width: 1380px)': {
            gridTemplateColumns: '1fr',
          },
        })}
        onSubmit={(event) => {
          event.preventDefault();
          if (summary.connectionActive) {
            void actions.stopMatch();
          } else {
            void actions.startMatch();
          }
        }}
      >
        <section
          className={css({
            display: 'grid',
            gap: '4',
          })}
        >
          <LauncherCard
            title="部屋コード"
            icon={<Flag size={24} weight="fill" />}
          >
            <div
              className={css({
                display: 'grid',
                gap: '6',
                gridTemplateColumns: 'minmax(220px, 0.85fr) minmax(0, 1.15fr)',
                '@media (max-width: 760px)': {
                  gridTemplateColumns: '1fr',
                },
              })}
            >
              <div
                className={css({
                  alignContent: 'start',
                  borderColor: 'gray.surface.border',
                  borderRightWidth: '1px',
                  display: 'grid',
                  gap: '3',
                  pr: '6',
                  '@media (max-width: 860px)': {
                    borderBottomWidth: '1px',
                    borderRightWidth: '0',
                    pb: '5',
                    pr: '0',
                  },
                })}
              >
                <input
                  className={css({
                    bg: 'transparent',
                    border: 'none',
                    color: 'fg.default',
                    fontWeight: 'black',
                    outline: 'none',
                    textStyle: '4xl',
                    w: 'full',
                    _placeholder: {
                      color: 'fg.subtle',
                    },
                  })}
                  value={form.roomCode}
                  maxLength={64}
                  placeholder="test-room"
                  autoComplete="off"
                  onChange={(event) =>
                    updateField('roomCode', event.target.value)
                  }
                />
              </div>

              <div
                className={css({
                  alignContent: 'start',
                  display: 'grid',
                  gap: '3',
                })}
              >
                <div
                  className={css({
                    alignItems: 'center',
                    color: 'fg.muted',
                    display: 'flex',
                    fontWeight: 'black',
                    gap: '2',
                    textStyle: 'sm',
                  })}
                >
                  モードを選択
                </div>
                <div
                  className={css({
                    display: 'grid',
                    gap: '3',
                    gridTemplateColumns: 'repeat(2, minmax(0, 1fr))',
                    '@media (max-width: 620px)': {
                      gridTemplateColumns: '1fr',
                    },
                  })}
                >
                  <RoleButton
                    active={form.role === 'host'}
                    icon={<Crown size={26} weight="fill" />}
                    title="ホスト"
                    subtitle="部屋を作成して待つ"
                    tone="red"
                    onClick={() => updateField('role', 'host')}
                  />
                  <RoleButton
                    active={form.role === 'client'}
                    icon={<Users size={26} weight="fill" />}
                    title="参加"
                    subtitle="部屋に参加する"
                    tone="green"
                    onClick={() => updateField('role', 'client')}
                  />
                </div>
              </div>
            </div>
          </LauncherCard>

          <LauncherCard
            title="ゲーム設定"
            icon={<Star size={24} weight="fill" />}
          >
            <div className={css({ display: 'grid', gap: '3' })}>
              <div
                className={css({
                  display: 'grid',
                  gap: '3',
                  gridTemplateColumns: 'repeat(4, minmax(0, 1fr))',
                  '@media (max-width: 1260px)': {
                    gridTemplateColumns: 'repeat(2, minmax(0, 1fr))',
                  },
                  '@media (max-width: 720px)': {
                    gridTemplateColumns: '1fr',
                  },
                })}
              >
                <SelectField
                  icon={<RadioButton size={18} />}
                  label="コース"
                  options={courseOptions}
                  value={form.courseMode}
                  onChange={(value) =>
                    updateField('courseMode', value as CourseMode)
                  }
                />
                <SelectField
                  icon={<Trophy size={18} weight="fill" />}
                  label="勝利数"
                  options={winsOptions}
                  value={String(form.wins)}
                  onChange={(value) => updateField('wins', Number(value))}
                />
                <SelectField
                  icon={<Star size={18} weight="fill" />}
                  label="ビッグスター"
                  options={bigStarsOptions}
                  value={String(form.bigStars)}
                  onChange={(value) => updateField('bigStars', Number(value))}
                />
                <SelectField
                  icon={<Heart size={18} weight="fill" />}
                  label="残機"
                  options={livesOptions}
                  value={form.lives}
                  onChange={(value) => updateField('lives', value as Lives)}
                />
              </div>
              <TextField
                label="Match seed"
                value={form.matchSeed}
                placeholder="ランダム時は空でも自動生成"
                onChange={(value) => updateField('matchSeed', value)}
              />
            </div>
          </LauncherCard>

          <div
            className={css({
              display: 'grid',
              gap: '3',
              justifyItems: 'center',
            })}
          >
            <button
              type="submit"
              className={cx(
                css({
                  borderRadius: 'l2',
                  borderWidth: '4px',
                  color: 'fg.default',
                  fontWeight: 'black',
                  minH: 'cta',
                  overflow: 'hidden',
                  px: '6',
                  py: '4',
                  position: 'relative',
                  textStyle: '2xl',
                  transition: 'common',
                  w: 'full',
                  focusVisibleRing: 'outside',
                  cursor: 'pointer',
                  '@media (min-width: 1280px)': {
                    minH: '20',
                    textStyle: '3xl',
                  },
                }),
                summary.connectionActive
                  ? css({
                      bg: 'gray.3',
                      borderColor: 'gray.3',
                      _hover: {
                        bg: 'red.subtle.bg',
                      },
                    })
                  : css({
                      bg: 'red.700',
                      borderColor: 'yellow.500',
                      _hover: {
                        bg: 'red.600',
                      },
                    }),
              )}
            >
              <span
                className={css({
                  alignItems: 'center',
                  display: 'flex',
                  gap: '4',
                  justifyContent: 'center',
                })}
              >
                {summary.connectionActive ? '停止' : '対戦を開始'}
                {summary.connectionActive ? (
                  <Stop size={34} weight="fill" />
                ) : (
                  <Play
                    className={css({
                      transition: 'transform',
                      '.group:hover &': {
                        transform: 'translateX(0.25rem)',
                      },
                    })}
                    size={34}
                    weight="fill"
                  />
                )}
              </span>
            </button>
          </div>

          <BattleLogPanel
            lastLogDir={lastLogDir}
            onOpenLogDir={() => void actions.openLogDir()}
          />
        </section>

        <aside
          className={css({
            alignContent: 'start',
            display: 'grid',
            gap: '4',
          })}
        >
          <InfoPanel
            icon={<Broadcast size={22} weight="bold" />}
            title="接続状況"
            badge={summary.connectionActive ? '良好' : '待機'}
            badgeTone={summary.connectionActive ? 'green' : 'slate'}
          >
            <SummaryItem
              label="接続状態"
              value={summary.connectionActive ? '接続中' : '未接続'}
            />
            <WebRtcDiagnosticsPanel
              diagnostics={diagnostics.bridgeDiagnostics}
              compact
            />
          </InfoPanel>

          <div
            className={css({
              display: 'grid',
              gap: '3',
              gridTemplateColumns: 'repeat(2, minmax(0, 1fr))',
              '@media (max-width: 560px)': {
                gridTemplateColumns: '1fr',
              },
            })}
          >
            <SmallInfoCard
              imageSrc={form.role === 'host' ? playerMBadge : playerLBadge}
              label="操作キャラ"
              value={form.role === 'host' ? 'Mario' : 'Luigi'}
            />
            <SmallInfoCard
              icon={<Flag size={30} weight="fill" />}
              label="起動ステージ"
              value={summary.selectedStageLabel}
              caption="0-4 決定"
            />
          </div>
        </aside>
      </form>
    </Tabs.Content>
  );
}

function BattleLogPanel({
  lastLogDir,
  onOpenLogDir,
}: {
  lastLogDir: string;
  onOpenLogDir: () => void;
}) {
  return (
    <LauncherCard title="通信ログ" icon={<Broadcast size={22} />}>
      <div className={css({ display: 'grid', gap: '3' })}>
        <div
          className={css({
            alignItems: 'center',
            display: 'flex',
            gap: '3',
            justifyContent: 'space-between',
          })}
        >
          <span
            className={css({
              color: 'fg.muted',
              fontWeight: 'bold',
              textStyle: 'sm',
            })}
          >
            Log directory
          </span>
          <Button
            variant="outline"
            disabled={!lastLogDir}
            onClick={onOpenLogDir}
          >
            ログを開く
          </Button>
        </div>
        <code
          className={css({
            bg: 'gray.surface.bg',
            borderColor: 'gray.surface.border',
            borderRadius: 'l2',
            borderWidth: '1px',
            color: 'fg.muted',
            fontFamily: 'mono',
            fontWeight: 'semibold',
            overflowWrap: 'anywhere',
            px: '3',
            py: '2',
            textStyle: 'xs',
          })}
        >
          {lastLogDir || 'not started'}
        </code>
      </div>
    </LauncherCard>
  );
}
