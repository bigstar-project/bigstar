import type { SoloTestStatus } from '../bindings';
import { localPlayerSide, opponentPlayerSide } from '../matchHistory';
import { cpuOpponents } from './CpuBattleView';
import { matchWinsFor } from './MatchView';
import type { SidebarSession } from './SidebarStatus';
import type {
  BattleMatchRecord,
  ConnectionStatusState,
  MatchmakingRoomsState,
} from './types';

/** 進行中の対戦、公開中の部屋、CPU対戦のうち、サイドバーに出すものを 1 つ選ぶ */
export function sidebarSession({
  connectionStatus,
  currentMatch,
  hostedRoom,
  soloStatus,
}: {
  connectionStatus: ConnectionStatusState;
  currentMatch: BattleMatchRecord | null;
  hostedRoom: MatchmakingRoomsState['hostedRoom'];
  soloStatus: SoloTestStatus;
}): SidebarSession | null {
  if (connectionStatus.active) {
    if (connectionStatus.recoveryDeadlineMs !== undefined) {
      return {
        kind: 'reconnecting',
        deadlineMs: connectionStatus.recoveryDeadlineMs,
      };
    }
    if (currentMatch?.status === 'running') {
      const latest = currentMatch.stages.at(-1);
      const opponentSide = opponentPlayerSide(currentMatch);
      return {
        kind: 'match',
        opponentName: currentMatch.playerNames[opponentSide],
        opponentWins: matchWinsFor(latest, opponentSide),
        selfWins: matchWinsFor(latest, localPlayerSide(currentMatch)),
      };
    }
    return { kind: 'connecting' };
  }
  if (hostedRoom) {
    return { kind: 'hosting', sinceMs: hostedRoom.createdAtMs };
  }
  if (soloStatus.active) {
    const opponent = soloStatus.config?.cpu_opponent;
    if (opponent) {
      const name =
        cpuOpponents.find((entry) => entry.value === opponent)?.label ?? 'CPU';
      return { kind: 'cpu', opponentName: name };
    }
    return { kind: 'solo-test' };
  }
  return null;
}
