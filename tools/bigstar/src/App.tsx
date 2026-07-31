import { useEffect, useState } from 'react';
import { css } from 'styled-system/css';
import { AIReplayViewer } from '@/launcher/AIReplayViewer';
import {
  areAiDevToolsEnabled,
  currentRuntimeCapabilities,
} from './buildProfile';
import { AppTitlebar } from './components/AppTitlebar';
import { BattleView } from './launcher/BattleView';
import { HistoryView } from './launcher/HistoryView';
import { LauncherShell } from './launcher/LauncherShell';
import { OnboardingGate } from './launcher/OnboardingGate';
import { SettingsView } from './launcher/SettingsView';
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
          onCheckForUpdate={() => void launcher.actions.checkForUpdate()}
          onViewChange={launcher.changeView}
          aiDevToolsEnabled={aiDevToolsEnabled}
          romStatus={launcher.romStatus}
          updateBusy={launcher.updateBusy}
          updateStatus={launcher.updateStatus}
        >
          <BattleView
            actions={launcher.actions}
            diagnostics={{
              bridgeDiagnostics: launcher.bridgeDiagnostics,
              gameStateMismatch: launcher.gameStateMismatch,
            }}
            form={launcher.form}
            matchmakingRooms={launcher.matchmakingRooms}
            currentMatch={launcher.currentMatch}
            summary={launcher.summary}
            updateField={launcher.updateField}
          />
          {aiDevToolsEnabled && aiViewerMounted ? <AIReplayViewer /> : null}
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
            startup={launcher.startup}
            summary={launcher.summary}
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
