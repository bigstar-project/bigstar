use crate::models::{CourseMode, GameSettings, LaunchRequest, Lives};

pub(crate) fn validate_request(request: &LaunchRequest) -> Result<(), String> {
    validate_settings(&request.settings)?;
    let room = request.room_code.trim();
    if room.is_empty()
        || room.len() > 64
        || !room
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || b == b'_' || b == b'-')
    {
        return Err("部屋コードは ^[A-Za-z0-9_-]{1,64}$ にしてください".into());
    }
    let signal = request.signal_url.trim();
    if !(signal.starts_with("ws://") || signal.starts_with("wss://")) {
        return Err("シグナリングサーバーは ws:// または wss:// で始めてください".into());
    }
    Ok(())
}

pub(crate) fn validate_settings(settings: &GameSettings) -> Result<(), String> {
    if settings.wins < 1 || settings.wins > 3 {
        return Err("勝利数は 1-3 にしてください".into());
    }
    let max_games = max_games_for_wins(settings.wins);
    if settings.course_stages.len() != max_games {
        return Err(format!(
            "コース列は勝利数{}に対して{}試合分にしてください",
            settings.wins, max_games
        ));
    }
    if settings.course_stages.iter().any(|stage| *stage > 4) {
        return Err("コースは 0-4 にしてください".into());
    }
    if matches!(settings.course_mode, CourseMode::Random)
        && has_duplicate_stage(&settings.course_stages)
    {
        return Err("ランダムコースでは同じコースを2度選べません".into());
    }
    if !matches!(settings.big_stars, 3 | 5 | 10) {
        return Err("ビッグスターは 3/5/10 のいずれかにしてください".into());
    }
    if settings.input_delay_frames > 16 {
        return Err("InputDelayFrames は 0-16 にしてください".into());
    }
    if settings.input_max_frame_lead > 16 {
        return Err("InputMaxFrameLead は 0-16 にしてください".into());
    }
    if settings.rng_seeds.len() != max_games {
        return Err(format!(
            "RNG seed は勝利数{}に対して{}試合分にしてください",
            settings.wins, max_games
        ));
    }
    for seed in &settings.rng_seeds {
        parse_match_seed(seed.trim())?;
    }
    parse_match_seed(settings.match_seed.trim())?;
    if settings
        .rng_seeds
        .first()
        .is_some_and(|seed| seed.trim() != settings.match_seed.trim())
    {
        return Err("match_seed は RNG seed 列の先頭と一致させてください".into());
    }
    Ok(())
}

pub(crate) fn selected_stage(settings: &GameSettings, fallback_stage: u8) -> Result<u8, String> {
    if let Some(stage) = settings.course_stages.first() {
        return Ok((*stage).min(4));
    }
    match settings.course_mode {
        CourseMode::Random => Ok((parse_match_seed(settings.match_seed.trim())? % 5) as u8),
        CourseMode::Select => Ok(fallback_stage.min(4)),
    }
}

fn parse_match_seed(value: &str) -> Result<u32, String> {
    if value.is_empty() {
        return Err("ランダムコースではマッチシードが必要です".into());
    }
    let parsed = if let Some(hex) = value
        .strip_prefix("0x")
        .or_else(|| value.strip_prefix("0X"))
    {
        u32::from_str_radix(hex, 16)
    } else {
        value.parse::<u32>()
    };
    parsed.map_err(|_| "マッチシードは10進数か0x始まりの16進数で指定してください".to_owned())
}

fn has_duplicate_stage(stages: &[u8]) -> bool {
    let mut seen = [false; 5];
    for stage in stages {
        let index = usize::from(*stage);
        if index >= seen.len() {
            continue;
        }
        if seen[index] {
            return true;
        }
        seen[index] = true;
    }
    false
}

pub(crate) fn course_mode_value(course_mode: CourseMode) -> &'static str {
    match course_mode {
        CourseMode::Random => "random",
        CourseMode::Select => "select",
    }
}

pub(crate) fn lives_value(lives: Lives) -> &'static str {
    match lives {
        Lives::Three => "3",
        Lives::Five => "5",
        Lives::Endless => "endless",
    }
}

fn max_games_for_wins(wins: u8) -> usize {
    usize::from(wins.saturating_mul(2).saturating_sub(1))
}
