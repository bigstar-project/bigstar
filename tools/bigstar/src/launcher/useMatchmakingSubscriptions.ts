import { useEffect } from 'react';
import {
  type RoomSummary,
  subscribeHostRoomEvents,
  subscribeLobbyRooms,
} from '../matchmakingClient';

const RECONNECT_DELAY_MS = 5000;

export function useLobbyRoomsSubscription({
  enabled,
  onDisabled,
  onError,
  onLoadingChange,
  onSnapshot,
  signalUrl,
}: {
  enabled: boolean;
  onDisabled: () => void;
  onError: (error: unknown) => void;
  onLoadingChange: (loading: boolean) => void;
  onSnapshot: (rooms: RoomSummary[]) => void;
  signalUrl: string;
}) {
  useEffect(() => {
    if (!enabled) {
      onLoadingChange(false);
      onDisabled();
      return;
    }

    let disposed = false;
    let unsubscribe: (() => void) | null = null;
    let reconnectTimer: number | null = null;

    const connect = () => {
      if (disposed) return;
      onLoadingChange(true);
      unsubscribe = subscribeLobbyRooms(signalUrl, {
        onClose: () => {
          unsubscribe = null;
          if (!disposed) {
            reconnectTimer = window.setTimeout(connect, RECONNECT_DELAY_MS);
          }
        },
        onError: (error) => {
          if (!disposed) onError(error);
        },
        onOpen: () => {
          if (!disposed) onLoadingChange(false);
        },
        onSnapshot: (rooms) => {
          if (!disposed) {
            onLoadingChange(false);
            onSnapshot(rooms);
          }
        },
      });
    };

    connect();
    return () => {
      disposed = true;
      if (reconnectTimer !== null) window.clearTimeout(reconnectTimer);
      unsubscribe?.();
    };
  }, [enabled, onDisabled, onError, onLoadingChange, onSnapshot, signalUrl]);
}

export function useHostedRoomSubscription({
  enabled,
  onError,
  onJoined,
  roomId,
  signalUrl,
}: {
  enabled: boolean;
  onError: (error: unknown) => void;
  onJoined: (room: RoomSummary) => void;
  roomId: string;
  signalUrl: string;
}) {
  useEffect(() => {
    if (!enabled) return;

    let disposed = false;
    let unsubscribe: (() => void) | null = null;
    let reconnectTimer: number | null = null;

    const connect = () => {
      if (disposed) return;
      unsubscribe = subscribeHostRoomEvents(signalUrl, roomId, {
        onClose: () => {
          unsubscribe = null;
          if (!disposed) {
            reconnectTimer = window.setTimeout(connect, RECONNECT_DELAY_MS);
          }
        },
        onError: (error) => {
          if (!disposed) onError(error);
        },
        onJoined: (room) => {
          if (!disposed) onJoined(room);
        },
      });
    };

    connect();
    return () => {
      disposed = true;
      if (reconnectTimer !== null) window.clearTimeout(reconnectTimer);
      unsubscribe?.();
    };
  }, [enabled, onError, onJoined, roomId, signalUrl]);
}
