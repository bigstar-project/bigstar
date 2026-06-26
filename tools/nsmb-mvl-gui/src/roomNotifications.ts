import {
  isPermissionGranted,
  requestPermission,
  sendNotification,
} from '@tauri-apps/plugin-notification';

function isTauriRuntime() {
  return '__TAURI_INTERNALS__' in window;
}

export async function notifyNewRoomAvailable(roomId: string) {
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

  sendNotification({
    title: '新しい部屋があります',
    body: `新しい部屋が作成されました: ${roomId}`,
  });
  return true;
}
