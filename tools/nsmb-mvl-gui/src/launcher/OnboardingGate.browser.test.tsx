import { describe, expect, test, vi } from 'vitest';
import { render } from 'vitest-browser-react';
import { initialForm } from '../form';
import { OnboardingGate } from './OnboardingGate';

describe('初回セットアップゲート', () => {
  test('ロムと入力設定が完了するまでランチャーをブロックする', async () => {
    const selectBaseRomAndPrepare = vi.fn(async () => {});
    const openMelondsInputConfig = vi.fn(async () => {});

    const screen = await render(
      <OnboardingGate
        actions={{
          openMelondsInputConfig,
          selectBaseRomAndPrepare,
        }}
        activeView="battle"
        activityStatus={null}
        form={{ ...initialForm, baseRomPath: '' }}
        onboarding={{
          inputConfigOpened: false,
          loaded: true,
          romGenerationBusy: false,
          romsPrepared: false,
        }}
        onOpenAi={vi.fn()}
      />,
    );

    await expect
      .element(screen.getByRole('heading', { name: '初回セットアップ' }))
      .toBeVisible();
    await expect.element(screen.getByText('ROM生成後')).toBeVisible();
    await screen.getByRole('button', { name: 'ROMを選んで生成' }).click();

    expect(selectBaseRomAndPrepare).toHaveBeenCalledTimes(1);
    await expect
      .element(screen.getByRole('button', { name: '入力設定を開く' }))
      .toBeDisabled();
  });

  test('ロム準備後に入力設定を開けるようにする', async () => {
    const openMelondsInputConfig = vi.fn(async () => {});

    const screen = await render(
      <OnboardingGate
        actions={{
          openMelondsInputConfig,
          selectBaseRomAndPrepare: vi.fn(async () => {}),
        }}
        activeView="battle"
        activityStatus={{ kind: 'ok', text: '共通 ROM の準備が完了しました' }}
        form={{ ...initialForm, baseRomPath: 'C:\\roms\\base.nds' }}
        onboarding={{
          inputConfigOpened: false,
          loaded: true,
          romGenerationBusy: false,
          romsPrepared: true,
        }}
        onOpenAi={vi.fn()}
      />,
    );

    await expect
      .element(screen.getByText('共通 ROM の準備が完了しました'))
      .toBeVisible();
    await screen.getByRole('button', { name: '入力設定を開く' }).click();

    expect(openMelondsInputConfig).toHaveBeenCalledTimes(1);
  });

  test('セットアップ完了後は表示しない', async () => {
    const screen = await render(
      <OnboardingGate
        actions={{
          openMelondsInputConfig: vi.fn(async () => {}),
          selectBaseRomAndPrepare: vi.fn(async () => {}),
        }}
        activeView="battle"
        activityStatus={null}
        form={initialForm}
        onboarding={{
          inputConfigOpened: true,
          loaded: true,
          romGenerationBusy: false,
          romsPrepared: true,
        }}
        onOpenAi={vi.fn()}
      />,
    );

    await expect
      .element(screen.getByRole('heading', { name: '初回セットアップ' }))
      .not.toBeInTheDocument();
  });

  test('未セットアップでもAI開発へ移動できる', async () => {
    const onOpenAi = vi.fn();

    const screen = await render(
      <OnboardingGate
        actions={{
          openMelondsInputConfig: vi.fn(async () => {}),
          selectBaseRomAndPrepare: vi.fn(async () => {}),
        }}
        activeView="battle"
        activityStatus={null}
        form={initialForm}
        onboarding={{
          inputConfigOpened: false,
          loaded: true,
          romGenerationBusy: false,
          romsPrepared: false,
        }}
        onOpenAi={onOpenAi}
      />,
    );

    await screen.getByRole('button', { name: 'AI開発を開く' }).click();

    expect(onOpenAi).toHaveBeenCalledTimes(1);
  });
});
