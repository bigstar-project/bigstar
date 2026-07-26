use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Manager, PhysicalPosition, PhysicalSize, Position, Size};

const WINDOW_STATE_FILE_NAME: &str = ".window-state.json";
const MIN_WINDOW_WIDTH: u32 = 640;
const MIN_WINDOW_HEIGHT: u32 = 480;

#[derive(Debug, Deserialize, Serialize)]
struct SavedWindowState {
    width: u32,
    height: u32,
    x: i32,
    y: i32,
    maximized: bool,
    fullscreen: bool,
}

pub(crate) fn restore_main_window_state(app: &AppHandle) -> Result<(), String> {
    let Some(window) = app.get_webview_window("main") else {
        return Ok(());
    };
    let path = crate::paths::app_data_dir(app)?.join(WINDOW_STATE_FILE_NAME);
    if !path.is_file() {
        return Ok(());
    }

    let content = std::fs::read_to_string(&path)
        .map_err(|err| format!("ウィンドウ状態を読み取れません: {err}"))?;
    let state: SavedWindowState = serde_json::from_str(&content)
        .map_err(|err| format!("ウィンドウ状態を解析できません: {err}"))?;

    if state.width >= MIN_WINDOW_WIDTH && state.height >= MIN_WINDOW_HEIGHT {
        window
            .set_size(Size::Physical(PhysicalSize::new(state.width, state.height)))
            .map_err(|err| format!("ウィンドウサイズを復元できません: {err}"))?;
    }
    if position_intersects_a_monitor(app, &state)? {
        window
            .set_position(Position::Physical(PhysicalPosition::new(state.x, state.y)))
            .map_err(|err| format!("ウィンドウ位置を復元できません: {err}"))?;
    }
    if state.maximized {
        window
            .maximize()
            .map_err(|err| format!("最大化状態を復元できません: {err}"))?;
    }
    if state.fullscreen {
        window
            .set_fullscreen(true)
            .map_err(|err| format!("全画面状態を復元できません: {err}"))?;
    }
    Ok(())
}

pub(crate) fn save_main_window_state(app: &AppHandle) -> Result<(), String> {
    let Some(window) = app.get_webview_window("main") else {
        return Ok(());
    };
    let size = window
        .inner_size()
        .map_err(|err| format!("ウィンドウサイズを取得できません: {err}"))?;
    let position = window
        .outer_position()
        .map_err(|err| format!("ウィンドウ位置を取得できません: {err}"))?;
    let state = SavedWindowState {
        width: size.width,
        height: size.height,
        x: position.x,
        y: position.y,
        maximized: window
            .is_maximized()
            .map_err(|err| format!("最大化状態を取得できません: {err}"))?,
        fullscreen: window
            .is_fullscreen()
            .map_err(|err| format!("全画面状態を取得できません: {err}"))?,
    };
    let content = serde_json::to_vec_pretty(&state)
        .map_err(|err| format!("ウィンドウ状態を変換できません: {err}"))?;
    let path = crate::paths::app_data_dir(app)?.join(WINDOW_STATE_FILE_NAME);
    std::fs::write(path, content).map_err(|err| format!("ウィンドウ状態を保存できません: {err}"))
}

fn position_intersects_a_monitor(
    app: &AppHandle,
    state: &SavedWindowState,
) -> Result<bool, String> {
    let monitors = app
        .available_monitors()
        .map_err(|err| format!("モニター情報を取得できません: {err}"))?;
    let window_left = i64::from(state.x);
    let window_top = i64::from(state.y);
    let window_right = window_left + i64::from(state.width);
    let window_bottom = window_top + i64::from(state.height);

    Ok(monitors.iter().any(|monitor| {
        let position = monitor.position();
        let size = monitor.size();
        let monitor_left = i64::from(position.x);
        let monitor_top = i64::from(position.y);
        let monitor_right = monitor_left + i64::from(size.width);
        let monitor_bottom = monitor_top + i64::from(size.height);
        window_left < monitor_right
            && window_right > monitor_left
            && window_top < monitor_bottom
            && window_bottom > monitor_top
    }))
}
