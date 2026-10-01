import {
  ArrowRight,
  Check,
  FileArrowUp,
  WarningCircle,
} from '@phosphor-icons/react';
import { getCurrentWebview } from '@tauri-apps/api/webview';
import {
  type ReactNode,
  useEffect,
  useEffectEvent,
  useId,
  useState,
} from 'react';
import { css, cx } from 'styled-system/css';
import * as Alert from '@/components/ui/alert';
import { Button } from '@/components/ui/button';
import * as Field from '@/components/ui/field';
import { Input } from '@/components/ui/input';
import { Spinner } from '@/components/ui/spinner';
import { AppTitlebar } from '../components/AppTitlebar';
import type { FormState } from '../types';
import type { ActivityStatus } from './ActivityToasts';
import { Brand } from './Brand';
import { shortPath } from './path';
import type {
  LauncherActions,
  OnboardingState,
  RomGenerationError,
  UpdateFormField,
} from './types';

type StepState = 'done' | 'active' | 'locked';

/** 結果を手順の中に出す操作。ROM の生成は onboarding.romError で別に受け取る */
type StatusStep = 'name' | 'input';

/** 対応していない ROM を選んだときに、バックエンドが返す文言の頭 */
const unsupportedRomMessage = '対応していないROMです';

const playerNameMaxLength = 32;

/**
 * 初回起動で、ロビーの代わりに出す準備の画面。
 * 名前、ROM、入力設定の順に進め、すべて終えて「ロビーへ進む」を押すまで出し続ける
 */
export function OnboardingGate({
  actions,
  activityStatus,
  aiDevToolsEnabled = true,
  form,
  onboarding,
  onFinish,
  onOpenAi,
  updateField,
}: {
  actions: Pick<
    LauncherActions,
    | 'openMelondsInputConfig'
    | 'prepareBaseRomFromPath'
    | 'savePlayerName'
    | 'selectBaseRomAndPrepare'
  >;
  activityStatus: ActivityStatus | null;
  aiDevToolsEnabled?: boolean;
  form: FormState;
  onboarding: OnboardingState;
  onFinish: () => void;
  onOpenAi: () => void;
  updateField: UpdateFormField;
}) {
  const [editingName, setEditingName] = useState(false);
  // 操作した手順と、そのときの状態。あとから届いた警告とエラーだけを、その手順の下に出す
  const [statusMark, setStatusMark] = useState<{
    before: ActivityStatus | null;
    step: StatusStep;
  } | null>(null);

  const nameDone = onboarding.playerNameConfigured && !editingName;
  const romDone = onboarding.romsPrepared;
  const inputDone = onboarding.inputConfigOpened;
  const activeStep = !nameDone
    ? 'name'
    : !romDone
      ? 'rom'
      : !inputDone
        ? 'input'
        : null;
  const stepState = (step: 'name' | 'rom' | 'input', done: boolean) =>
    done ? 'done' : step === activeStep ? 'active' : 'locked';
  const ready = activeStep === null;

  const romDropEnabled = activeStep === 'rom' && !onboarding.romGenerationBusy;
  const romDragging = useNdsFileDrop(romDropEnabled, (path) => {
    void actions.prepareBaseRomFromPath(path);
  });

  const markStatus = (step: StatusStep) =>
    setStatusMark({ before: activityStatus, step });
  const stepError = (step: StatusStep) =>
    statusMark?.step === step &&
    activityStatus !== null &&
    activityStatus !== statusMark.before &&
    (activityStatus.kind === 'warn' || activityStatus.kind === 'error')
      ? activityStatus.text
      : null;

  return (
    <div
      className={css({
        bg: 'canvas',
        display: 'flex',
        flexDirection: 'column',
        h: 'full',
      })}
    >
      <AppTitlebar navigation={false} />
      <div
        className={css({
          display: 'flex',
          flex: '1',
          flexDirection: 'column',
          minH: '0',
          overflowY: 'auto',
          pb: '10',
          pt: '6',
          px: '12',
        })}
      >
        <main
          className={css({
            alignSelf: 'center',
            display: 'flex',
            flexGrow: '1',
            flexShrink: '0',
            flexDirection: 'column',
            // モックの 600px にいちばん近い既存の幅
            maxW: 'mainPanel',
            w: 'full',
          })}
        >
          <Brand eyeColor="var(--colors-canvas)" />
          <h1
            className={css({
              fontWeight: 'bold',
              mt: '10',
              textStyle: '2xl',
            })}
          >
            対戦の前に、3つだけ準備します
          </h1>
          <p className={css({ color: 'fg.muted', mt: '2', textStyle: 'sm' })}>
            どれもあとから設定画面で変更できます。
          </p>

          <ol
            className={css({
              display: 'flex',
              flexDirection: 'column',
              mt: '9',
            })}
          >
            <Step
              number={1}
              state={stepState('name', nameDone)}
              title="プレイヤーネームを設定"
            >
              {nameDone ? (
                <div
                  className={css({
                    alignItems: 'center',
                    display: 'flex',
                    gap: '2',
                    minW: '0',
                  })}
                >
                  <span
                    className={css({
                      fontWeight: 'semibold',
                      overflowWrap: 'anywhere',
                      textStyle: 'sm',
                    })}
                  >
                    {form.hostName.trim()}
                  </span>
                  <Button
                    aria-label="プレイヤーネームを変更"
                    // 名前より目立たせない
                    className={css({ color: 'fg.muted' })}
                    colorPalette="gray"
                    size="2xs"
                    variant="plain"
                    onClick={() => setEditingName(true)}
                  >
                    変更
                  </Button>
                </div>
              ) : activeStep === 'name' ? (
                <NameForm
                  error={stepError('name')}
                  value={form.hostName}
                  onChange={(value) => updateField('hostName', value)}
                  onSubmit={async () => {
                    markStatus('name');
                    if (await actions.savePlayerName()) setEditingName(false);
                  }}
                />
              ) : null}
            </Step>

            <Step
              number={2}
              state={stepState('rom', romDone)}
              title="オンライン対戦用ROMを生成"
            >
              {romDone ? (
                <p className={css({ color: 'fg.muted', textStyle: 'sm' })}>
                  {form.baseRomPath ? (
                    <>
                      <span
                        className={css({
                          color: 'fg.default',
                          fontFamily: 'mono',
                          overflowWrap: 'anywhere',
                          pr: '2',
                        })}
                      >
                        {form.baseRomPath}
                      </span>
                      から生成しました
                    </>
                  ) : (
                    '生成しました'
                  )}
                </p>
              ) : activeStep === 'rom' ? (
                <>
                  <StepDescription>
                    手元のベースROM（北米版『New Super Mario Bros.』）を選ぶと、
                    <br />
                    オンライン対戦用のROMを自動で作ります。
                  </StepDescription>
                  {onboarding.romGenerationBusy ? (
                    <RomBusy baseRomPath={form.baseRomPath} />
                  ) : onboarding.romError ? (
                    <RomErrorAlert
                      error={onboarding.romError}
                      onRetry={() => void actions.selectBaseRomAndPrepare()}
                    />
                  ) : (
                    <RomDropZone
                      dragging={romDragging}
                      onClick={() => void actions.selectBaseRomAndPrepare()}
                    />
                  )}
                </>
              ) : null}
            </Step>

            <Step
              last
              number={3}
              state={stepState('input', inputDone)}
              title="melonDS の入力を設定"
            >
              {inputDone ? null : (
                <StepDescription locked={activeStep !== 'input'}>
                  melonDS
                  の入力設定を開き、キーボードかコントローラーにボタンを割り当てます。
                </StepDescription>
              )}
              {activeStep === 'input' ? (
                <>
                  <div
                    className={css({
                      alignItems: 'center',
                      display: 'flex',
                      flexWrap: 'wrap',
                      gap: '3.5',
                      mt: '2',
                    })}
                  >
                    <Button
                      colorPalette="amber"
                      onClick={() => {
                        markStatus('input');
                        void actions.openMelondsInputConfig();
                      }}
                    >
                      入力設定を開く
                    </Button>
                    <span
                      className={css({ color: 'fg.subtle', textStyle: 'xs' })}
                    >
                      開くとこのステップは完了です
                    </span>
                  </div>
                  <StepError text={stepError('input')} />
                </>
              ) : null}
            </Step>
          </ol>

          <footer
            className={css({
              alignItems: 'center',
              borderTopWidth: '1px',
              display: 'flex',
              gap: '4',
              mt: 'auto',
              pt: '5',
            })}
          >
            {aiDevToolsEnabled ? (
              <Button
                colorPalette="gray"
                size="sm"
                variant="plain"
                onClick={onOpenAi}
              >
                AI開発を開く
              </Button>
            ) : null}
            <span
              aria-live="polite"
              className={css({
                color: 'fg.subtle',
                ml: 'auto',
                textStyle: 'sm',
              })}
            >
              {ready ? '準備ができました' : 'すべて完了するとロビーに進めます'}
            </span>
            <Button
              colorPalette="amber"
              disabled={!ready}
              size="lg"
              onClick={onFinish}
            >
              ロビーへ進む
              <ArrowRight weight="bold" />
            </Button>
          </footer>
        </main>
      </div>
    </div>
  );
}

function Step({
  children,
  last = false,
  number,
  state,
  title,
}: {
  children: ReactNode;
  last?: boolean;
  number: number;
  state: StepState;
  title: string;
}) {
  return (
    <li
      aria-current={state === 'active' ? 'step' : undefined}
      className={css({ display: 'flex', gap: '5' })}
    >
      <div
        className={css({
          alignItems: 'center',
          display: 'flex',
          flexDirection: 'column',
        })}
      >
        <span
          className={cx(
            css({
              alignItems: 'center',
              borderRadius: 'full',
              boxSize: '8',
              display: 'flex',
              flexShrink: '0',
              fontWeight: 'bold',
              justifyContent: 'center',
              textStyle: 'sm',
            }),
            state === 'done'
              ? css({ bg: 'success.subtle.bg', color: 'success.subtle.fg' })
              : state === 'active'
                ? css({ bg: 'amber.solid.bg', color: 'amber.solid.fg' })
                : css({
                    borderColor: 'gray.6',
                    borderWidth: '1.5px',
                    color: 'fg.subtle',
                  }),
          )}
        >
          {state === 'done' ? (
            <>
              <Check aria-hidden="true" size={14} weight="bold" />
              <span className={css({ srOnly: true })}>完了</span>
            </>
          ) : (
            number
          )}
        </span>
        {last ? null : (
          <span
            aria-hidden="true"
            className={css({
              borderColor: state === 'done' ? 'success.a5' : 'gray.4',
              borderLeftWidth: '1.5px',
              flex: '1',
              my: '1.5',
            })}
          />
        )}
      </div>
      <div
        className={css({
          display: 'flex',
          flex: '1',
          flexDirection: 'column',
          gap: '1.5',
          minW: '0',
          pb: '8',
        })}
      >
        <div
          className={css({ alignItems: 'center', display: 'flex', minH: '8' })}
        >
          <h2
            className={css({
              color:
                state === 'locked'
                  ? 'fg.subtle'
                  : state === 'done'
                    ? 'fg.muted'
                    : 'fg.default',
              fontWeight: 'bold',
              textStyle: 'md',
            })}
          >
            {title}
          </h2>
        </div>
        {children}
      </div>
    </li>
  );
}

function StepDescription({
  children,
  locked = false,
}: {
  children: ReactNode;
  locked?: boolean;
}) {
  return (
    <p
      className={css({
        color: locked ? 'fg.subtle' : 'fg.muted',
        textStyle: 'sm',
        // 折り返したときに、最後の行が 1、2 文字だけにならないようにする
        textWrap: '[pretty]',
      })}
    >
      {children}
    </p>
  );
}

function StepError({ text }: { text: string | null }) {
  if (!text) return null;
  return (
    <p
      className={css({
        color: 'fg.error',
        overflowWrap: 'anywhere',
        textStyle: 'sm',
      })}
      role="alert"
    >
      {text}
    </p>
  );
}

function NameForm({
  error,
  onChange,
  onSubmit,
  value,
}: {
  error: string | null;
  onChange: (value: string) => void;
  onSubmit: () => Promise<void>;
  value: string;
}) {
  const [busy, setBusy] = useState(false);

  return (
    <>
      <StepDescription>
        公開ルームの一覧で、相手に表示される名前です。あとから設定画面で変更できます。
      </StepDescription>
      <form
        className={css({ display: 'flex', gap: '2.5', mt: '2' })}
        onSubmit={(event) => {
          event.preventDefault();
          setBusy(true);
          void onSubmit().finally(() => setBusy(false));
        }}
      >
        <Field.Root className={css({ w: '72' })} invalid={error !== null}>
          <Input
            aria-label="プレイヤーネーム"
            maxLength={playerNameMaxLength}
            placeholder="Player"
            value={value}
            onChange={(event) => onChange(event.target.value)}
          />
        </Field.Root>
        <Button colorPalette="amber" loading={busy} type="submit">
          保存
        </Button>
      </form>
      <StepError text={error} />
    </>
  );
}

function RomDropZone({
  dragging,
  onClick,
}: {
  dragging: boolean;
  onClick: () => void;
}) {
  const hintId = useId();

  return (
    <button
      aria-describedby={hintId}
      aria-label="ROMを選んで生成"
      className={cx(
        css({
          alignItems: 'center',
          bg: 'gray.2',
          borderColor: 'gray.6',
          borderRadius: 'l3',
          borderStyle: 'dashed',
          borderWidth: '1.5px',
          cursor: 'pointer',
          display: 'flex',
          gap: '3.5',
          h: '24',
          justifyContent: 'center',
          mt: '2.5',
          transition: 'common',
          w: 'full',
          focusVisibleRing: 'outside',
          _hover: { bg: 'gray.3', borderColor: 'gray.7' },
        }),
        dragging
          ? css({
              bg: 'amber.a2',
              borderColor: 'amber.9',
              _hover: { bg: 'amber.a2', borderColor: 'amber.9' },
            })
          : undefined,
      )}
      type="button"
      onClick={onClick}
    >
      <FileArrowUp
        aria-hidden="true"
        className={css({ color: dragging ? 'amber.9' : 'fg.muted' })}
        size={22}
      />
      <span
        className={css({
          alignItems: 'flex-start',
          display: 'flex',
          flexDirection: 'column',
          gap: '0.5',
        })}
      >
        <span className={css({ fontWeight: 'bold', textStyle: 'md' })}>
          ROMを選んで生成
        </span>
        <span
          className={css({ color: 'fg.muted', textStyle: 'xs' })}
          id={hintId}
        >
          .nds ファイルをここにドロップしても選べます
        </span>
      </span>
    </button>
  );
}

function RomBusy({ baseRomPath }: { baseRomPath: string }) {
  return (
    <output
      className={css({
        bg: 'gray.2',
        borderRadius: 'l3',
        borderWidth: '1px',
        display: 'flex',
        flexDirection: 'column',
        gap: '2.5',
        mt: '2.5',
        px: '4.5',
        py: '4',
      })}
    >
      <div
        className={css({
          alignItems: 'center',
          display: 'flex',
          gap: '2.5',
          textStyle: 'sm',
        })}
      >
        <Spinner
          aria-hidden="true"
          className={css({ color: 'amber.9' })}
          size="sm"
        />
        <span className={css({ fontWeight: 'semibold' })}>
          基準セーブを初期化中
        </span>
        {baseRomPath ? (
          <span
            className={css({
              color: 'fg.subtle',
              fontFamily: 'mono',
              ml: 'auto',
              textStyle: 'xs',
            })}
          >
            {shortPath(baseRomPath)}
          </span>
        ) : null}
      </div>
      <span className={css({ color: 'fg.muted', textStyle: 'xs' })}>
        完了まで、このままお待ちください。
      </span>
    </output>
  );
}

function RomErrorAlert({
  error,
  onRetry,
}: {
  error: RomGenerationError;
  onRetry: () => void;
}) {
  const unsupported = error.message.includes(unsupportedRomMessage);

  return (
    <Alert.Root
      className={css({ mt: '2.5' })}
      role="alert"
      status="error"
      variant="surface"
    >
      <Alert.Icon>
        <WarningCircle weight="bold" />
      </Alert.Icon>
      <Alert.Content>
        <Alert.Title>
          {unsupported ? unsupportedRomMessage : 'ROMを生成できませんでした'}
        </Alert.Title>
        <Alert.Description className={css({ overflowWrap: 'anywhere' })}>
          {unsupported
            ? '使えるのは北米版『New Super Mario Bros.』のROMだけです。'
            : error.message}
        </Alert.Description>
        {error.sourceRom ? (
          <span
            className={css({
              color: 'fg.subtle',
              fontFamily: 'mono',
              overflowWrap: 'anywhere',
              textStyle: 'xs',
            })}
          >
            {error.sourceRom}
          </span>
        ) : null}
      </Alert.Content>
      <Button colorPalette="gray" size="sm" variant="subtle" onClick={onRetry}>
        選び直す
      </Button>
    </Alert.Root>
  );
}

function isTauriRuntime() {
  return '__TAURI_INTERNALS__' in window && !('__BIGSTAR_E2E__' in window);
}

function isNdsPath(path: string) {
  return path.toLowerCase().endsWith('.nds');
}

/**
 * ウィンドウへ .nds ファイルを落としたら、そのパスを渡す。
 * 戻り値は、.nds ファイルをウィンドウの上で持っている間だけ true
 */
function useNdsFileDrop(enabled: boolean, onDrop: (path: string) => void) {
  const [dragging, setDragging] = useState(false);
  const drop = useEffectEvent(onDrop);

  useEffect(() => {
    if (!enabled || !isTauriRuntime()) return;
    let disposed = false;
    let unlisten: (() => void) | undefined;
    void getCurrentWebview()
      .onDragDropEvent(({ payload }) => {
        if (payload.type === 'enter') {
          setDragging(payload.paths.some(isNdsPath));
        } else if (payload.type === 'leave') {
          setDragging(false);
        } else if (payload.type === 'drop') {
          setDragging(false);
          const path = payload.paths.find(isNdsPath);
          if (path) drop(path);
        }
      })
      .then((cleanup) => {
        if (disposed) cleanup();
        else unlisten = cleanup;
      })
      .catch(() => undefined);
    return () => {
      disposed = true;
      unlisten?.();
      setDragging(false);
    };
  }, [enabled]);

  return dragging;
}
