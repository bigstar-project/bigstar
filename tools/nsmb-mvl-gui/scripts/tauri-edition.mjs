import { spawn } from 'node:child_process';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import process from 'node:process';
import { normalizeBuildProfile } from './build-profile-config.mjs';
import {
  loadEditionConfig,
  normalizeEdition,
  tauriEditionOverlay,
  writeTauriEditionOverlay,
} from './edition-config.mjs';
import { stageEditionArtifacts } from './stage-edition-artifacts.mjs';

function parseEnvFile(content) {
  const entries = {};
  for (const rawLine of content.split(/\r?\n/)) {
    const line = rawLine.trim();
    if (line.length === 0 || line.startsWith('#')) {
      continue;
    }

    const separatorIndex = line.indexOf('=');
    if (separatorIndex === -1) {
      continue;
    }

    const key = line.slice(0, separatorIndex).trim();
    let value = line.slice(separatorIndex + 1).trim();
    if (
      (value.startsWith('"') && value.endsWith('"')) ||
      (value.startsWith("'") && value.endsWith("'"))
    ) {
      value = value.slice(1, -1);
    }
    entries[key] = value;
  }
  return entries;
}

function loadLocalBuildEnv() {
  const envPath = resolve(process.cwd(), '.env.local');
  if (!existsSync(envPath)) {
    return {};
  }
  return parseEnvFile(readFileSync(envPath, 'utf8'));
}

function parseTauriArgs(args, command) {
  const forwardedArgs = [];
  let buildProfile = command === 'dev' ? 'local' : null;
  let edition = process.env.NSMB_MVL_EDITION || 'insiders';
  let appVersion = process.env.NSMB_MVL_APP_VERSION || null;
  const extraConfigs = [];
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === '--') {
      continue;
    }
    if (arg === '--build-profile') {
      buildProfile = args[i + 1] ?? null;
      i++;
      continue;
    }
    if (arg.startsWith('--build-profile=')) {
      buildProfile = arg.slice('--build-profile='.length);
      continue;
    }
    if (arg === '--edition') {
      edition = args[i + 1] ?? '';
      i++;
      continue;
    }
    if (arg.startsWith('--edition=')) {
      edition = arg.slice('--edition='.length);
      continue;
    }
    if (arg === '--app-version') {
      appVersion = args[i + 1] ?? '';
      i++;
      continue;
    }
    if (arg.startsWith('--app-version=')) {
      appVersion = arg.slice('--app-version='.length);
      continue;
    }
    if (arg === '--config' || arg === '-c') {
      extraConfigs.push(args[i + 1] ?? '');
      i++;
      continue;
    }
    if (arg.startsWith('--config=')) {
      extraConfigs.push(arg.slice('--config='.length));
      continue;
    }
    forwardedArgs.push(arg);
  }
  if (buildProfile !== null) {
    try {
      buildProfile = normalizeBuildProfile(buildProfile);
    } catch (error) {
      console.error(error.message);
      process.exit(1);
    }
  }
  if (command === 'dev' && buildProfile !== 'local') {
    console.error('dev コマンドでは distribution profile を使用できません');
    process.exit(1);
  }
  try {
    edition = normalizeEdition(edition);
  } catch (error) {
    console.error(error.message);
    process.exit(1);
  }
  if (
    appVersion !== null &&
    !/^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$/.test(appVersion)
  ) {
    console.error('--app-version は SemVer 形式で指定してください');
    process.exit(1);
  }
  if (extraConfigs.some((config) => config.trim() === '')) {
    console.error('--config には設定ファイルのパスを指定してください');
    process.exit(1);
  }
  return { appVersion, buildProfile, edition, extraConfigs, forwardedArgs };
}

function splitFeatureList(value) {
  return value
    .split(/[\s,]+/)
    .map((feature) => feature.trim())
    .filter(Boolean);
}

function joinFeatureList(features) {
  return [...new Set(features)].join(',');
}

function ensureCargoFeature(args, feature) {
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === '--features') {
      const features = splitFeatureList(args[i + 1] ?? '');
      if (!features.includes(feature)) {
        args[i + 1] = joinFeatureList([...features, feature]);
      }
      return args;
    }
    if (arg.startsWith('--features=')) {
      const features = splitFeatureList(arg.slice('--features='.length));
      if (!features.includes(feature)) {
        args[i] = `--features=${joinFeatureList([...features, feature])}`;
      }
      return args;
    }
  }
  return [...args, '--features', feature];
}

function hasCargoFeature(args, feature) {
  return args.some((arg, index) => {
    if (arg === '--features') {
      return splitFeatureList(args[index + 1] ?? '').includes(feature);
    }
    return (
      arg.startsWith('--features=') &&
      splitFeatureList(arg.slice('--features='.length)).includes(feature)
    );
  });
}

function mergeObjects(base, override) {
  if (
    base &&
    override &&
    typeof base === 'object' &&
    typeof override === 'object' &&
    !Array.isArray(base) &&
    !Array.isArray(override)
  ) {
    const result = { ...base };
    for (const [key, value] of Object.entries(override)) {
      result[key] = key in result ? mergeObjects(result[key], value) : value;
    }
    return result;
  }
  return override;
}

function mergeExtraConfigs(
  editionConfig,
  appVersion,
  extraConfigs,
  development,
) {
  let overlay = {};
  for (const configPath of extraConfigs) {
    const resolvedPath = resolve(process.cwd(), configPath);
    const extra = JSON.parse(readFileSync(resolvedPath, 'utf8'));
    overlay = mergeObjects(overlay, extra);
  }
  overlay = mergeObjects(
    overlay,
    tauriEditionOverlay(editionConfig, appVersion, { development }),
  );
  const outputPath = writeTauriEditionOverlay(
    editionConfig,
    appVersion,
    process.cwd(),
    { development },
  );
  if (extraConfigs.length > 0) {
    writeFileSync(outputPath, `${JSON.stringify(overlay, null, 2)}\n`, 'utf8');
  }
  return outputPath;
}

const pnpmCli = process.env.npm_execpath;
if (!pnpmCli) {
  console.error('pnpm から Tauri script を実行してください');
  process.exit(1);
}

const [command, ...commandArgs] = process.argv.slice(2);
if (command !== 'dev' && command !== 'build') {
  console.error('Usage: tauri-edition.mjs <dev|build> [options]');
  process.exit(1);
}

const localEnv = loadLocalBuildEnv();
const {
  appVersion,
  buildProfile,
  edition,
  extraConfigs,
  forwardedArgs,
} = parseTauriArgs(commandArgs, command);
const editionConfig = loadEditionConfig(edition);
let tauriArgs =
  command === 'build' && buildProfile === 'distribution'
    ? ensureCargoFeature(forwardedArgs, 'single-instance')
    : forwardedArgs;
if (edition === 'insiders') {
  tauriArgs = ensureCargoFeature(tauriArgs, 'insiders-edition');
} else if (hasCargoFeature(tauriArgs, 'insiders-edition')) {
  console.error('Public版へ insiders-edition feature は指定できません');
  process.exit(1);
}
const editionOverlayPath = mergeExtraConfigs(
  editionConfig,
  appVersion,
  extraConfigs,
  command === 'dev',
);
tauriArgs = [...tauriArgs, '--config', editionOverlayPath];
const buildStartedAt = Date.now();
const resolvedAppVersion =
  appVersion || process.env.npm_package_version || '0.0.0';
const targetRoot = process.env.CARGO_TARGET_DIR
  ? resolve(process.cwd(), process.env.CARGO_TARGET_DIR)
  : resolve(process.cwd(), 'src-tauri', 'target');
const child = spawn(
  process.execPath,
  [pnpmCli, 'tauri', command, ...tauriArgs],
  {
    cwd: process.cwd(),
    env: {
      ...process.env,
      ...localEnv,
      ...(buildProfile ? { NSMB_MVL_BUILD_PROFILE: buildProfile } : {}),
      NSMB_MVL_APP_VERSION: resolvedAppVersion,
      NSMB_MVL_APP_DATA_DIR_NAME: editionConfig.dataDirectoryName,
      NSMB_MVL_DEFAULT_SIGNAL_URL: editionConfig.defaultSignalUrl,
      NSMB_MVL_DEV_PORT: String(editionConfig.devPort),
      NSMB_MVL_EDITION: edition,
    },
    shell: false,
    stdio: 'inherit',
  },
);

child.on('exit', (code, signal) => {
  if (signal) {
    process.kill(process.pid, signal);
    return;
  }
  if (code === 0 && command === 'build') {
    try {
      const profile =
        tauriArgs.includes('--debug') || tauriArgs.includes('-d')
          ? 'debug'
          : 'release';
      const destination = stageEditionArtifacts({
        appVersion: resolvedAppVersion,
        buildProfile: buildProfile || 'local',
        buildStartedAt,
        editionConfig,
        guiRoot: process.cwd(),
        includeBundles: !tauriArgs.includes('--no-bundle'),
        profile,
        targetRoot,
      });
      console.log(`staged ${edition} artifacts: ${destination}`);
    } catch (error) {
      console.error(error instanceof Error ? error.message : error);
      process.exit(1);
      return;
    }
  }
  process.exit(code ?? 1);
});
