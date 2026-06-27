import { afterEach, describe, expect, test, vi } from 'vitest';

const mocks = vi.hoisted(() => ({
  actionCallback: null as null | ((notification: unknown) => void),
  isPermissionGranted: vi.fn(),
  onAction: vi.fn(async (callback: (notification: unknown) => void) => {
    mocks.actionCallback = callback;
    return { unregister: vi.fn(async () => {}) };
  }),
  requestPermission: vi.fn(),
  sendNotification: vi.fn(),
  showNewRoomNotification: vi.fn(),
  window: {
    setFocus: vi.fn(async () => {}),
    show: vi.fn(async () => {}),
    unminimize: vi.fn(async () => {}),
  },
}));

vi.mock('@tauri-apps/plugin-notification', () => ({
  isPermissionGranted: mocks.isPermissionGranted,
  onAction: mocks.onAction,
  requestPermission: mocks.requestPermission,
  sendNotification: mocks.sendNotification,
}));

vi.mock('@tauri-apps/api/window', () => ({
  getCurrentWindow: vi.fn(() => mocks.window),
}));

vi.mock('./tauriClient', () => ({
  showNewRoomNotification: mocks.showNewRoomNotification,
}));

async function importNotifications() {
  vi.resetModules();
  return import('./roomNotifications');
}

afterEach(() => {
  vi.unstubAllGlobals();
  vi.clearAllMocks();
  mocks.actionCallback = null;
});

describe('room notifications', () => {
  test('新規部屋通知は部屋IDではなく相手名を本文に使う', async () => {
    vi.stubGlobal('window', { __TAURI_INTERNALS__: {} });
    mocks.isPermissionGranted.mockResolvedValueOnce(true);
    mocks.showNewRoomNotification.mockResolvedValueOnce(false);
    const { notifyNewRoomAvailable } = await importNotifications();

    await expect(
      notifyNewRoomAvailable({
        host_name: 'Alice',
        room_id: 'room-1',
      }),
    ).resolves.toBe(true);

    expect(mocks.sendNotification).toHaveBeenCalledWith({
      title: '新しい部屋があります',
      body: 'Aliceさんが部屋を作成しました',
      autoCancel: true,
      extra: {
        kind: 'new_room',
        room_id: 'room-1',
      },
    });
  });

  test('Windowsネイティブ通知で処理できた場合はJS通知を重ねて出さない', async () => {
    vi.stubGlobal('window', { __TAURI_INTERNALS__: {} });
    mocks.isPermissionGranted.mockResolvedValueOnce(true);
    mocks.showNewRoomNotification.mockResolvedValueOnce(true);
    const { notifyNewRoomAvailable } = await importNotifications();

    await expect(
      notifyNewRoomAvailable({
        host_name: 'Alice',
        room_id: 'room-1',
      }),
    ).resolves.toBe(true);

    expect(mocks.showNewRoomNotification).toHaveBeenCalledWith({
      title: '新しい部屋があります',
      body: 'Aliceさんが部屋を作成しました',
    });
    expect(mocks.sendNotification).not.toHaveBeenCalled();
  });

  test('新規部屋通知の押下でメインウィンドウを表示してフォーカスする', async () => {
    vi.stubGlobal('window', { __TAURI_INTERNALS__: {} });
    mocks.isPermissionGranted.mockResolvedValueOnce(true);
    mocks.showNewRoomNotification.mockResolvedValueOnce(false);
    const { notifyNewRoomAvailable } = await importNotifications();

    await notifyNewRoomAvailable({
      host_name: 'Alice',
      room_id: 'room-1',
    });
    mocks.actionCallback?.({ extra: { kind: 'new_room' } });

    await vi.waitFor(() =>
      expect(mocks.window.setFocus).toHaveBeenCalledTimes(1),
    );
    expect(mocks.window.show).toHaveBeenCalledTimes(1);
    expect(mocks.window.unminimize).toHaveBeenCalledTimes(1);
  });
});
