import { getCurrentWindow } from '@tauri-apps/api/window';
import {
  isPermissionGranted,
  onAction,
  requestPermission,
  sendNotification,
} from '@tauri-apps/plugin-notification';

type NewRoomNotificationRoom = {
  host_name: string;
  room_id: string;
};

const newRoomNotificationKind = 'new_room';

let notificationActionHandlerInstalled = false;

function isTauriRuntime() {
  return '__TAURI_INTERNALS__' in window;
}

async function showMainWindow() {
  const window = getCurrentWindow();
  await window.show();
  await window.unminimize();
  await window.setFocus();
}

async function installNotificationActionHandler() {
  if (notificationActionHandlerInstalled || !isTauriRuntime()) {
    return;
  }
  notificationActionHandlerInstalled = true;
  try {
    await onAction((notification) => {
      if (notification.extra?.kind === newRoomNotificationKind) {
        void showMainWindow().catch(() => {});
      }
    });
  } catch {
    notificationActionHandlerInstalled = false;
  }
}

export async function notifyNewRoomAvailable(room: NewRoomNotificationRoom) {
  if (!isTauriRuntime()) {
    return false;
  }

  let granted = await isPermissionGranted();
  if (!granted) {
    granted = (await requestPermission()) === 'granted';
  }
  if (!granted) {
    return false;
  }

  await installNotificationActionHandler();
  const opponentName = room.host_name.trim() || '相手';
  sendNotification({
    title: '新しい部屋があります',
    body: `${opponentName}さんが部屋を作成しました`,
    autoCancel: true,
    extra: {
      kind: newRoomNotificationKind,
      room_id: room.room_id,
    },
  });
  return true;
}
