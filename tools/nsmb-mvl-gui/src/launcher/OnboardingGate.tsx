import {
  CheckCircle,
  GameController,
  HardDrives,
  UserCircle,
} from '@phosphor-icons/react';
import type { ReactNode } from 'react';
import { css } from 'styled-system/css';
import { TextField } from '../components/Fields';
import { Button, Dialog } from '../components/ui';
import type { FormState, StatusKind } from '../types';
import type {
  LauncherActions,
  OnboardingState,
  UpdateFormField,
} from './types';

function StepStatus({
  complete,
  pendingText,
}: {
  complete: boolean;
  pendingText: string;
}) {
  return (
    <span
      className={css({
        color: complete ? 'green.plain.fg' : 'fg.muted',
        fontWeight: 'bold',
        textStyle: 'xs',
      })}
    >
      {complete ? '完了' : pendingText}
    </span>
  );
}

function activityStatusColor(kind: StatusKind) {
  if (kind === 'ok') {
    return 'green.plain.fg';
  }
  if (kind === 'error') {
    return 'red.plain.fg';
  }
  if (kind === 'warn') {
    return 'yellow.plain.fg';
  }
  return 'fg.muted';
}

function StepRow({
  action,
  complete,
  description,
  disabled,
  icon,
  pendingText,
  title,
}: {
  action: ReactNode;
  complete: boolean;
  description: string;
  disabled?: boolean;
  icon: ReactNode;
  pendingText: string;
  title: string;
}) {
  return (
    <section
      className={css({
        alignItems: 'center',
        bg: 'gray.surface.bg',
        borderColor: complete ? 'green.outline.border' : 'gray.surface.border',
        borderRadius: 'l2',
        borderWidth: '1px',
        display: 'grid',
        gap: '4',
        gridTemplateColumns: {
          base: '1fr',
          md: 'auto minmax(0, 1fr) auto',
        },
        opacity: disabled ? '0.56' : '1',
        p: '4',
      })}
    >
      <div
        className={css({
          alignItems: 'center',
          bg: complete ? 'green.subtle.bg' : 'blue.subtle.bg',
          borderColor: complete
            ? 'green.outline.border'
            : 'blue.outline.border',
          borderRadius: 'l2',
          borderWidth: '1px',
          color: complete ? 'green.plain.fg' : 'blue.plain.fg',
          display: 'flex',
          h: '12',
          justifyContent: 'center',
          w: '12',
        })}
      >
        {complete ? <CheckCircle size={26} weight="bold" /> : icon}
      </div>
      <div
        className={css({
          display: 'grid',
          gap: '1',
          minW: '0',
        })}
      >
        <div
          className={css({
            alignItems: 'center',
            display: 'flex',
            flexWrap: 'wrap',
            gap: '2',
          })}
        >
          <h2
            className={css({
              color: 'fg.default',
              fontWeight: 'black',
              textStyle: 'lg',
            })}
          >
            {title}
          </h2>
          <StepStatus complete={complete} pendingText={pendingText} />
        </div>
        <p
          className={css({
            color: 'fg.muted',
            fontWeight: 'semibold',
            overflowWrap: 'anywhere',
            textStyle: 'sm',
          })}
        >
          {description}
        </p>
      </div>
      {action}
    </section>
  );
}

export function OnboardingGate({
  actions,
  activityStatus,
  form,
  onboarding,
  updateField,
}: {
  actions: Pick<
    LauncherActions,
    'openMelondsInputConfig' | 'savePlayerName' | 'selectBaseRomAndPrepare'
  >;
  activityStatus: { text: string; kind: StatusKind } | null;
  form: FormState;
  onboarding: OnboardingState;
  updateField: UpdateFormField;
}) {
  if (
    !onboarding.loaded ||
    (onboarding.romsPrepared &&
      onboarding.inputConfigOpened &&
      onboarding.playerNameConfigured)
  ) {
    return null;
  }

  return (
    <Dialog.Root open closeOnEscape={false} closeOnInteractOutside={false}>
      <Dialog.Backdrop />
      <Dialog.Positioner>
        <Dialog.Content
          className={css({
            maxW: '3xl',
            w: 'full',
          })}
        >
          <Dialog.Header>
            <div
              className={css({
                alignItems: 'center',
                display: 'flex',
                gap: '3',
              })}
            >
              <GameController
                className={css({ color: 'blue.plain.fg', flexShrink: '0' })}
                size={34}
                weight="fill"
              />
              <Dialog.Title
                className={css({
                  fontWeight: 'black',
                  textStyle: '2xl',
                })}
              >
                初回セットアップ
              </Dialog.Title>
            </div>
            <Dialog.Description>
              最初に、対戦で使うROM、melonDS側のボタン割り当て、公開ルームに表示する名前を設定してください。
            </Dialog.Description>
          </Dialog.Header>

          <Dialog.Body
            className={css({
              display: 'grid',
              gap: '5',
            })}
          >
            {activityStatus ? (
              <p
                className={css({
                  color: activityStatusColor(activityStatus.kind),
                  fontWeight: 'semibold',
                  overflowWrap: 'anywhere',
                  textStyle: 'sm',
                })}
              >
                {activityStatus.text}
              </p>
            ) : null}

            <div
              className={css({
                display: 'grid',
                gap: '3',
              })}
            >
              <StepRow
                action={
                  <div
                    className={css({
                      display: 'grid',
                      gap: '2',
                      gridTemplateColumns: {
                        base: '1fr',
                        md: 'minmax(0, 1fr) auto',
                      },
                      minW: { base: '0', md: '80' },
                    })}
                  >
                    <TextField
                      label="プレイヤーネーム"
                      value={form.hostName}
                      maxLength={32}
                      placeholder="Player"
                      onChange={(value) => updateField('hostName', value)}
                    />
                    <Button
                      type="button"
                      className={css({
                        alignSelf: 'end',
                      })}
                      onClick={() => void actions.savePlayerName()}
                    >
                      保存
                    </Button>
                  </div>
                }
                complete={onboarding.playerNameConfigured}
                description="公開ルームの一覧で相手に表示される名前です。あとから設定画面で変更できます。"
                icon={<UserCircle size={26} weight="fill" />}
                pendingText="未完了"
                title="プレイヤーネームを設定"
              />
              <StepRow
                action={
                  <Button
                    type="button"
                    loading={onboarding.romGenerationBusy}
                    loadingText="生成中"
                    onClick={() => void actions.selectBaseRomAndPrepare()}
                  >
                    <HardDrives size={20} weight="fill" />
                    ROMを選んで生成
                  </Button>
                }
                complete={onboarding.romsPrepared}
                description={
                  form.baseRomPath
                    ? form.baseRomPath
                    : '手元のベースROMを選択してください。選択すると、オンライン対戦用のROMが自動生成されます。'
                }
                icon={<HardDrives size={26} weight="fill" />}
                pendingText="未完了"
                title="オンライン対戦用ROMを生成"
              />
              <StepRow
                action={
                  <Button
                    type="button"
                    disabled={!onboarding.romsPrepared}
                    onClick={() => void actions.openMelondsInputConfig()}
                  >
                    <GameController size={20} weight="fill" />
                    入力設定を開く
                  </Button>
                }
                complete={onboarding.inputConfigOpened}
                description="melonDSの入力設定を開き、キーボードまたはコントローラーにボタンを割り当ててください。"
                disabled={!onboarding.romsPrepared}
                icon={<GameController size={26} weight="fill" />}
                pendingText={onboarding.romsPrepared ? '未完了' : 'ROM生成後'}
                title="melonDSの入力を設定"
              />
            </div>
          </Dialog.Body>
        </Dialog.Content>
      </Dialog.Positioner>
    </Dialog.Root>
  );
}
