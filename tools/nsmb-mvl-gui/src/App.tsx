import { invoke } from "@tauri-apps/api/core";
import { useCallback, useEffect, useMemo, useState } from "react";

type Role = "host" | "client";
type CourseMode = "random" | "select";
type Lives = "3" | "5" | "endless";
type StatusKind = "idle" | "ok" | "warn" | "error";

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

type FormState = {
  role: Role;
  signalUrl: string;
  roomCode: string;
  port: number;
  hostRomPath: string;
  clientRomPath: string;
  baseRomPath: string;
  courseMode: CourseMode;
  wins: number;
  bigStars: number;
  lives: Lives;
  matchSeed: string;
};

const initialForm: FormState = {
  role: "host",
  signalUrl: "",
  roomCode: "",
  port: 8165,
  hostRomPath: "",
  clientRomPath: "",
  baseRomPath: "",
  courseMode: "random",
  wins: 2,
  bigStars: 5,
  lives: "endless",
  matchSeed: "",
};

export function App() {
  const [form, setForm] = useState<FormState>(initialForm);
  const [status, setStatus] = useState({
    text: "初期化中",
    kind: "idle" as StatusKind,
  });
  const [lastLogDir, setLastLogDir] = useState("");
  const [lastGeneratedStage, setLastGeneratedStage] = useState<number | null>(null);

  const currentRomPath = form.role === "host" ? form.hostRomPath : form.clientRomPath;
  const selectedStage = useMemo(
    () => selectedStageFrom(form.courseMode, form.matchSeed),
    [form.courseMode, form.matchSeed],
  );
  const selectedStageLabel =
    selectedStage === null ? (form.courseMode === "random" ? "seed未設定" : "0") : String(selectedStage);
  const courseNote =
    form.courseMode === "select"
      ? "Choose Each Time は direct route では未対応のため、現在は固定 stage 0 で起動します。"
      : "Match seed から stage 0-4 を決め、ROM生成と起動時に同じ stage を渡します。";

  const updateField = <K extends keyof FormState>(key: K, value: FormState[K]) => {
    setForm((current) => ({ ...current, [key]: value }));
    if (key === "courseMode" || key === "matchSeed") {
      setLastGeneratedStage(null);
    }
  };

  const pollStatus = useCallback(async () => {
    try {
      const response = await invoke<SessionStatus>("session_status");
      if (response.log_dir) {
        setLastLogDir(response.log_dir);
      }
      if (!response.active) {
        setStatus({ text: "未接続", kind: "idle" });
        return;
      }
      setStatus({
        text: `実行中 melonDS:${response.melon ?? "-"} bridge:${response.bridge ?? "-"}`,
        kind: "ok",
      });
    } catch {
      setStatus({ text: "状態取得に失敗しました", kind: "warn" });
    }
  }, []);

  useEffect(() => {
    let disposed = false;

    async function init() {
      try {
        const defaults = await invoke<Defaults>("get_defaults");
        if (disposed) return;
        setForm({
          role: "host",
          signalUrl: defaults.signal_url,
          roomCode: defaults.room_code,
          port: defaults.port,
          hostRomPath: defaults.host_rom_path,
          clientRomPath: defaults.client_rom_path,
          baseRomPath: defaults.base_rom_path,
          courseMode: "random",
          wins: 2,
          bigStars: 5,
          lives: "endless",
          matchSeed: String(generateSeed()),
        });
        await pollStatus();
      } catch (error) {
        if (!disposed) {
          setStatus({ text: String(error), kind: "error" });
        }
      }
    }

    void init();
    const timer = window.setInterval(pollStatus, 2000);
    return () => {
      disposed = true;
      window.clearInterval(timer);
    };
  }, [pollStatus]);

  const preflightCheck = async () => {
    try {
      setStatus({ text: "起動前チェック中", kind: "idle" });
      const response = await invoke<PreflightResponse>("preflight_check");
      console.info("preflight", response);
      setStatus({ text: "起動前チェック OK", kind: "ok" });
    } catch (error) {
      setStatus({ text: String(error), kind: "error" });
    }
  };

  const generateRoms = async () => {
    const nextForm = withRequiredSeed(form);
    if (nextForm.matchSeed !== form.matchSeed) {
      setForm(nextForm);
    }
    const stage = selectedStageFrom(nextForm.courseMode, nextForm.matchSeed);
    if (stage === null) {
      setStatus({ text: "Match seed は10進数、または 0x から始まる16進数で指定してください", kind: "error" });
      return;
    }

    const request: GenerateRomRequest = {
      source_rom: nextForm.baseRomPath,
      host_rom: nextForm.hostRomPath,
      client_rom: nextForm.clientRomPath,
      stage,
      settings: currentSettings(nextForm),
    };

    try {
      setStatus({ text: `ROM生成中 stage=${stage}`, kind: "idle" });
      const response = await invoke<GenerateRomResponse>("generate_roms", { request });
      setForm((current) => ({
        ...current,
        hostRomPath: response.host_rom,
        clientRomPath: response.client_rom,
      }));
      setLastGeneratedStage(stage);
      setStatus({ text: `ROM生成が完了しました stage=${stage}`, kind: "ok" });
    } catch (error) {
      setStatus({ text: String(error), kind: "error" });
    }
  };

  const startMatch = async () => {
    const nextForm = withRequiredSeed(form);
    if (nextForm.matchSeed !== form.matchSeed) {
      setForm(nextForm);
    }
    const stage = selectedStageFrom(nextForm.courseMode, nextForm.matchSeed);
    if (stage === null) {
      setStatus({ text: "Match seed は10進数、または 0x から始まる16進数で指定してください", kind: "error" });
      return;
    }

    const request: LaunchRequest = {
      role: nextForm.role,
      signal_url: nextForm.signalUrl,
      room_code: nextForm.roomCode,
      port: nextForm.port,
      rom_path: nextForm.role === "host" ? nextForm.hostRomPath : nextForm.clientRomPath,
      settings: currentSettings(nextForm),
    };

    try {
      setStatus({ text: `起動中 stage=${stage}`, kind: "idle" });
      const response = await invoke<LaunchResponse>("start_match", { request });
      setLastLogDir(response.log_dir);
      setStatus({
        text: `起動済み melonDS:${response.melon_pid} bridge:${response.bridge_pid}`,
        kind: "ok",
      });
    } catch (error) {
      setStatus({ text: String(error), kind: "error" });
    }
  };

  const stopMatch = async () => {
    try {
      await invoke("stop_match");
      setStatus({ text: "停止しました", kind: "warn" });
    } catch (error) {
      setStatus({ text: String(error), kind: "error" });
    }
  };

  return (
    <main className="mx-auto grid w-[min(1160px,calc(100vw-48px))] gap-6 py-8">
      <header className="flex items-end justify-between gap-6">
        <div>
          <p className="mb-1 text-sm font-bold text-slate-500">NSMB Mario vs Luigi</p>
          <h1 className="text-3xl font-bold text-slate-950">対戦ランチャー</h1>
        </div>
        <StatusPill kind={status.kind}>{status.text}</StatusPill>
      </header>

      <div className="grid grid-cols-[minmax(0,1.4fr)_minmax(320px,0.9fr)] gap-5 max-[860px]:grid-cols-1">
        <form
          className="grid gap-4 rounded-lg border border-slate-200 bg-white p-5 shadow-sm"
          onSubmit={(event) => {
            event.preventDefault();
            void startMatch();
          }}
        >
          <h2 className="text-lg font-bold text-slate-950">接続</h2>
          <div className="grid grid-cols-2 gap-1 rounded-lg border border-slate-300 bg-slate-100 p-1" role="radiogroup">
            <RoleButton active={form.role === "host"} onClick={() => updateField("role", "host")}>
              ホスト
            </RoleButton>
            <RoleButton active={form.role === "client"} onClick={() => updateField("role", "client")}>
              参加
            </RoleButton>
          </div>

          <TextField label="部屋コード" value={form.roomCode} maxLength={64} onChange={(value) => updateField("roomCode", value)} />
          <TextField label="シグナリングサーバー URL" value={form.signalUrl} onChange={(value) => updateField("signalUrl", value)} />
          <NumberField label="UDP ポート" value={form.port} min={1} max={65535} onChange={(value) => updateField("port", value)} />

          <h2 className="pt-2 text-lg font-bold text-slate-950">ROM</h2>
          <TextField label="ホスト用 ROM" value={form.hostRomPath} onChange={(value) => updateField("hostRomPath", value)} />
          <TextField label="参加用 ROM" value={form.clientRomPath} onChange={(value) => updateField("clientRomPath", value)} />
          <TextField label="ベース ROM" value={form.baseRomPath} onChange={(value) => updateField("baseRomPath", value)} />

          <div className="mt-1 flex flex-wrap justify-end gap-2">
            <ActionButton kind="secondary" type="button" onClick={() => void preflightCheck()}>
              起動前チェック
            </ActionButton>
            <ActionButton kind="secondary" type="button" onClick={() => void generateRoms()}>
              ROM生成
            </ActionButton>
            <ActionButton kind="primary" type="submit">
              開始
            </ActionButton>
            <ActionButton kind="secondary" type="button" onClick={() => void stopMatch()}>
              停止
            </ActionButton>
          </div>

          <div className="grid gap-1 pt-1 text-xs font-bold text-slate-500">
            <span>Log directory</span>
            <code className="overflow-wrap-anywhere rounded-md border border-slate-200 bg-slate-50 px-3 py-2 font-mono text-xs font-semibold text-slate-800">
              {lastLogDir || "not started"}
            </code>
          </div>
        </form>

        <section className="grid content-start gap-4 rounded-lg border border-slate-200 bg-white p-5 shadow-sm">
          <h2 className="text-lg font-bold text-slate-950">ゲーム設定</h2>
          <SelectField label="コース" value={form.courseMode} onChange={(value) => updateField("courseMode", value as CourseMode)}>
            <option value="random">ランダム</option>
            <option value="select">毎回選ぶ</option>
          </SelectField>
          <SelectField label="勝利数" value={String(form.wins)} onChange={(value) => updateField("wins", Number(value))}>
            {[1, 2, 3].map((value) => (
              <option key={value} value={value}>
                {value}
              </option>
            ))}
          </SelectField>
          <SelectField label="ビッグスター" value={String(form.bigStars)} onChange={(value) => updateField("bigStars", Number(value))}>
            {[3, 5, 10].map((value) => (
              <option key={value} value={value}>
                {value}
              </option>
            ))}
          </SelectField>
          <SelectField label="残機" value={form.lives} onChange={(value) => updateField("lives", value as Lives)}>
            <option value="3">3</option>
            <option value="5">5</option>
            <option value="endless">無限</option>
          </SelectField>
          <TextField
            label="Match seed"
            value={form.matchSeed}
            placeholder="ランダム時は空でも自動生成"
            onChange={(value) => updateField("matchSeed", value)}
          />

          <div className="mt-1 grid gap-3 border-t border-slate-200 pt-4">
            <SummaryItem label="操作キャラ" value={form.role === "host" ? "Mario" : "Luigi"} />
            <SummaryItem label="使用 ROM" value={currentRomPath || "未設定"} />
            <SummaryItem
              label="生成/起動 stage"
              value={lastGeneratedStage === null ? selectedStageLabel : `${selectedStageLabel}（生成済み）`}
            />
            <SummaryItem label="コース処理" value={courseNote} />
          </div>
        </section>
      </div>
    </main>
  );
}

function StatusPill({ children, kind }: { children: string; kind: StatusKind }) {
  const colors: Record<StatusKind, string> = {
    idle: "border-slate-300 text-slate-600",
    ok: "border-emerald-300 text-emerald-800",
    warn: "border-amber-300 text-amber-800",
    error: "border-rose-300 text-rose-800",
  };
  return (
    <div className={`min-h-10 max-w-[48ch] overflow-wrap-anywhere rounded-lg border bg-white px-3 py-2 ${colors[kind]}`}>
      {children}
    </div>
  );
}

function RoleButton({ active, children, onClick }: { active: boolean; children: string; onClick: () => void }) {
  return (
    <button
      type="button"
      aria-pressed={active}
      className={`min-h-9 rounded-md px-3 font-semibold transition ${
        active ? "bg-white text-slate-950 shadow-sm" : "text-slate-600 hover:bg-slate-50"
      }`}
      onClick={onClick}
    >
      {children}
    </button>
  );
}

function TextField({
  label,
  value,
  maxLength,
  placeholder,
  onChange,
}: {
  label: string;
  value: string;
  maxLength?: number;
  placeholder?: string;
  onChange: (value: string) => void;
}) {
  return (
    <label className="grid gap-1.5 text-sm font-bold text-slate-700">
      {label}
      <input
        className="min-h-10 rounded-md border border-slate-300 bg-white px-3 py-2 font-normal text-slate-950 outline-none focus:border-blue-600 focus:ring-2 focus:ring-blue-100"
        value={value}
        maxLength={maxLength}
        placeholder={placeholder}
        autoComplete="off"
        onChange={(event) => onChange(event.target.value)}
      />
    </label>
  );
}

function NumberField({
  label,
  value,
  min,
  max,
  onChange,
}: {
  label: string;
  value: number;
  min: number;
  max: number;
  onChange: (value: number) => void;
}) {
  return (
    <label className="grid gap-1.5 text-sm font-bold text-slate-700">
      {label}
      <input
        className="min-h-10 rounded-md border border-slate-300 bg-white px-3 py-2 font-normal text-slate-950 outline-none focus:border-blue-600 focus:ring-2 focus:ring-blue-100"
        type="number"
        min={min}
        max={max}
        value={value}
        onChange={(event) => onChange(Number(event.target.value))}
      />
    </label>
  );
}

function SelectField({
  label,
  value,
  children,
  onChange,
}: {
  label: string;
  value: string;
  children: React.ReactNode;
  onChange: (value: string) => void;
}) {
  return (
    <label className="grid gap-1.5 text-sm font-bold text-slate-700">
      {label}
      <select
        className="min-h-10 rounded-md border border-slate-300 bg-white px-3 py-2 font-normal text-slate-950 outline-none focus:border-blue-600 focus:ring-2 focus:ring-blue-100"
        value={value}
        onChange={(event) => onChange(event.target.value)}
      >
        {children}
      </select>
    </label>
  );
}

function ActionButton({
  children,
  kind,
  type,
  onClick,
}: {
  children: string;
  kind: "primary" | "secondary";
  type: "button" | "submit";
  onClick?: () => void;
}) {
  const styles =
    kind === "primary"
      ? "border-blue-700 bg-blue-600 text-white hover:bg-blue-700"
      : "border-slate-300 bg-white text-slate-800 hover:bg-slate-50";
  return (
    <button type={type} className={`min-h-10 min-w-24 rounded-md border px-4 font-bold transition ${styles}`} onClick={onClick}>
      {children}
    </button>
  );
}

function SummaryItem({ label, value }: { label: string; value: string }) {
  return (
    <div className="grid gap-0.5">
      <span className="text-xs font-bold text-slate-500">{label}</span>
      <strong className="overflow-wrap-anywhere text-sm text-slate-950">{value}</strong>
    </div>
  );
}

function currentSettings(form: FormState): GameSettings {
  return {
    course_mode: form.courseMode,
    wins: form.wins,
    big_stars: form.bigStars,
    lives: form.lives,
    match_seed: form.matchSeed.trim(),
  };
}

function withRequiredSeed(form: FormState): FormState {
  if (form.courseMode === "random" && form.matchSeed.trim() === "") {
    return { ...form, matchSeed: String(generateSeed()) };
  }
  return form;
}

function selectedStageFrom(courseMode: CourseMode, matchSeed: string): number | null {
  if (courseMode === "select") {
    return 0;
  }
  const seed = parseSeed(matchSeed.trim());
  if (seed === null) {
    return null;
  }
  return Number(seed % 5n);
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
