import type { ComponentProps } from 'react';
import { describe, expect, test, vi } from 'vitest';
import { render } from 'vitest-browser-react';
import { initialForm } from '../form';
import { OnboardingGate } from './OnboardingGate';
import type { OnboardingState } from './types';

type GateProps = ComponentProps<typeof OnboardingGate>;

const notStarted: OnboardingState = {
  inputConfigOpened: false,
  loaded: true,
  playerNameConfigured: false,
  romError: null,
  romGenerationBusy: false,
  romsPrepared: false,
};

function gateProps(overrides: Partial<GateProps> = {}): GateProps {
  return {
    actions: {
      openMelondsInputConfig: vi.fn(async () => {}),
      prepareBaseRomFromPath: vi.fn(async () => {}),
      savePlayerName: vi.fn(async () => true),
      selectBaseRomAndPrepare: vi.fn(async () => {}),
    },
    activityStatus: null,
    form: { ...initialForm, baseRomPath: '', hostName: '' },
    onboarding: notStarted,
    onFinish: vi.fn(),
    onOpenAi: vi.fn(),
    updateField: vi.fn(),
    ...overrides,
  };
}

describe('初回セットアップ', () => {
  test('名前から順に進め、終わっていない手順の操作は出さない', async () => {
    const screen = await render(<OnboardingGate {...gateProps()} />);

    await expect
      .element(
        screen.getByRole('heading', { name: '対戦の前に、3つだけ準備します' }),
      )
      .toBeVisible();
    await expect
      .element(screen.getByLabelText('プレイヤーネーム'))
      .toBeVisible();
    await expect
      .element(screen.getByRole('button', { name: 'ROMを選んで生成' }))
      .not.toBeInTheDocument();
    await expect
      .element(screen.getByRole('button', { name: '入力設定を開く' }))
      .not.toBeInTheDocument();
    await expect
      .element(screen.getByRole('button', { name: 'ロビーへ進む' }))
      .toBeDisabled();
    await expect
      .element(screen.getByText('すべて完了するとロビーに進めます'))
      .toBeVisible();
  });

  test('プレイヤーネームを入力して保存できる', async () => {
    const props = gateProps();
    const screen = await render(<OnboardingGate {...props} />);

    await screen.getByLabelText('プレイヤーネーム').fill('Alice');
    await screen.getByRole('button', { name: '保存' }).click();

    expect(props.updateField).toHaveBeenCalledWith('hostName', 'Alice');
    expect(props.actions.savePlayerName).toHaveBeenCalledTimes(1);
  });

  test('保存できなかった理由を名前の手順の下に出す', async () => {
    const props = gateProps({
      // 操作の前から出ている状態は、この手順の結果として扱わない
      activityStatus: { kind: 'error', text: '前からあるエラー' },
      actions: {
        ...gateProps().actions,
        savePlayerName: vi.fn(async () => false),
      },
    });
    const screen = await render(<OnboardingGate {...props} />);

    await expect
      .element(screen.getByText('前からあるエラー'))
      .not.toBeInTheDocument();
    await screen.getByRole('button', { name: '保存' }).click();
    await screen.rerender(
      <OnboardingGate
        {...props}
        activityStatus={{
          kind: 'warn',
          text: 'プレイヤーネームを入力してください',
        }}
      />,
    );

    await expect
      .element(screen.getByRole('alert'))
      .toHaveTextContent('プレイヤーネームを入力してください');
  });

  test('保存した名前は「変更」から入力し直せる', async () => {
    const screen = await render(
      <OnboardingGate
        {...gateProps({
          form: { ...initialForm, hostName: 'Alice' },
          onboarding: { ...notStarted, playerNameConfigured: true },
        })}
      />,
    );

    const nameInput = screen.getByRole('textbox', { name: 'プレイヤーネーム' });
    await expect.element(screen.getByText('Alice')).toBeVisible();
    await expect.element(nameInput).not.toBeInTheDocument();
    await screen
      .getByRole('button', { name: 'プレイヤーネームを変更' })
      .click();

    await expect.element(nameInput).toHaveValue('Alice');
    // 名前を保存し直すまで、ほかの手順へは進めない
    await expect
      .element(screen.getByRole('button', { name: 'ROMを選んで生成' }))
      .not.toBeInTheDocument();
  });

  test('名前のあとは、ROMを選んで生成できる', async () => {
    const props = gateProps({
      onboarding: { ...notStarted, playerNameConfigured: true },
    });
    const screen = await render(<OnboardingGate {...props} />);

    await screen.getByRole('button', { name: 'ROMを選んで生成' }).click();

    expect(props.actions.selectBaseRomAndPrepare).toHaveBeenCalledTimes(1);
  });

  test('生成中は、使っているROMの名前と待つことを伝える', async () => {
    const screen = await render(
      <OnboardingGate
        {...gateProps({
          form: { ...initialForm, baseRomPath: 'D:\\roms\\nsmb.nds' },
          onboarding: {
            ...notStarted,
            playerNameConfigured: true,
            romGenerationBusy: true,
          },
        })}
      />,
    );

    const status = screen.getByRole('status');
    await expect.element(status).toHaveTextContent('基準セーブを初期化中');
    await expect.element(status).toHaveTextContent('nsmb.nds');
    await expect
      .element(screen.getByRole('button', { name: 'ROMを選んで生成' }))
      .not.toBeInTheDocument();
  });

  test('対応していないROMなら、使えるROMを伝えて選び直せる', async () => {
    const props = gateProps({
      onboarding: {
        ...notStarted,
        playerNameConfigured: true,
        romError: {
          message: '対応していないROMです: D:\\roms\\nsmb_jp.nds (SHA-256 00)',
          sourceRom: 'D:\\roms\\nsmb_jp.nds',
        },
      },
    });
    const screen = await render(<OnboardingGate {...props} />);

    const alert = screen.getByRole('alert');
    await expect.element(alert).toHaveTextContent('対応していないROMです');
    await expect
      .element(alert)
      .toHaveTextContent(
        '使えるのは北米版『New Super Mario Bros.』のROMだけです。',
      );
    await expect.element(alert).toHaveTextContent('D:\\roms\\nsmb_jp.nds');
    await expect.element(alert).not.toHaveTextContent('SHA-256');
    await screen.getByRole('button', { name: '選び直す' }).click();

    expect(props.actions.selectBaseRomAndPrepare).toHaveBeenCalledTimes(1);
  });

  test('ほかの理由で生成できなければ、その理由をそのまま出す', async () => {
    const screen = await render(
      <OnboardingGate
        {...gateProps({
          onboarding: {
            ...notStarted,
            playerNameConfigured: true,
            romError: { message: 'ファイルを読めません', sourceRom: '' },
          },
        })}
      />,
    );

    const alert = screen.getByRole('alert');
    await expect.element(alert).toHaveTextContent('ROMを生成できませんでした');
    await expect.element(alert).toHaveTextContent('ファイルを読めません');
  });

  test('ROMのあとは、melonDSの入力設定を開ける', async () => {
    const props = gateProps({
      form: { ...initialForm, baseRomPath: 'D:\\roms\\nsmb.nds' },
      onboarding: {
        ...notStarted,
        playerNameConfigured: true,
        romsPrepared: true,
      },
    });
    const screen = await render(<OnboardingGate {...props} />);

    await expect.element(screen.getByText('D:\\roms\\nsmb.nds')).toBeVisible();
    await screen.getByRole('button', { name: '入力設定を開く' }).click();
    await screen.rerender(
      <OnboardingGate
        {...props}
        activityStatus={{ kind: 'error', text: 'melonDS が見つかりません' }}
      />,
    );

    expect(props.actions.openMelondsInputConfig).toHaveBeenCalledTimes(1);
    await expect
      .element(screen.getByRole('alert'))
      .toHaveTextContent('melonDS が見つかりません');
  });

  test('すべて終えたら、ロビーへ進める', async () => {
    const props = gateProps({
      form: {
        ...initialForm,
        baseRomPath: 'D:\\roms\\nsmb.nds',
        hostName: 'Alice',
      },
      onboarding: {
        ...notStarted,
        inputConfigOpened: true,
        playerNameConfigured: true,
        romsPrepared: true,
      },
    });
    const screen = await render(<OnboardingGate {...props} />);

    await expect.element(screen.getByText('準備ができました')).toBeVisible();
    await screen.getByRole('button', { name: 'ロビーへ進む' }).click();

    expect(props.onFinish).toHaveBeenCalledTimes(1);
  });

  test('未セットアップでもAI開発へ移動できる', async () => {
    const props = gateProps();
    const screen = await render(<OnboardingGate {...props} />);

    await screen.getByRole('button', { name: 'AI開発を開く' }).click();

    expect(props.onOpenAi).toHaveBeenCalledTimes(1);
  });

  test('AI開発機能が無効なビルドではAI開発ボタンを表示しない', async () => {
    const screen = await render(
      <OnboardingGate {...gateProps({ aiDevToolsEnabled: false })} />,
    );

    await expect
      .element(screen.getByRole('button', { name: 'AI開発を開く' }))
      .not.toBeInTheDocument();
  });
});
