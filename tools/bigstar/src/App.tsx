import { useEffect, useState } from 'react';
import { css } from 'styled-system/css';
import { AIReplayViewer } from '@/launcher/AIReplayViewer';
import {
  areAiDevToolsEnabled,
  currentRuntimeCapabilities,
} from './buildProfile';
import { AppTitlebar } from './components/AppTitlebar';
import { BattleView } from './launcher/BattleView';
import { CpuBattleView } from './launcher/CpuBattleView';
import { HistoryView } from './launcher/HistoryView';
import { LauncherShell } from './launcher/LauncherShell';
import { OnboardingGate } from './launcher/OnboardingGate';
import { SettingsView } from './launcher/SettingsView';
import { SoloTestView } from './launcher/SoloTestView';
import { useLauncherController } from './launcher/useLauncherController';

export function App() {
  const launcher = useLauncherController();
  const aiDevToolsEnabled = areAiDevToolsEnabled();
  const feedbackSubmissionEnabled =
    currentRuntimeCapabilities().feedbackSubmission;
  const [aiViewerMounted, setAiViewerMounted] = useState(
    launcher.activeView === 'ai',
  );
  useEffect(() => {
    if (launcher.activeView === 'ai') setAiViewerMounted(true);
  }, [launcher.activeView]);
  const onboardingMissing =
    launcher.onboarding.loaded &&
    (!launcher.onboarding.romsPrepared ||
      !launcher.onboarding.inputConfigOpened ||
      !launcher.onboarding.playerNameConfigured);
  const onboardingOpen = onboardingMissing && launcher.activeView !== 'ai';
  // 終わった対戦は「ロビーに戻る」で閉じるまで対戦画面に残す
  const [dismissedMatchId, setDismissedMatchId] = useState<string | null>(null);
  const showMatch =
    launcher.currentMatch !== null &&
    launcher.currentMatch.id !== dismissedMatchId;
  // Kiso で組み直した画面だけ、無地の背景と 808px の列で表示する
  const pageLayout =
    launcher.activeView === 'settings' ||
    (launcher.activeView === 'battle' && showMatch);

  return (
    <div className={css({ h: 'dvh', overflow: 'hidden' })}>
      <AppTitlebar />
      <div
        aria-hidden={onboardingOpen ? true : undefined}
        className={css({ h: '[calc(100dvh - 2rem)]', overflow: 'hidden' })}
        inert={onboardingOpen ? true : undefined}
      >
        <LauncherShell
          activeView={launcher.activeView}
          activityStatus={launcher.activityStatus}
          connectionStatus={launcher.connectionStatus}
          layout={pageLayout ? 'page' : 'panel'}
          onCheckForUpdate={() => void launcher.actions.checkForUpdate()}
          onViewChange={launcher.changeView}
          soloTestEnabled={currentRuntimeCapabilities().soloTest}
          aiDevToolsEnabled={aiDevToolsEnabled}
          romStatus={launcher.romStatus}
          updateBusy={launcher.updateBusy}
          updateStatus={launcher.updateStatus}
        >
          <BattleView
            actions={launcher.actions}
            connectionStatus={launcher.connectionStatus}
            form={launcher.form}
            matchmakingRooms={launcher.matchmakingRooms}
            currentMatch={launcher.currentMatch}
            onOpenHistory={() => launcher.changeView('history')}
            onReturnToLobby={() =>
              setDismissedMatchId(launcher.currentMatch?.id ?? null)
            }
            showMatch={showMatch}
            summary={launcher.summary}
            updateField={launcher.updateField}
          />
          {aiDevToolsEnabled && aiViewerMounted ? <AIReplayViewer /> : null}
          <CpuBattleView
            controller={launcher.soloTest}
            blocked={launcher.soloTestBlocked}
          />
          {currentRuntimeCapabilities().soloTest ? (
            <SoloTestView
              controller={launcher.soloTest}
              blocked={launcher.soloTestBlocked}
            />
          ) : null}
          <HistoryView
            onOpenLogDir={launcher.actions.openLogDir}
            onUploadLogArchive={
              feedbackSubmissionEnabled
                ? launcher.actions.uploadLogArchive
                : undefined
            }
          />
          <SettingsView
            actions={launcher.actions}
            form={launcher.form}
            romGenerationBusy={launcher.onboarding.romGenerationBusy}
            startup={launcher.startup}
            updateField={launcher.updateField}
          />
        </LauncherShell>
      </div>
      <OnboardingGate
        actions={launcher.actions}
        activeView={launcher.activeView}
        activityStatus={launcher.activityStatus}
        form={launcher.form}
        onboarding={launcher.onboarding}
        aiDevToolsEnabled={aiDevToolsEnabled}
        onOpenAi={() => launcher.changeView('ai')}
        updateField={launcher.updateField}
      />
    </div>
  );
}
