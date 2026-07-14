CREATE TABLE app_meta (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);

CREATE TABLE match_history (
  id TEXT PRIMARY KEY,
  list_index INTEGER NOT NULL,
  started_at TEXT NOT NULL,
  status TEXT NOT NULL,
  role TEXT NOT NULL,
  room_code TEXT NOT NULL,
  log_dir TEXT NOT NULL,
  local_player_id TEXT,
  local_player_name TEXT NOT NULL,
  opponent_player_id TEXT,
  opponent_player_name TEXT NOT NULL,
  local_wins INTEGER NOT NULL,
  opponent_wins INTEGER NOT NULL,
  match_winner TEXT,
  record_json TEXT NOT NULL,
  updated_at_unix_ms INTEGER NOT NULL
);

CREATE INDEX idx_match_history_started_at
  ON match_history(started_at DESC);

CREATE INDEX idx_match_history_opponent_started_at
  ON match_history(opponent_player_id, started_at DESC);

CREATE INDEX idx_match_history_status_started_at
  ON match_history(status, started_at DESC);

CREATE TABLE match_stage_results (
  match_id TEXT NOT NULL REFERENCES match_history(id) ON DELETE CASCADE,
  game_index INTEGER NOT NULL,
  stage INTEGER,
  frame INTEGER NOT NULL,
  local_stars INTEGER NOT NULL,
  opponent_stars INTEGER NOT NULL,
  local_displayed_stars INTEGER NOT NULL,
  opponent_displayed_stars INTEGER NOT NULL,
  local_collected_stars INTEGER NOT NULL,
  opponent_collected_stars INTEGER NOT NULL,
  local_lives INTEGER NOT NULL,
  opponent_lives INTEGER NOT NULL,
  local_deaths INTEGER NOT NULL,
  opponent_deaths INTEGER NOT NULL,
  local_dead INTEGER NOT NULL,
  opponent_dead INTEGER NOT NULL,
  winner TEXT,
  resolved INTEGER NOT NULL,
  stage_line TEXT NOT NULL,
  PRIMARY KEY (match_id, game_index)
);

CREATE INDEX idx_stage_results_stage
  ON match_stage_results(stage);

CREATE INDEX idx_stage_results_winner
  ON match_stage_results(winner);
