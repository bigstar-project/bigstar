import { spawn } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import process from 'node:process';

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
    forwardedArgs.push(arg);
  }
  if (buildProfile !== null && buildProfile !== 'local' && buildProfile !== 'distribution') {
    console.error('--build-profile は local または distribution を指定してください');
    process.exit(1);
  }
  return { buildProfile, forwardedArgs };
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

const pnpmCli = process.env.npm_execpath;
if (!pnpmCli) {
  console.error('pnpm から build script を実行してください');
  process.exit(1);
}

const localEnv = loadLocalBuildEnv();
const { buildProfile, forwardedArgs } = parseBuildArgs(process.argv.slice(2));
const tauriBuildArgs =
  buildProfile === 'distribution'
    ? ensureCargoFeature(forwardedArgs, 'single-instance')
    : forwardedArgs;
const child = spawn(
  process.execPath,
  [pnpmCli, 'tauri', 'build', ...tauriBuildArgs],
  {
    cwd: process.cwd(),
    env: {
      ...process.env,
      ...localEnv,
      ...(buildProfile ? { NSMB_MVL_BUILD_PROFILE: buildProfile } : {}),
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
