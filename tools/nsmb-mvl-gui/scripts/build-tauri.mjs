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

const pnpmCli = process.env.npm_execpath;
if (!pnpmCli) {
  console.error('pnpm から build script を実行してください');
  process.exit(1);
}

const localEnv = loadLocalBuildEnv();
const child = spawn(
  process.execPath,
  [pnpmCli, 'tauri', 'build', ...process.argv.slice(2)],
  {
    cwd: process.cwd(),
    env: { ...process.env, ...localEnv },
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
