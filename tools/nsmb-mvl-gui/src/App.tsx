import { AIReplayViewer } from './launcher/AIReplayViewer';
import { BattleView } from './launcher/BattleView';
import { LauncherShell } from './launcher/LauncherShell';
import { OnboardingGate } from './launcher/OnboardingGate';
import { SettingsView } from './launcher/SettingsView';
import { useLauncherController } from './launcher/useLauncherController';

export function App() {
  const launcher = useLauncherController();
  const onboardingMissing =
    launcher.onboarding.loaded &&
    (!launcher.onboarding.romsPrepared ||
      !launcher.onboarding.inputConfigOpened);
  const onboardingOpen = onboardingMissing && launcher.activeView === 'battle';

  return (
    <>
      <div
        aria-hidden={onboardingOpen ? true : undefined}
        inert={onboardingOpen ? true : undefined}
      >
        <LauncherShell
          activeView={launcher.activeView}
          activityStatus={launcher.activityStatus}
          connectionStatus={launcher.connectionStatus}
          onCheckForUpdate={() => void launcher.actions.checkForUpdate()}
          onViewChange={launcher.changeView}
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
            lastLogDir={launcher.lastLogDir}
            matchmakingRooms={launcher.matchmakingRooms}
            summary={launcher.summary}
            updateField={launcher.updateField}
          />
          <AIReplayViewer />
          <SettingsView
            actions={launcher.actions}
            form={launcher.form}
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
        onOpenAi={() => launcher.changeView('ai')}
      />
    </>
  );
}
