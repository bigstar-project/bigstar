use std::fs;
use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};

use rusqlite::{named_params, params, Connection, OptionalExtension, Transaction};
use tauri::AppHandle;

use crate::models::{
    MatchHistoryRecord, MatchHistoryStatus, MvlPlayerResult, MvlStageResult, Role,
};
use crate::paths::{app_data_dir, load_match_history_document_content};

const JSON_IMPORT_META_KEY: &str = "match_history_json_imported";

mod embedded {
    refinery::embed_migrations!("migrations");
}

pub(crate) fn load_match_history(app: &AppHandle) -> Result<Vec<MatchHistoryRecord>, String> {
    let mut conn = open_history_database(app)?;
    ensure_json_history_imported(app, &mut conn)?;
    load_match_history_records(&conn)
}

pub(crate) fn save_match_history(
    app: &AppHandle,
    matches: &[MatchHistoryRecord],
) -> Result<(), String> {
    let mut conn = open_history_database(app)?;
    ensure_json_history_imported(app, &mut conn)?;
    save_match_history_records(&mut conn, matches)
}

fn open_history_database(app: &AppHandle) -> Result<Connection, String> {
    let path = history_database_path(app)?;
    let mut conn =
        Connection::open(&path).map_err(|err| format!("match history DB を開けません: {err}"))?;
    configure_connection(&conn)?;
    run_migrations(&mut conn)?;
    Ok(conn)
}

fn history_database_path(app: &AppHandle) -> Result<PathBuf, String> {
    Ok(app_data_dir(app)?.join("match-history.sqlite3"))
}

fn legacy_match_history_path(app: &AppHandle) -> Result<PathBuf, String> {
    Ok(app_data_dir(app)?.join("match-history.json"))
}

fn configure_connection(conn: &Connection) -> Result<(), String> {
    conn.execute_batch(
        "
        PRAGMA foreign_keys = ON;
        PRAGMA busy_timeout = 5000;
        PRAGMA journal_mode = WAL;
        PRAGMA synchronous = NORMAL;
        ",
    )
    .map_err(|err| format!("match history DB を設定できません: {err}"))
}

fn run_migrations(conn: &mut Connection) -> Result<(), String> {
    embedded::migrations::runner()
        .run(conn)
        .map(|_| ())
        .map_err(|err| format!("match history DB migration に失敗しました: {err}"))
}

fn ensure_json_history_imported(app: &AppHandle, conn: &mut Connection) -> Result<(), String> {
    if meta_value(conn, JSON_IMPORT_META_KEY)?.as_deref() == Some("1") {
        return Ok(());
    }

    let legacy_path = legacy_match_history_path(app)?;
    if !legacy_path.exists() {
        set_meta_value(conn, JSON_IMPORT_META_KEY, "1")?;
        return Ok(());
    }

    let existing_count: u32 = conn
        .query_row("SELECT COUNT(*) FROM match_history", [], |row| row.get(0))
        .map_err(|err| format!("match history DB を確認できません: {err}"))?;
    if existing_count > 0 {
        set_meta_value(conn, JSON_IMPORT_META_KEY, "1")?;
        return Ok(());
    }

    let content = fs::read_to_string(&legacy_path)
        .map_err(|err| format!("既存match history JSONを読み込めません: {err}"))?;
    let (matches, _) = load_match_history_document_content(&content)?;

    let tx = conn
        .transaction()
        .map_err(|err| format!("match history import transaction を開始できません: {err}"))?;
    replace_match_history_records(&tx, &matches)?;
    set_meta_value_tx(&tx, JSON_IMPORT_META_KEY, "1")?;
    tx.commit()
        .map_err(|err| format!("match history import を確定できません: {err}"))
}

fn meta_value(conn: &Connection, key: &str) -> Result<Option<String>, String> {
    conn.query_row("SELECT value FROM app_meta WHERE key = ?1", [key], |row| {
        row.get(0)
    })
    .optional()
    .map_err(|err| format!("match history meta を読み込めません: {err}"))
}

fn set_meta_value(conn: &mut Connection, key: &str, value: &str) -> Result<(), String> {
    let tx = conn
        .transaction()
        .map_err(|err| format!("match history meta transaction を開始できません: {err}"))?;
    set_meta_value_tx(&tx, key, value)?;
    tx.commit()
        .map_err(|err| format!("match history meta を保存できません: {err}"))
}

fn set_meta_value_tx(tx: &Transaction<'_>, key: &str, value: &str) -> Result<(), String> {
    tx.execute(
        "
        INSERT INTO app_meta (key, value)
        VALUES (?1, ?2)
        ON CONFLICT(key) DO UPDATE SET value = excluded.value
        ",
        params![key, value],
    )
    .map(|_| ())
    .map_err(|err| format!("match history meta を保存できません: {err}"))
}

fn load_match_history_records(conn: &Connection) -> Result<Vec<MatchHistoryRecord>, String> {
    let mut stmt = conn
        .prepare(
            "
            SELECT record_json
            FROM match_history
            ORDER BY list_index ASC, started_at DESC
            ",
        )
        .map_err(|err| format!("match history query を準備できません: {err}"))?;
    let rows = stmt
        .query_map([], |row| row.get::<_, String>(0))
        .map_err(|err| format!("match history を読み込めません: {err}"))?;

    let mut matches = Vec::new();
    for row in rows {
        let json = row.map_err(|err| format!("match history row を読み込めません: {err}"))?;
        let record = serde_json::from_str(&json)
            .map_err(|err| format!("match history record JSONの形式が不正です: {err}"))?;
        matches.push(record);
    }
    Ok(matches)
}

fn save_match_history_records(
    conn: &mut Connection,
    matches: &[MatchHistoryRecord],
) -> Result<(), String> {
    let tx = conn
        .transaction()
        .map_err(|err| format!("match history transaction を開始できません: {err}"))?;
    replace_match_history_records(&tx, matches)?;
    set_meta_value_tx(&tx, JSON_IMPORT_META_KEY, "1")?;
    tx.commit()
        .map_err(|err| format!("match history を保存できません: {err}"))
}

fn replace_match_history_records(
    tx: &Transaction<'_>,
    matches: &[MatchHistoryRecord],
) -> Result<(), String> {
    tx.execute("DELETE FROM match_history", [])
        .map_err(|err| format!("match history を削除できません: {err}"))?;

    for (index, record) in matches.iter().enumerate() {
        insert_match_history_record(tx, index, record)?;
    }

    Ok(())
}

fn insert_match_history_record(
    tx: &Transaction<'_>,
    index: usize,
    record: &MatchHistoryRecord,
) -> Result<(), String> {
    let projection = MatchProjection::from_record(record);
    let record_json = serde_json::to_string(record)
        .map_err(|err| format!("match history record をJSON化できません: {err}"))?;
    let updated_at_unix_ms = now_unix_ms();

    tx.execute(
        "
        INSERT INTO match_history (
          id,
          list_index,
          started_at,
          status,
          role,
          room_code,
          log_dir,
          local_player_id,
          local_player_name,
          opponent_player_id,
          opponent_player_name,
          local_wins,
          opponent_wins,
          match_winner,
          record_json,
          updated_at_unix_ms
        ) VALUES (
          :id,
          :list_index,
          :started_at,
          :status,
          :role,
          :room_code,
          :log_dir,
          :local_player_id,
          :local_player_name,
          :opponent_player_id,
          :opponent_player_name,
          :local_wins,
          :opponent_wins,
          :match_winner,
          :record_json,
          :updated_at_unix_ms
        )
        ",
        named_params! {
            ":id": record.id,
            ":list_index": index as u32,
            ":started_at": record.started_at,
            ":status": status_label(&record.status),
            ":role": role_label(&record.role),
            ":room_code": record.room_code,
            ":log_dir": record.log_dir,
            ":local_player_id": projection.local_player_id.as_deref(),
            ":local_player_name": projection.local_player_name,
            ":opponent_player_id": projection.opponent_player_id.as_deref(),
            ":opponent_player_name": projection.opponent_player_name,
            ":local_wins": projection.local_wins,
            ":opponent_wins": projection.opponent_wins,
            ":match_winner": projection.match_winner.as_deref(),
            ":record_json": record_json,
            ":updated_at_unix_ms": updated_at_unix_ms,
        },
    )
    .map_err(|err| format!("match history record を保存できません: {err}"))?;

    for stage in &record.stages {
        insert_stage_result(tx, record, stage)?;
    }

    Ok(())
}

fn insert_stage_result(
    tx: &Transaction<'_>,
    record: &MatchHistoryRecord,
    stage: &MvlStageResult,
) -> Result<(), String> {
    let local = local_player_result(&record.role, stage);
    let opponent = opponent_player_result(&record.role, stage);
    let winner = stage_winner_label(&record.role, stage.winner);

    tx.execute(
        "
        INSERT INTO match_stage_results (
          match_id,
          game_index,
          stage,
          frame,
          local_stars,
          opponent_stars,
          local_displayed_stars,
          opponent_displayed_stars,
          local_collected_stars,
          opponent_collected_stars,
          local_lives,
          opponent_lives,
          local_deaths,
          opponent_deaths,
          local_dead,
          opponent_dead,
          winner,
          resolved,
          stage_line
        ) VALUES (
          ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10,
          ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19
        )
        ",
        params![
            record.id,
            stage.game_index,
            stage.stage,
            stage.frame,
            local.stars,
            opponent.stars,
            local.displayed_stars,
            opponent.displayed_stars,
            local.collected_stars,
            opponent.collected_stars,
            local.lives,
            opponent.lives,
            local.deaths,
            opponent.deaths,
            bool_to_i64(local.dead),
            bool_to_i64(opponent.dead),
            winner.as_deref(),
            bool_to_i64(stage.resolved),
            stage.line,
        ],
    )
    .map(|_| ())
    .map_err(|err| format!("match stage result を保存できません: {err}"))
}

struct MatchProjection {
    local_player_id: Option<String>,
    local_player_name: String,
    opponent_player_id: Option<String>,
    opponent_player_name: String,
    local_wins: u32,
    opponent_wins: u32,
    match_winner: Option<String>,
}

impl MatchProjection {
    fn from_record(record: &MatchHistoryRecord) -> Self {
        let (local_player_id, opponent_player_id) = match record.role {
            Role::Host => (&record.player_ids.mario, &record.player_ids.luigi),
            Role::Client => (&record.player_ids.luigi, &record.player_ids.mario),
        };
        let (local_player_name, opponent_player_name) = match record.role {
            Role::Host => (&record.player_names.mario, &record.player_names.luigi),
            Role::Client => (&record.player_names.luigi, &record.player_names.mario),
        };

        let local_mario_luigi_id = match record.role {
            Role::Host => 0,
            Role::Client => 1,
        };
        let (local_wins, opponent_wins) = count_wins(&record.stages, local_mario_luigi_id);
        let match_winner = if matches!(record.status, MatchHistoryStatus::Completed) {
            winner_from_counts(local_wins, opponent_wins)
        } else {
            None
        };

        Self {
            local_player_id: non_empty(local_player_id),
            local_player_name: local_player_name.clone(),
            opponent_player_id: non_empty(opponent_player_id),
            opponent_player_name: opponent_player_name.clone(),
            local_wins,
            opponent_wins,
            match_winner,
        }
    }
}

fn count_wins(stages: &[MvlStageResult], local_mario_luigi_id: u8) -> (u32, u32) {
    let mut local_wins = 0;
    let mut opponent_wins = 0;
    for stage in stages.iter().filter(|stage| stage.resolved) {
        match stage.winner {
            Some(winner) if winner == local_mario_luigi_id => local_wins += 1,
            Some(_) => opponent_wins += 1,
            None => {}
        }
    }
    (local_wins, opponent_wins)
}

fn winner_from_counts(local_wins: u32, opponent_wins: u32) -> Option<String> {
    match local_wins.cmp(&opponent_wins) {
        std::cmp::Ordering::Greater => Some("local".to_owned()),
        std::cmp::Ordering::Less => Some("opponent".to_owned()),
        std::cmp::Ordering::Equal => None,
    }
}

fn local_player_result<'a>(role: &Role, stage: &'a MvlStageResult) -> &'a MvlPlayerResult {
    match role {
        Role::Host => &stage.mario,
        Role::Client => &stage.luigi,
    }
}

fn opponent_player_result<'a>(role: &Role, stage: &'a MvlStageResult) -> &'a MvlPlayerResult {
    match role {
        Role::Host => &stage.luigi,
        Role::Client => &stage.mario,
    }
}

fn stage_winner_label(role: &Role, winner: Option<u8>) -> Option<String> {
    match (role, winner) {
        (Role::Host, Some(0)) | (Role::Client, Some(1)) => Some("local".to_owned()),
        (Role::Host, Some(1)) | (Role::Client, Some(0)) => Some("opponent".to_owned()),
        _ => None,
    }
}

fn non_empty(value: &str) -> Option<String> {
    let trimmed = value.trim();
    if trimmed.is_empty() {
        None
    } else {
        Some(trimmed.to_owned())
    }
}

fn role_label(role: &Role) -> &'static str {
    match role {
        Role::Host => "host",
        Role::Client => "client",
    }
}

fn status_label(status: &MatchHistoryStatus) -> &'static str {
    match status {
        MatchHistoryStatus::Running => "running",
        MatchHistoryStatus::Completed => "completed",
        MatchHistoryStatus::Stopped => "stopped",
    }
}

fn bool_to_i64(value: bool) -> i64 {
    if value {
        1
    } else {
        0
    }
}

fn now_unix_ms() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as i64
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::models::{CourseMode, GameSettings, Lives, MatchPlayerIds, MatchPlayerNames};

    #[test]
    fn save_projects_opponent_and_stage_stats_for_queries() {
        let mut conn = Connection::open_in_memory().expect("open in-memory db");
        run_migrations(&mut conn).expect("run migrations");
        let records = vec![sample_record()];

        save_match_history_records(&mut conn, &records).expect("save history");

        let projected: (Option<String>, String, u32, u32, Option<String>) = conn
            .query_row(
                "
                SELECT opponent_player_id, opponent_player_name, local_wins, opponent_wins, match_winner
                FROM match_history
                WHERE id = ?1
                ",
                ["match-1"],
                |row| {
                    Ok((
                        row.get(0)?,
                        row.get(1)?,
                        row.get(2)?,
                        row.get(3)?,
                        row.get(4)?,
                    ))
                },
            )
            .expect("query projected row");
        assert_eq!(
            projected,
            (
                Some("client-profile".to_owned()),
                "Client".to_owned(),
                1,
                1,
                None
            )
        );

        let stage_winner: String = conn
            .query_row(
                "
                SELECT winner
                FROM match_stage_results
                WHERE match_id = ?1 AND game_index = ?2
                ",
                params!["match-1", 2_u32],
                |row| row.get(0),
            )
            .expect("query stage winner");
        assert_eq!(stage_winner, "opponent");
    }

    #[test]
    fn load_returns_saved_record_json_in_list_order() {
        let mut conn = Connection::open_in_memory().expect("open in-memory db");
        run_migrations(&mut conn).expect("run migrations");
        let first = sample_record();
        let mut second = sample_record();
        second.id = "match-2".to_owned();
        second.started_at = "2026-06-24T01:00:00.000Z".to_owned();

        save_match_history_records(&mut conn, &[first, second]).expect("save history");
        let loaded = load_match_history_records(&conn).expect("load history");

        assert_eq!(loaded.len(), 2);
        assert_eq!(loaded[0].id, "match-1");
        assert_eq!(loaded[1].id, "match-2");
    }

    #[test]
    fn save_keeps_more_than_one_thousand_records() {
        let mut conn = Connection::open_in_memory().expect("open in-memory db");
        run_migrations(&mut conn).expect("run migrations");
        let mut records = Vec::new();
        for index in 0..1001 {
            let mut record = sample_record();
            record.id = format!("match-{index}");
            record.started_at = format!("2026-06-24T00:{:02}:00.000Z", index % 60);
            records.push(record);
        }

        save_match_history_records(&mut conn, &records).expect("save history");
        let loaded = load_match_history_records(&conn).expect("load history");

        assert_eq!(loaded.len(), 1001);
        assert_eq!(loaded[0].id, "match-0");
        assert_eq!(loaded[1000].id, "match-1000");
    }

    fn sample_record() -> MatchHistoryRecord {
        MatchHistoryRecord {
            id: "match-1".to_owned(),
            log_dir: "C:\\logs\\match-1".to_owned(),
            player_ids: MatchPlayerIds {
                mario: "host-profile".to_owned(),
                luigi: "client-profile".to_owned(),
            },
            player_names: MatchPlayerNames {
                mario: "Host".to_owned(),
                luigi: "Client".to_owned(),
            },
            role: Role::Host,
            room_code: "ROOM1".to_owned(),
            settings: GameSettings {
                course_mode: CourseMode::Random,
                course_stages: vec![0, 1, 2, 3, 4],
                wins: 2,
                big_stars: 5,
                lives: Lives::Three,
                match_seed: "1".to_owned(),
                rng_seeds: vec!["1".to_owned(), "2".to_owned()],
                input_delay_frames: 4,
                input_max_frame_lead: 4,
                rollback_enabled: false,
            },
            stages: vec![
                MvlStageResult {
                    game_index: 1,
                    stage: Some(0),
                    frame: 1200,
                    winner: Some(0),
                    mario: player(5, 3, false),
                    luigi: player(3, 2, false),
                    mario_match_wins: 1,
                    luigi_match_wins: 0,
                    target_wins: 2,
                    resolved: true,
                    line: "stage 1".to_owned(),
                },
                MvlStageResult {
                    game_index: 2,
                    stage: Some(1),
                    frame: 2400,
                    winner: Some(1),
                    mario: player(2, 1, false),
                    luigi: player(5, 2, false),
                    mario_match_wins: 1,
                    luigi_match_wins: 1,
                    target_wins: 2,
                    resolved: true,
                    line: "stage 2".to_owned(),
                },
            ],
            started_at: "2026-06-24T00:00:00.000Z".to_owned(),
            status: MatchHistoryStatus::Completed,
        }
    }

    fn player(stars: u32, lives: u32, dead: bool) -> MvlPlayerResult {
        MvlPlayerResult {
            stars,
            displayed_stars: stars,
            collected_stars: stars,
            lives,
            deaths: 3 - lives,
            dead,
        }
    }
}
