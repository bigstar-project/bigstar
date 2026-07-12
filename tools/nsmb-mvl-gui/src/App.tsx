import { css } from 'styled-system/css';
import { AppTitlebar } from './components/AppTitlebar';
import { BattleView } from './launcher/BattleView';
import { HistoryView } from './launcher/HistoryView';
import { LauncherShell } from './launcher/LauncherShell';
import { OnboardingGate } from './launcher/OnboardingGate';
import { SettingsView } from './launcher/SettingsView';
import { useLauncherController } from './launcher/useLauncherController';

export function App() {
  const launcher = useLauncherController();
  const onboardingOpen =
    launcher.onboarding.loaded &&
    (!launcher.onboarding.romsPrepared ||
      !launcher.onboarding.inputConfigOpened ||
      !launcher.onboarding.playerNameConfigured);

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
          <HistoryView
            onCreateLogArchive={launcher.actions.createLogArchive}
            onOpenLogDir={launcher.actions.openLogDir}
            onUploadLogArchive={launcher.actions.uploadLogArchive}
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
        activityStatus={launcher.activityStatus}
        form={launcher.form}
        onboarding={launcher.onboarding}
        updateField={launcher.updateField}
      />
    </div>
  );
}
