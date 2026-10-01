import { useEffect, useState } from 'react';
import { css } from 'styled-system/css';
import { AIReplayViewer } from '@/launcher/AIReplayViewer';
import {
  areAiDevToolsEnabled,
  currentRuntimeCapabilities,
} from './buildProfile';
import { AppTitlebar } from './components/AppTitlebar';
import type { ActivityStatus } from './launcher/ActivityToasts';
import { BattleView } from './launcher/BattleView';
import { CpuBattleView } from './launcher/CpuBattleView';
import { HistoryView } from './launcher/HistoryView';
import { LauncherShell } from './launcher/LauncherShell';
import { OnboardingGate } from './launcher/OnboardingGate';
import { RomPreparationBanner } from './launcher/RomPreparationBanner';
import { SettingsView } from './launcher/SettingsView';
import { SoloTestView } from './launcher/SoloTestView';
import { sidebarSession } from './launcher/sidebarSession';
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
  const onboardingMissing = launcher.onboardingRequired;
  // そろっても「ロビーへ進む」を押すまでは、初回セットアップの画面を出し続ける
  const [onboardingPending, setOnboardingPending] = useState(false);
  if (onboardingMissing && !onboardingPending) setOnboardingPending(true);
  const onboardingOpen =
    (onboardingMissing || onboardingPending) && launcher.activeView !== 'ai';
  // セットアップ中の結果はその画面で伝えたので、閉じたあとに通知として出さない
  const [statusAtOnboardingEnd, setStatusAtOnboardingEnd] =
    useState<ActivityStatus | null>(null);
  const toastStatus =
    onboardingOpen || launcher.activityStatus === statusAtOnboardingEnd
      ? null
      : launcher.activityStatus;
  // 終わった対戦は「ロビーに戻る」で閉じるまで対戦画面に残す
  const [dismissedMatchId, setDismissedMatchId] = useState<string | null>(null);
  const showMatch =
    launcher.currentMatch !== null &&
    launcher.currentMatch.id !== dismissedMatchId;
  // CPU 対戦を始められない原因が対戦画面にあるときは、そこへ移るボタンを出す
  const cpuBlockedAction = launcher.matchmakingRooms.hostedRoom
    ? {
        label: '待機中の部屋を見る',
        onClick: () => launcher.changeView('battle'),
      }
    : launcher.connectionStatus.active
      ? {
          label: '対戦画面を見る',
          onClick: () => launcher.changeView('battle'),
        }
      : null;

  const { hostedRoomId, rooms } = launcher.matchmakingRooms;

  return (
    <div
      className={css({
        display: 'flex',
        flexDirection: 'column',
        h: 'dvh',
        overflow: 'hidden',
      })}
    >
      {/* 初回セットアップには戻る先がないので、戻る・進むを出さない */}
      <AppTitlebar navigation={!onboardingOpen} />
      <div className={css({ flexGrow: '1', minH: '0' })}>
        <LauncherShell
          activeView={launcher.activeView}
          activityStatus={toastStatus}
          hidden={onboardingOpen}
          layout={launcher.activeView === 'ai' ? 'wide' : 'page'}
          onCheckForUpdate={() => void launcher.actions.checkForUpdate()}
          onViewChange={launcher.changeView}
          playerName={launcher.form.hostName.trim()}
          roomCount={
            rooms.filter((room) => room.room_id !== hostedRoomId).length
          }
          session={sidebarSession({
            connectionStatus: launcher.connectionStatus,
            currentMatch: launcher.currentMatch,
            hostedRoom: launcher.matchmakingRooms.hostedRoom,
            soloStatus: launcher.soloTest.status,
          })}
          soloTestEnabled={currentRuntimeCapabilities().soloTest}
          aiDevToolsEnabled={aiDevToolsEnabled}
          romStatus={null}
          preparationBanner={
            <RomPreparationBanner
              busy={launcher.romPreparation.busy}
              error={launcher.romPreparation.error}
              onRetry={launcher.romPreparation.retry}
              onSelectRom={launcher.actions.selectBaseRomAndPrepare}
            />
          }
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
            updateBusy={launcher.updateBusy}
            updateField={launcher.updateField}
          />
          {aiDevToolsEnabled && aiViewerMounted ? <AIReplayViewer /> : null}
          <CpuBattleView
            blocked={launcher.soloTestBlocked}
            blockedAction={cpuBlockedAction}
            controller={launcher.soloTest}
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
            romGenerationBusy={launcher.romPreparation.busy}
            startup={launcher.startup}
            updateField={launcher.updateField}
          />
        </LauncherShell>
        {onboardingOpen ? (
          <OnboardingGate
            actions={launcher.actions}
            activityStatus={launcher.activityStatus}
            aiDevToolsEnabled={aiDevToolsEnabled}
            form={launcher.form}
            onboarding={launcher.onboarding}
            onFinish={() => {
              setOnboardingPending(false);
              setStatusAtOnboardingEnd(launcher.activityStatus);
              launcher.changeView('battle');
            }}
            onOpenAi={() => launcher.changeView('ai')}
            updateField={launcher.updateField}
          />
        ) : null}
      </div>
    </div>
  );
}
