import { BattleView } from './launcher/BattleView';
import { LauncherShell } from './launcher/LauncherShell';
import { SettingsView } from './launcher/SettingsView';
import { useLauncherController } from './launcher/useLauncherController';

export function App() {
  const launcher = useLauncherController();

  return (
    <LauncherShell
      activeView={launcher.activeView}
      onCheckForUpdate={() => void launcher.actions.checkForUpdate()}
      onViewChange={launcher.changeView}
      status={launcher.status}
      updateBusy={launcher.updateBusy}
      updateStatus={launcher.updateStatus}
    >
      <BattleView
        actions={launcher.actions}
        diagnostics={{ bridgeDiagnostics: launcher.bridgeDiagnostics }}
        form={launcher.form}
        lastLogDir={launcher.lastLogDir}
        summary={launcher.summary}
        updateField={launcher.updateField}
      />
      <SettingsView
        actions={launcher.actions}
        form={launcher.form}
        summary={launcher.summary}
        updateField={launcher.updateField}
      />
    </LauncherShell>
  );
}
