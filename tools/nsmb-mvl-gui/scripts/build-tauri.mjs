import { spawn } from 'node:child_process';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import process from 'node:process';
import {
  loadEditionConfig,
  normalizeEdition,
  tauriEditionOverlay,
  writeTauriEditionOverlay,
} from './edition-config.mjs';

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

function parseBuildArgs(args) {
  const forwardedArgs = [];
  let buildProfile = null;
  let edition = process.env.NSMB_MVL_EDITION || 'insiders';
  let appVersion = process.env.NSMB_MVL_APP_VERSION || null;
  const extraConfigs = [];
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
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
  if (buildProfile !== null && buildProfile !== 'local' && buildProfile !== 'distribution') {
    console.error('--build-profile は local または distribution を指定してください');
    process.exit(1);
  }
  try {
    edition = normalizeEdition(edition);
  } catch (error) {
    console.error(error.message);
    process.exit(1);
  }
  if (appVersion !== null && !/^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$/.test(appVersion)) {
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

function mergeExtraConfigs(editionConfig, appVersion, extraConfigs) {
  let overlay = {};
  for (const configPath of extraConfigs) {
    const resolvedPath = resolve(process.cwd(), configPath);
    const extra = JSON.parse(readFileSync(resolvedPath, 'utf8'));
    overlay = mergeObjects(overlay, extra);
  }
  overlay = mergeObjects(
    overlay,
    tauriEditionOverlay(editionConfig, appVersion),
  );
  const outputPath = writeTauriEditionOverlay(
    editionConfig,
    appVersion,
    process.cwd(),
  );
  if (extraConfigs.length > 0) {
    writeFileSync(outputPath, `${JSON.stringify(overlay, null, 2)}\n`, 'utf8');
  }
  return outputPath;
}

const pnpmCli = process.env.npm_execpath;
if (!pnpmCli) {
  console.error('pnpm から build script を実行してください');
  process.exit(1);
}

const localEnv = loadLocalBuildEnv();
const {
  appVersion,
  buildProfile,
  edition,
  extraConfigs,
  forwardedArgs,
} = parseBuildArgs(process.argv.slice(2));
const editionConfig = loadEditionConfig(edition);
let tauriBuildArgs =
  buildProfile === 'distribution'
    ? ensureCargoFeature(forwardedArgs, 'single-instance')
    : forwardedArgs;
if (edition === 'insiders') {
  tauriBuildArgs = ensureCargoFeature(tauriBuildArgs, 'insiders-edition');
} else if (hasCargoFeature(tauriBuildArgs, 'insiders-edition')) {
  console.error('Public版へ insiders-edition feature は指定できません');
  process.exit(1);
}
const editionOverlayPath = mergeExtraConfigs(
  editionConfig,
  appVersion,
  extraConfigs,
);
tauriBuildArgs = [...tauriBuildArgs, '--config', editionOverlayPath];
const child = spawn(
  process.execPath,
  [pnpmCli, 'tauri', 'build', ...tauriBuildArgs],
  {
    cwd: process.cwd(),
    env: {
      ...process.env,
      ...localEnv,
      ...(buildProfile ? { NSMB_MVL_BUILD_PROFILE: buildProfile } : {}),
      NSMB_MVL_APP_VERSION: appVersion || process.env.npm_package_version,
      NSMB_MVL_APP_DATA_DIR_NAME: editionConfig.dataDirectoryName,
      NSMB_MVL_DEFAULT_SIGNAL_URL: editionConfig.defaultSignalUrl,
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
  process.exit(code ?? 1);
});
