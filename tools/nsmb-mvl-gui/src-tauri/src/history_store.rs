use std::fs;
use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};

use rusqlite::{
    named_params, params, params_from_iter, types::Value, Connection, OptionalExtension,
    Transaction,
};
use tauri::AppHandle;

use crate::models::{
    MatchHistoryCursor, MatchHistoryDashboard, MatchHistoryFilter, MatchHistoryOpponent,
    MatchHistoryOutcome, MatchHistoryPage, MatchHistoryPageRequest, MatchHistoryRecord,
    MatchHistoryStageStatistics, MatchHistoryStatus, MatchHistoryStreakKind, MatchHistorySummary,
    MatchHistoryTrendPoint, MvlPlayerResult, MvlStageResult, Role,
};
use crate::paths::{app_data_dir, load_match_history_document_content};

const JSON_IMPORT_META_KEY: &str = "match_history_json_imported";

mod embedded {
    refinery::embed_migrations!("migrations");
}

pub(crate) fn upsert_match_history(
    app: &AppHandle,
    record: &MatchHistoryRecord,
) -> Result<(), String> {
    let mut conn = open_history_database(app)?;
    ensure_json_history_imported(app, &mut conn)?;
    HistoryRepository::new(&mut conn).upsert_match(record)
}

pub(crate) fn delete_match_history(app: &AppHandle, match_id: &str) -> Result<(), String> {
    let mut conn = open_history_database(app)?;
    ensure_json_history_imported(app, &mut conn)?;
    HistoryRepository::new(&mut conn).delete_match(match_id)
}

pub(crate) fn query_match_history(
    app: &AppHandle,
    request: &MatchHistoryPageRequest,
) -> Result<MatchHistoryPage, String> {
    let mut conn = open_history_database(app)?;
    ensure_json_history_imported(app, &mut conn)?;
    HistoryRepository::new(&mut conn).history_page(request)
}

pub(crate) fn load_match_history_dashboard(
    app: &AppHandle,
    filter: &MatchHistoryFilter,
) -> Result<MatchHistoryDashboard, String> {
    let mut conn = open_history_database(app)?;
    ensure_json_history_imported(app, &mut conn)?;
    HistoryRepository::new(&mut conn).dashboard(filter)
}

pub(crate) fn load_match_history_opponents(
    app: &AppHandle,
) -> Result<Vec<MatchHistoryOpponent>, String> {
    let mut conn = open_history_database(app)?;
    ensure_json_history_imported(app, &mut conn)?;
    HistoryRepository::new(&mut conn).opponents()
}

struct HistoryRepository<'conn> {
    conn: &'conn mut Connection,
}

impl<'conn> HistoryRepository<'conn> {
    fn new(conn: &'conn mut Connection) -> Self {
        Self { conn }
    }

    fn upsert_match(&mut self, record: &MatchHistoryRecord) -> Result<(), String> {
        if !has_played_result(record) {
            return self.delete_match(&record.id);
        }
        let tx = self
            .conn
            .transaction()
            .map_err(|err| format!("match history UPSERT transaction を開始できません: {err}"))?;
        tx.execute("DELETE FROM match_history WHERE id = ?1", [&record.id])
            .map_err(|err| format!("更新前のmatch historyを削除できません: {err}"))?;
        insert_match_history_record(&tx, 0, record)?;
        set_meta_value_tx(&tx, JSON_IMPORT_META_KEY, "1")?;
        tx.commit()
            .map_err(|err| format!("match history UPSERTを確定できません: {err}"))
    }

    fn delete_match(&mut self, match_id: &str) -> Result<(), String> {
        self.conn
            .execute("DELETE FROM match_history WHERE id = ?1", [match_id])
            .map(|_| ())
            .map_err(|err| format!("match historyを削除できません: {err}"))
    }

    fn history_page(
        &mut self,
        request: &MatchHistoryPageRequest,
    ) -> Result<MatchHistoryPage, String> {
        let scoped = ScopedMatchesQuery::new(&request.filter, true, true);
        let count_sql = format!("{} SELECT COUNT(*) FROM filtered_matches", scoped.cte);
        let total = self
            .conn
            .query_row(&count_sql, params_from_iter(scoped.params.iter()), |row| {
                row.get::<_, u32>(0)
            })
            .map_err(|err| format!("match history件数を取得できません: {err}"))?;

        let limit = request.limit.clamp(1, 100);
        let mut params = scoped.params.clone();
        let mut cursor_clause = String::new();
        if let Some(cursor) = &request.cursor {
            cursor_clause.push_str(" AND (started_at < ? OR (started_at = ? AND id < ?))");
            params.push(cursor.started_at.clone().into());
            params.push(cursor.started_at.clone().into());
            params.push(cursor.id.clone().into());
        }
        params.push(i64::from(limit + 1).into());
        let page_sql = format!(
            "{} SELECT record_json FROM filtered_matches \
             WHERE 1 = 1{} ORDER BY started_at DESC, id DESC LIMIT ?",
            scoped.cte, cursor_clause
        );
        let mut stmt = self
            .conn
            .prepare(&page_sql)
            .map_err(|err| format!("match history page queryを準備できません: {err}"))?;
        let rows = stmt
            .query_map(params_from_iter(params.iter()), |row| {
                row.get::<_, String>(0)
            })
            .map_err(|err| format!("match history pageを取得できません: {err}"))?;
        let mut matches: Vec<MatchHistoryRecord> = Vec::new();
        for row in rows {
            let json = row.map_err(|err| format!("match history page行を読めません: {err}"))?;
            matches.push(
                serde_json::from_str(&json)
                    .map_err(|err| format!("match history JSONの形式が不正です: {err}"))?,
            );
        }
        let has_more = matches.len() > limit as usize;
        matches.truncate(limit as usize);
        let next_cursor = if has_more {
            matches.last().map(|record| MatchHistoryCursor {
                started_at: record.started_at.clone(),
                id: record.id.clone(),
            })
        } else {
            None
        };
        Ok(MatchHistoryPage {
            matches,
            next_cursor,
            total,
        })
    }

    fn dashboard(&mut self, filter: &MatchHistoryFilter) -> Result<MatchHistoryDashboard, String> {
        let analytics_filter = MatchHistoryFilter {
            outcome: None,
            ..filter.clone()
        };
        let match_scope = ScopedMatchesQuery::new(&analytics_filter, false, false);
        let summary_sql = format!(
            "{} SELECT \
               COALESCE(SUM(match_winner = 'local'), 0), \
               COALESCE(SUM(match_winner = 'opponent'), 0), \
               COALESCE(SUM(status = 'stopped'), 0) \
             FROM filtered_matches",
            match_scope.cte
        );
        let (wins, losses, stopped) = self
            .conn
            .query_row(
                &summary_sql,
                params_from_iter(match_scope.params.iter()),
                |row| {
                    Ok((
                        row.get::<_, u32>(0)?,
                        row.get::<_, u32>(1)?,
                        row.get::<_, u32>(2)?,
                    ))
                },
            )
            .map_err(|err| format!("match history summaryを取得できません: {err}"))?;

        let game_scope = ScopedMatchesQuery::new(&analytics_filter, false, false);
        let mut game_params = game_scope.params.clone();
        let stage_clause = if let Some(stage) = analytics_filter.stage {
            game_params.push(i64::from(stage).into());
            " AND sr.stage = ?"
        } else {
            ""
        };
        let games_sql = format!(
            "{} SELECT \
               COALESCE(SUM(sr.winner = 'local'), 0), \
               COALESCE(SUM(sr.winner = 'opponent'), 0) \
             FROM filtered_matches mh \
             JOIN match_stage_results sr ON sr.match_id = mh.id \
             WHERE sr.resolved = 1 AND sr.winner IS NOT NULL{}",
            game_scope.cte, stage_clause
        );
        let (game_wins, game_losses) = self
            .conn
            .query_row(&games_sql, params_from_iter(game_params.iter()), |row| {
                Ok((row.get::<_, u32>(0)?, row.get::<_, u32>(1)?))
            })
            .map_err(|err| format!("game summaryを取得できません: {err}"))?;

        let outcomes_sql = format!(
            "{} SELECT match_winner FROM filtered_matches \
             WHERE status = 'completed' AND match_winner IS NOT NULL \
             ORDER BY started_at DESC, id DESC",
            match_scope.cte
        );
        let mut stmt = self
            .conn
            .prepare(&outcomes_sql)
            .map_err(|err| format!("連勝queryを準備できません: {err}"))?;
        let outcomes = stmt
            .query_map(params_from_iter(match_scope.params.iter()), |row| {
                row.get::<_, String>(0)
            })
            .map_err(|err| format!("連勝データを取得できません: {err}"))?;
        let mut streak = 0;
        let mut first_outcome: Option<String> = None;
        for outcome in outcomes {
            let outcome = outcome.map_err(|err| format!("連勝データを読めません: {err}"))?;
            match &first_outcome {
                None => first_outcome = Some(outcome),
                Some(first) if first != &outcome => break,
                _ => {}
            }
            streak += 1;
        }
        let streak_kind = match first_outcome.as_deref() {
            Some("local") => Some(MatchHistoryStreakKind::Win),
            Some("opponent") => Some(MatchHistoryStreakKind::Loss),
            _ => None,
        };

        let trend = self.load_trend(&match_scope)?;
        let stages = self.load_stage_statistics(&game_scope, analytics_filter.stage)?;
        Ok(MatchHistoryDashboard {
            summary: MatchHistorySummary {
                wins,
                losses,
                stopped,
                game_wins,
                game_losses,
                streak,
                streak_kind,
            },
            trend,
            stages,
        })
    }

    fn load_trend(
        &self,
        scope: &ScopedMatchesQuery,
    ) -> Result<Vec<MatchHistoryTrendPoint>, String> {
        let sql = format!(
            "{} SELECT id, started_at, opponent_player_name, match_winner FROM (\
               SELECT id, started_at, opponent_player_name, match_winner \
               FROM filtered_matches \
               WHERE status = 'completed' AND match_winner IS NOT NULL \
               ORDER BY started_at DESC, id DESC LIMIT 69\
             ) ORDER BY started_at ASC, id ASC",
            scope.cte
        );
        let mut stmt = self
            .conn
            .prepare(&sql)
            .map_err(|err| format!("勝率推移queryを準備できません: {err}"))?;
        let rows = stmt
            .query_map(params_from_iter(scope.params.iter()), |row| {
                Ok((
                    row.get::<_, String>(0)?,
                    row.get::<_, String>(1)?,
                    row.get::<_, String>(2)?,
                    row.get::<_, String>(3)?,
                ))
            })
            .map_err(|err| format!("勝率推移を取得できません: {err}"))?;
        let mut raw = Vec::new();
        for row in rows {
            raw.push(row.map_err(|err| format!("勝率推移行を読めません: {err}"))?);
        }
        let visible_start = raw.len().saturating_sub(60);
        let mut trend = Vec::with_capacity(raw.len() - visible_start);
        for (index, (match_id, started_at, opponent_name, outcome)) in
            raw.iter().enumerate().skip(visible_start)
        {
            let start = index.saturating_sub(9);
            let window = &raw[start..=index];
            let window_wins = window
                .iter()
                .filter(|(_, _, _, value)| value == "local")
                .count();
            trend.push(MatchHistoryTrendPoint {
                match_id: match_id.clone(),
                started_at: started_at.clone(),
                opponent_name: opponent_name.clone(),
                won: outcome == "local",
                rolling_win_rate: window_wins as f64 / window.len() as f64,
            });
        }
        Ok(trend)
    }

    fn load_stage_statistics(
        &self,
        scope: &ScopedMatchesQuery,
        selected_stage: Option<u8>,
    ) -> Result<Vec<MatchHistoryStageStatistics>, String> {
        let mut params = scope.params.clone();
        let stage_clause = if let Some(stage) = selected_stage {
            params.push(i64::from(stage).into());
            " AND sr.stage = ?"
        } else {
            ""
        };
        let sql = format!(
            "{} SELECT sr.stage, \
               COALESCE(SUM(sr.winner = 'local'), 0), \
               COALESCE(SUM(sr.winner = 'opponent'), 0) \
             FROM filtered_matches mh \
             JOIN match_stage_results sr ON sr.match_id = mh.id \
             WHERE sr.resolved = 1 AND sr.winner IS NOT NULL \
               AND sr.stage IS NOT NULL{} \
             GROUP BY sr.stage ORDER BY sr.stage",
            scope.cte, stage_clause
        );
        let mut stmt = self
            .conn
            .prepare(&sql)
            .map_err(|err| format!("ステージ統計queryを準備できません: {err}"))?;
        let rows = stmt
            .query_map(params_from_iter(params.iter()), |row| {
                Ok(MatchHistoryStageStatistics {
                    stage: row.get(0)?,
                    wins: row.get(1)?,
                    losses: row.get(2)?,
                })
            })
            .map_err(|err| format!("ステージ統計を取得できません: {err}"))?;
        let mut stages = Vec::new();
        for row in rows {
            stages.push(row.map_err(|err| format!("ステージ統計行を読めません: {err}"))?);
        }
        Ok(stages)
    }

    fn opponents(&mut self) -> Result<Vec<MatchHistoryOpponent>, String> {
        let mut stmt = self
            .conn
            .prepare(
                "SELECT grouped.opponent_player_id, \
                    (SELECT latest.opponent_player_name FROM match_history latest \
                     WHERE latest.opponent_player_id = grouped.opponent_player_id \
                       AND EXISTS (SELECT 1 FROM match_stage_results latest_stage \
                           WHERE latest_stage.match_id = latest.id \
                             AND latest_stage.resolved = 1 \
                             AND latest_stage.winner IS NOT NULL) \
                     ORDER BY latest.started_at DESC, latest.id DESC LIMIT 1), \
                    grouped.match_count, grouped.last_played_at \
                 FROM (\
                   SELECT opponent_player_id, COUNT(*) AS match_count, \
                          MAX(started_at) AS last_played_at \
                   FROM match_history \
                   WHERE opponent_player_id IS NOT NULL AND opponent_player_id <> '' \
                     AND EXISTS (SELECT 1 FROM match_stage_results grouped_stage \
                         WHERE grouped_stage.match_id = match_history.id \
                           AND grouped_stage.resolved = 1 \
                           AND grouped_stage.winner IS NOT NULL) \
                   GROUP BY opponent_player_id\
                 ) grouped \
                 ORDER BY grouped.match_count DESC, grouped.last_played_at DESC, \
                          grouped.opponent_player_id ASC",
            )
            .map_err(|err| format!("対戦相手queryを準備できません: {err}"))?;
        let rows = stmt
            .query_map([], |row| {
                Ok(MatchHistoryOpponent {
                    player_id: row.get(0)?,
                    latest_name: row.get(1)?,
                    matches: row.get(2)?,
                    last_played_at: row.get(3)?,
                })
            })
            .map_err(|err| format!("対戦相手を取得できません: {err}"))?;
        let mut opponents = Vec::new();
        for row in rows {
            opponents.push(row.map_err(|err| format!("対戦相手行を読めません: {err}"))?);
        }
        Ok(opponents)
    }
}

struct ScopedMatchesQuery {
    cte: String,
    params: Vec<Value>,
}

impl ScopedMatchesQuery {
    fn new(filter: &MatchHistoryFilter, include_outcome: bool, include_stage: bool) -> Self {
        let mut params = Vec::new();
        let mut scope_conditions = vec![
            "EXISTS (SELECT 1 FROM match_stage_results played_stage \
             WHERE played_stage.match_id = mh.id \
               AND played_stage.resolved = 1 \
               AND played_stage.winner IS NOT NULL)",
        ];
        if let Some(since) = &filter.since_started_at {
            scope_conditions.push("mh.started_at >= ?");
            params.push(since.clone().into());
        }
        if let Some(opponent_id) = &filter.opponent_player_id {
            scope_conditions.push("mh.opponent_player_id = ?");
            params.push(opponent_id.clone().into());
        }
        let scope_where = if scope_conditions.is_empty() {
            String::new()
        } else {
            format!(" WHERE {}", scope_conditions.join(" AND "))
        };
        let limit = filter
            .recent_matches
            .filter(|value| *value > 0)
            .map(|value| format!(" LIMIT {}", value.min(10_000)))
            .unwrap_or_default();

        let mut filtered_conditions = Vec::new();
        if include_outcome {
            match filter.outcome {
                Some(MatchHistoryOutcome::Completed) => {
                    filtered_conditions.push("mh.status = 'completed'")
                }
                Some(MatchHistoryOutcome::Win) => {
                    filtered_conditions.push("mh.match_winner = 'local'")
                }
                Some(MatchHistoryOutcome::Loss) => {
                    filtered_conditions.push("mh.match_winner = 'opponent'")
                }
                Some(MatchHistoryOutcome::Stopped) => {
                    filtered_conditions.push("mh.status = 'stopped'")
                }
                None => {}
            }
        }
        if include_stage {
            if let Some(stage) = filter.stage {
                filtered_conditions.push(
                    "EXISTS (SELECT 1 FROM match_stage_results filter_stage \
                     WHERE filter_stage.match_id = mh.id AND filter_stage.stage = ? \
                       AND filter_stage.resolved = 1)",
                );
                params.push(i64::from(stage).into());
            }
        }
        let filtered_where = if filtered_conditions.is_empty() {
            String::new()
        } else {
            format!(" WHERE {}", filtered_conditions.join(" AND "))
        };
        Self {
            cte: format!(
                "WITH scoped_matches AS (\
                   SELECT mh.* FROM match_history mh{} \
                   ORDER BY mh.started_at DESC, mh.id DESC{}\
                 ), filtered_matches AS (\
                   SELECT mh.* FROM scoped_matches mh{}\
                 )",
                scope_where, limit, filtered_where
            ),
            params,
        }
    }
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

#[cfg(test)]
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

#[cfg(test)]
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

    for (index, record) in matches
        .iter()
        .filter(|record| has_played_result(record))
        .enumerate()
    {
        insert_match_history_record(tx, index, record)?;
    }

    Ok(())
}

fn has_played_result(record: &MatchHistoryRecord) -> bool {
    record
        .stages
        .iter()
        .any(|stage| stage.resolved && matches!(stage.winner, Some(0) | Some(1)))
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

    #[test]
    fn repository_upsert_and_delete_only_touch_the_selected_match() {
        let mut conn = Connection::open_in_memory().expect("open in-memory db");
        run_migrations(&mut conn).expect("run migrations");
        let first = local_win_record("match-1", "2026-07-01T00:00:00.000Z", "rival-a");
        let second = local_win_record("match-2", "2026-07-02T00:00:00.000Z", "rival-b");
        {
            let mut repository = HistoryRepository::new(&mut conn);
            repository.upsert_match(&first).expect("upsert first");
            repository.upsert_match(&second).expect("upsert second");
            let mut updated = first.clone();
            updated.player_names.luigi = "Renamed Rival".to_owned();
            updated.stages.truncate(1);
            repository.upsert_match(&updated).expect("replace first");
            repository.delete_match("match-2").expect("delete second");
        }

        let rows: u32 = conn
            .query_row("SELECT COUNT(*) FROM match_history", [], |row| row.get(0))
            .expect("count matches");
        let stages: u32 = conn
            .query_row(
                "SELECT COUNT(*) FROM match_stage_results WHERE match_id = 'match-1'",
                [],
                |row| row.get(0),
            )
            .expect("count stages");
        let opponent_name: String = conn
            .query_row(
                "SELECT opponent_player_name FROM match_history WHERE id = 'match-1'",
                [],
                |row| row.get(0),
            )
            .expect("load opponent name");
        assert_eq!(rows, 1);
        assert_eq!(stages, 1);
        assert_eq!(opponent_name, "Renamed Rival");

        let opponents = HistoryRepository::new(&mut conn)
            .opponents()
            .expect("load remaining opponents");
        assert_eq!(opponents.len(), 1);
        assert_eq!(opponents[0].player_id, "rival-a");
    }

    #[test]
    fn repository_discards_matches_without_a_decided_stage() {
        let mut conn = Connection::open_in_memory().expect("open in-memory db");
        run_migrations(&mut conn).expect("run migrations");
        let mut unplayed = sample_record();
        unplayed.id = "unplayed".to_owned();
        unplayed.status = MatchHistoryStatus::Stopped;
        unplayed.player_ids.luigi = "ghost-rival".to_owned();
        unplayed.player_names.luigi = "Ghost Rival".to_owned();
        unplayed.stages.clear();

        let mut repository = HistoryRepository::new(&mut conn);
        repository
            .upsert_match(&unplayed)
            .expect("discard unplayed match");

        let page = repository
            .history_page(&MatchHistoryPageRequest {
                filter: MatchHistoryFilter::default(),
                cursor: None,
                limit: 50,
            })
            .expect("load page");
        assert_eq!(page.total, 0);
        assert!(repository.opponents().expect("load opponents").is_empty());
    }

    #[test]
    fn legacy_import_discards_matches_without_a_decided_stage() {
        let mut conn = Connection::open_in_memory().expect("open in-memory db");
        run_migrations(&mut conn).expect("run migrations");
        let played = sample_record();
        let mut unplayed = played.clone();
        unplayed.id = "unplayed".to_owned();
        unplayed.status = MatchHistoryStatus::Stopped;
        unplayed.stages.clear();

        save_match_history_records(&mut conn, &[unplayed, played.clone()]).expect("import history");
        let loaded = load_match_history_records(&conn).expect("load history");

        assert_eq!(loaded.len(), 1);
        assert_eq!(loaded[0].id, played.id);
    }

    #[test]
    fn repository_orders_opponents_by_match_count() {
        let mut conn = Connection::open_in_memory().expect("open in-memory db");
        run_migrations(&mut conn).expect("run migrations");
        let records = [
            local_win_record("recent", "2026-07-03T00:00:00.000Z", "rival-a"),
            local_win_record("frequent-1", "2026-07-01T00:00:00.000Z", "rival-b"),
            local_win_record("frequent-2", "2026-07-02T00:00:00.000Z", "rival-b"),
        ];
        let mut repository = HistoryRepository::new(&mut conn);
        for record in &records {
            repository.upsert_match(record).expect("upsert match");
        }

        let opponents = repository.opponents().expect("load opponents");

        assert_eq!(opponents.len(), 2);
        assert_eq!(opponents[0].player_id, "rival-b");
        assert_eq!(opponents[0].matches, 2);
        assert_eq!(opponents[1].player_id, "rival-a");
        assert_eq!(opponents[1].matches, 1);
    }

    #[test]
    fn repository_filters_pages_and_aggregates_history() {
        let mut conn = Connection::open_in_memory().expect("open in-memory db");
        run_migrations(&mut conn).expect("run migrations");
        let win = local_win_record("win", "2026-07-01T00:00:00.000Z", "rival-a");
        let mut loss = local_win_record("loss", "2026-07-03T00:00:00.000Z", "rival-a");
        loss.stages.truncate(1);
        loss.stages[0].winner = Some(1);
        loss.stages[0].stage = Some(1);
        loss.stages[0].mario_match_wins = 0;
        loss.stages[0].luigi_match_wins = 1;
        let mut stopped = sample_record();
        stopped.id = "stopped".to_owned();
        stopped.started_at = "2026-07-02T00:00:00.000Z".to_owned();
        stopped.status = MatchHistoryStatus::Stopped;
        stopped.player_ids.luigi = "rival-b".to_owned();
        stopped.player_names.luigi = "Rival B".to_owned();
        {
            let mut repository = HistoryRepository::new(&mut conn);
            repository.upsert_match(&win).expect("upsert win");
            repository.upsert_match(&loss).expect("upsert loss");
            repository.upsert_match(&stopped).expect("upsert stopped");

            let dashboard = repository
                .dashboard(&MatchHistoryFilter::default())
                .expect("load dashboard");
            assert_eq!(dashboard.summary.wins, 1);
            assert_eq!(dashboard.summary.losses, 1);
            assert_eq!(dashboard.summary.stopped, 1);
            assert_eq!(dashboard.summary.game_wins, 3);
            assert_eq!(dashboard.summary.game_losses, 2);
            assert_eq!(dashboard.summary.streak, 1);
            assert!(matches!(
                dashboard.summary.streak_kind,
                Some(MatchHistoryStreakKind::Loss)
            ));
            assert_eq!(dashboard.trend.len(), 2);
            assert_eq!(dashboard.stages.len(), 2);

            let page = repository
                .history_page(&MatchHistoryPageRequest {
                    filter: MatchHistoryFilter::default(),
                    cursor: None,
                    limit: 2,
                })
                .expect("load first page");
            assert_eq!(page.total, 3);
            assert_eq!(page.matches.len(), 2);
            assert_eq!(page.matches[0].id, "loss");
            assert!(page.next_cursor.is_some());

            let next_page = repository
                .history_page(&MatchHistoryPageRequest {
                    filter: MatchHistoryFilter::default(),
                    cursor: page.next_cursor,
                    limit: 2,
                })
                .expect("load second page");
            assert_eq!(next_page.matches.len(), 1);
            assert_eq!(next_page.matches[0].id, "win");

            let filtered = repository
                .history_page(&MatchHistoryPageRequest {
                    filter: MatchHistoryFilter {
                        opponent_player_id: Some("rival-a".to_owned()),
                        stage: Some(0),
                        outcome: Some(MatchHistoryOutcome::Win),
                        ..MatchHistoryFilter::default()
                    },
                    cursor: None,
                    limit: 50,
                })
                .expect("load filtered page");
            assert_eq!(filtered.total, 1);
            assert_eq!(filtered.matches[0].id, "win");

            let completed = repository
                .history_page(&MatchHistoryPageRequest {
                    filter: MatchHistoryFilter {
                        outcome: Some(MatchHistoryOutcome::Completed),
                        ..MatchHistoryFilter::default()
                    },
                    cursor: None,
                    limit: 50,
                })
                .expect("load completed matches");
            assert_eq!(completed.total, 2);
            assert!(completed
                .matches
                .iter()
                .all(|record| matches!(record.status, MatchHistoryStatus::Completed)));
        }
    }

    #[test]
    fn trend_includes_nine_matches_before_the_visible_range() {
        let mut conn = Connection::open_in_memory().expect("open in-memory db");
        run_migrations(&mut conn).expect("run migrations");
        let mut repository = HistoryRepository::new(&mut conn);
        for index in 0..70 {
            let started_at = format!(
                "2026-07-{:02}T{:02}:{:02}:00.000Z",
                1 + index / (24 * 60),
                (index / 60) % 24,
                index % 60
            );
            let mut record = local_win_record(&format!("trend-{index:02}"), &started_at, "rival-a");
            if index == 10 {
                for (game_index, stage) in record.stages.iter_mut().enumerate() {
                    stage.winner = Some(1);
                    stage.mario_match_wins = 0;
                    stage.luigi_match_wins = u32::try_from(game_index + 1).expect("game index");
                }
            }
            repository
                .upsert_match(&record)
                .expect("upsert trend match");
        }

        let dashboard = repository
            .dashboard(&MatchHistoryFilter::default())
            .expect("load dashboard");

        assert_eq!(dashboard.trend.len(), 60);
        assert_eq!(dashboard.trend[0].match_id, "trend-10");
        assert!((dashboard.trend[0].rolling_win_rate - 0.9).abs() < f64::EPSILON);
    }

    #[test]
    fn recent_match_scope_is_applied_before_outcome_filter() {
        let mut conn = Connection::open_in_memory().expect("open in-memory db");
        run_migrations(&mut conn).expect("run migrations");
        let older_win = local_win_record("older-win", "2026-07-01T00:00:00.000Z", "rival");
        let mut latest_loss = local_win_record("latest-loss", "2026-07-02T00:00:00.000Z", "rival");
        latest_loss.stages.truncate(1);
        latest_loss.stages[0].winner = Some(1);
        latest_loss.stages[0].mario_match_wins = 0;
        latest_loss.stages[0].luigi_match_wins = 1;
        let mut repository = HistoryRepository::new(&mut conn);
        repository.upsert_match(&older_win).expect("upsert win");
        repository.upsert_match(&latest_loss).expect("upsert loss");

        let page = repository
            .history_page(&MatchHistoryPageRequest {
                filter: MatchHistoryFilter {
                    recent_matches: Some(1),
                    outcome: Some(MatchHistoryOutcome::Win),
                    ..MatchHistoryFilter::default()
                },
                cursor: None,
                limit: 50,
            })
            .expect("query recent wins");
        assert_eq!(page.total, 0);
    }

    fn local_win_record(id: &str, started_at: &str, opponent_id: &str) -> MatchHistoryRecord {
        let mut record = sample_record();
        record.id = id.to_owned();
        record.started_at = started_at.to_owned();
        record.player_ids.luigi = opponent_id.to_owned();
        record.player_names.luigi = opponent_id.to_owned();
        record.stages = vec![record.stages[0].clone(), record.stages[0].clone()];
        record.stages[0].game_index = 0;
        record.stages[1].game_index = 1;
        record.stages[1].mario_match_wins = 2;
        record
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
