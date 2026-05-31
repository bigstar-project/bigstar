import { invoke } from "@tauri-apps/api/core";
import "./styles.css";

type Role = "host" | "client";
type CourseMode = "random" | "select";
type Lives = "3" | "5" | "endless";

type Defaults = {
  signal_url: string;
  room_code: string;
  host_rom_path: string;
  client_rom_path: string;
  base_rom_path: string;
  port: number;
};

type GameSettings = {
  course_mode: CourseMode;
  wins: number;
  big_stars: number;
  lives: Lives;
  match_seed: string;
};

type LaunchRequest = {
  role: Role;
  signal_url: string;
  room_code: string;
  port: number;
  rom_path: string;
  settings: GameSettings;
};

type GenerateRomRequest = {
  source_rom: string;
  host_rom: string;
  client_rom: string;
  stage: number;
  settings: GameSettings;
};

type LaunchResponse = {
  log_dir: string;
  melon_pid: number;
  bridge_pid: number;
};

type GenerateRomResponse = {
  host_rom: string;
  client_rom: string;
};

type SessionStatus = {
  active: boolean;
  log_dir?: string;
  melon?: string;
  bridge?: string;
};

type PreflightResponse = {
  melonds_path: string;
  bridge_path: string;
  input_script: string;
  symbols_file: string;
  bridge_smoke: string;
};

const state = {
  defaults: null as Defaults | null,
  role: "host" as Role,
  signalUrl: "",
  roomCode: "",
  port: 8165,
  hostRomPath: "",
  clientRomPath: "",
  baseRomPath: "",
  courseMode: "random" as CourseMode,
  wins: 2,
  bigStars: 5,
  lives: "endless" as Lives,
  matchSeed: "",
  lastGeneratedStage: null as number | null,
  lastLogDir: "",
};

const app = document.querySelector<HTMLDivElement>("#app");
if (!app) {
  throw new Error("missing #app");
}
const appRoot = app;

function currentRomPath(): string {
  return state.role === "host" ? state.hostRomPath : state.clientRomPath;
}

function currentSettings(): GameSettings {
  return {
    course_mode: state.courseMode,
    wins: state.wins,
    big_stars: state.bigStars,
    lives: state.lives,
    match_seed: state.matchSeed.trim(),
  };
}

function setStatus(text: string, kind: "idle" | "ok" | "warn" | "error" = "idle") {
  const el = document.querySelector<HTMLDivElement>("#status");
  if (!el) return;
  el.textContent = text;
  el.dataset.kind = kind;
}

function setLogDir(logDir: string) {
  state.lastLogDir = logDir;
  const el = document.querySelector<HTMLElement>("#log-dir");
  if (!el) return;
  el.textContent = logDir || "not started";
}

function render() {
  const selectedStage = selectedStageLabel();
  const courseNote =
    state.courseMode === "select"
      ? "毎回選ぶはdirect routeでは未対応のため、現在は固定stage 0で起動します。"
      : "ランダムはmatch seedからstage 0-4を決めます。";

  appRoot.innerHTML = `
    <main class="shell">
      <section class="topbar">
        <div>
          <p class="eyebrow">NSMB Mario vs Luigi</p>
          <h1>対戦ランチャー</h1>
        </div>
        <div id="status" class="status" data-kind="idle">未接続</div>
      </section>

      <section class="grid">
        <form id="match-form" class="panel">
          <h2>接続</h2>
          <div class="segmented" role="radiogroup" aria-label="role">
            <button type="button" class="segment" data-role="host" aria-pressed="${state.role === "host"}">ホスト</button>
            <button type="button" class="segment" data-role="client" aria-pressed="${state.role === "client"}">参加</button>
          </div>

          <label>
            部屋コード
            <input id="room-code" value="${escapeHtml(state.roomCode)}" maxlength="64" autocomplete="off" />
          </label>
          <label>
            シグナリングサーバー URL
            <input id="signal-url" value="${escapeHtml(state.signalUrl)}" autocomplete="off" />
          </label>
          <label>
            UDP ポート
            <input id="port" type="number" min="1" max="65535" value="${state.port}" />
          </label>

          <h2>ROM</h2>
          <label>
            ホスト用 ROM
            <input id="host-rom" value="${escapeHtml(state.hostRomPath)}" autocomplete="off" />
          </label>
          <label>
            参加用 ROM
            <input id="client-rom" value="${escapeHtml(state.clientRomPath)}" autocomplete="off" />
          </label>
          <label>
            ベース ROM
            <input id="base-rom" value="${escapeHtml(state.baseRomPath)}" autocomplete="off" />
          </label>

          <div class="actions">
            <button id="preflight" class="secondary" type="button">起動前チェック</button>
            <button id="generate" class="secondary" type="button">ROM生成</button>
            <button id="start" class="primary" type="submit">開始</button>
            <button id="stop" class="secondary" type="button">停止</button>
          </div>
          <div class="log-path">
            <span>Log directory</span>
            <code id="log-dir">${escapeHtml(state.lastLogDir || "not started")}</code>
          </div>
        </form>

        <section class="panel">
          <h2>ゲーム設定</h2>
          <label>
            コース
            <select id="course-mode">
              <option value="random" ${state.courseMode === "random" ? "selected" : ""}>ランダム</option>
              <option value="select" ${state.courseMode === "select" ? "selected" : ""}>毎回選ぶ</option>
            </select>
          </label>
          <label>
            勝利数
            <select id="wins">
              ${[1, 2, 3].map((value) => option(value, state.wins)).join("")}
            </select>
          </label>
          <label>
            ビッグスター
            <select id="big-stars">
              ${[3, 5, 10].map((value) => option(value, state.bigStars)).join("")}
            </select>
          </label>
          <label>
            残機
            <select id="lives">
              <option value="3" ${state.lives === "3" ? "selected" : ""}>3</option>
              <option value="5" ${state.lives === "5" ? "selected" : ""}>5</option>
              <option value="endless" ${state.lives === "endless" ? "selected" : ""}>無限</option>
            </select>
          </label>
          <label>
            マッチシード
            <input id="match-seed" value="${escapeHtml(state.matchSeed)}" autocomplete="off" placeholder="ランダム時は必須" />
          </label>

          <div class="summary">
            <div>
              <span>操作キャラ</span>
              <strong>${state.role === "host" ? "Mario" : "Luigi"}</strong>
            </div>
            <div>
              <span>使用 ROM</span>
              <strong>${escapeHtml(currentRomPath() || "未設定")}</strong>
            </div>
            <div>
              <span>生成/起動 stage</span>
              <strong>${escapeHtml(selectedStage)}</strong>
            </div>
            <div>
              <span>コース処理</span>
              <strong>${escapeHtml(courseNote)}</strong>
            </div>
          </div>
        </section>
      </section>
    </main>
  `;

  bind();
}

function bind() {
  document.querySelectorAll<HTMLButtonElement>("[data-role]").forEach((button) => {
    button.addEventListener("click", () => {
      state.role = button.dataset.role as Role;
      render();
      pollStatus();
    });
  });

  input("#room-code", (value) => (state.roomCode = value));
  input("#signal-url", (value) => (state.signalUrl = value));
  input("#port", (value) => (state.port = Number(value)));
  input("#host-rom", (value) => (state.hostRomPath = value));
  input("#client-rom", (value) => (state.clientRomPath = value));
  input("#base-rom", (value) => (state.baseRomPath = value));
  input("#match-seed", (value) => {
    state.matchSeed = value;
    state.lastGeneratedStage = null;
  });
  select("#course-mode", (value) => {
    state.courseMode = value as CourseMode;
    state.lastGeneratedStage = null;
  });
  select("#wins", (value) => (state.wins = Number(value)));
  select("#big-stars", (value) => (state.bigStars = Number(value)));
  select("#lives", (value) => (state.lives = value as Lives));

  document.querySelector<HTMLButtonElement>("#preflight")?.addEventListener("click", preflightCheck);
  document.querySelector<HTMLButtonElement>("#generate")?.addEventListener("click", generateRoms);
  document.querySelector<HTMLFormElement>("#match-form")?.addEventListener("submit", async (event) => {
    event.preventDefault();
    syncFromDom();
    await startMatch();
  });

  document.querySelector<HTMLButtonElement>("#stop")?.addEventListener("click", async () => {
    try {
      await invoke("stop_match");
      setStatus("停止しました", "warn");
    } catch (error) {
      setStatus(String(error), "error");
    }
  });
}

function syncFromDom() {
  state.roomCode = value("#room-code");
  state.signalUrl = value("#signal-url");
  state.port = Number(value("#port"));
  state.hostRomPath = value("#host-rom");
  state.clientRomPath = value("#client-rom");
  state.baseRomPath = value("#base-rom");
  state.courseMode = value("#course-mode") as CourseMode;
  state.wins = Number(value("#wins"));
  state.bigStars = Number(value("#big-stars"));
  state.lives = value("#lives") as Lives;
  state.matchSeed = value("#match-seed");
}

async function preflightCheck() {
  try {
    setStatus("起動前チェック中", "idle");
    const response = await invoke<PreflightResponse>("preflight_check");
    console.info("preflight", response);
    setStatus("起動前チェックOK", "ok");
  } catch (error) {
    setStatus(String(error), "error");
  }
}

async function generateRoms() {
  syncFromDom();
  ensureMatchSeed();
  const stage = selectedStage();
  if (stage === null) {
    setStatus("マッチシードは10進数か0x始まりの16進数で指定してください", "error");
    return;
  }
  const request: GenerateRomRequest = {
    source_rom: state.baseRomPath,
    host_rom: state.hostRomPath,
    client_rom: state.clientRomPath,
    stage,
    settings: currentSettings(),
  };

  try {
    setStatus(`ROM生成中 stage=${stage}`, "idle");
    const response = await invoke<GenerateRomResponse>("generate_roms", { request });
    state.hostRomPath = response.host_rom;
    state.clientRomPath = response.client_rom;
    state.lastGeneratedStage = stage;
    render();
    setStatus(`ROM生成が完了しました stage=${stage}`, "ok");
  } catch (error) {
    setStatus(String(error), "error");
  }
}

async function startMatch() {
  ensureMatchSeed();
  const stage = selectedStage();
  if (stage === null) {
    setStatus("マッチシードは10進数か0x始まりの16進数で指定してください", "error");
    return;
  }

  const request: LaunchRequest = {
    role: state.role,
    signal_url: state.signalUrl,
    room_code: state.roomCode,
    port: state.port,
    rom_path: currentRomPath(),
    settings: currentSettings(),
  };

  try {
    setStatus(`起動中 stage=${stage}`, "idle");
    const response = await invoke<LaunchResponse>("start_match", { request });
    setLogDir(response.log_dir);
    setStatus(`起動済み melonDS:${response.melon_pid} bridge:${response.bridge_pid}`, "ok");
    console.info("log_dir", response.log_dir);
  } catch (error) {
    setStatus(String(error), "error");
  }
}

async function pollStatus() {
  try {
    const status = await invoke<SessionStatus>("session_status");
    if (status.log_dir) {
      setLogDir(status.log_dir);
    }
    if (!status.active) {
      setStatus("未接続", "idle");
      return;
    }
    setStatus(`実行中 melonDS:${status.melon ?? "-"} bridge:${status.bridge ?? "-"}`, "ok");
  } catch {
    setStatus("状態取得に失敗しました", "warn");
  }
}

function ensureMatchSeed() {
  if (state.courseMode === "random" && state.matchSeed.trim() === "") {
    state.matchSeed = String(generateSeed());
  }
}

function selectedStage(): number | null {
  if (state.courseMode === "select") {
    return 0;
  }
  const seed = parseSeed(state.matchSeed.trim());
  if (seed === null) {
    return null;
  }
  return Number(seed % 5n);
}

function selectedStageLabel(): string {
  const stage = selectedStage();
  if (stage === null) {
    return state.courseMode === "random" ? "seed未設定" : "0";
  }
  return String(stage);
}

function parseSeed(value: string): bigint | null {
  if (/^0x[0-9a-f]+$/i.test(value)) {
    return BigInt(value);
  }
  if (/^[0-9]+$/.test(value)) {
    return BigInt(value);
  }
  return null;
}

function generateSeed(): number {
  const bytes = new Uint32Array(1);
  crypto.getRandomValues(bytes);
  return bytes[0] || Date.now() >>> 0;
}

function input(selector: string, fn: (value: string) => void) {
  document.querySelector<HTMLInputElement>(selector)?.addEventListener("input", (event) => {
    fn((event.target as HTMLInputElement).value);
  });
}

function select(selector: string, fn: (value: string) => void) {
  document.querySelector<HTMLSelectElement>(selector)?.addEventListener("change", (event) => {
    fn((event.target as HTMLSelectElement).value);
    render();
  });
}

function value(selector: string): string {
  const el = document.querySelector<HTMLInputElement | HTMLSelectElement>(selector);
  return el?.value.trim() ?? "";
}

function option(value: number, selected: number): string {
  return `<option value="${value}" ${value === selected ? "selected" : ""}>${value}</option>`;
}

function escapeHtml(value: string): string {
  return value
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

async function init() {
  state.defaults = await invoke<Defaults>("get_defaults");
  state.signalUrl = state.defaults.signal_url;
  state.roomCode = state.defaults.room_code;
  state.port = state.defaults.port;
  state.hostRomPath = state.defaults.host_rom_path;
  state.clientRomPath = state.defaults.client_rom_path;
  state.baseRomPath = state.defaults.base_rom_path;
  state.matchSeed = String(generateSeed());
  render();
  await pollStatus();
  window.setInterval(pollStatus, 2000);
}

init().catch((error) => {
  render();
  setStatus(String(error), "error");
});
