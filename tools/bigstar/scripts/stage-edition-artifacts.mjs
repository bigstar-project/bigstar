import {
  copyFileSync,
  cpSync,
  existsSync,
  mkdirSync,
  readdirSync,
  renameSync,
  rmSync,
  statSync,
  writeFileSync,
} from 'node:fs';
import { basename, dirname, relative, resolve, sep } from 'node:path';
import { spawnSync } from 'node:child_process';

const RUNTIME_FILES = [
  'bigstar.exe',
  'melonDS.exe',
  'bigstar-net-bridge.exe',
  'bigstar-rule-cpu.exe',
];

function assertGeneratedPath(path, generatedRoot) {
  const normalizedRoot = `${resolve(generatedRoot)}${sep}`;
  if (!resolve(path).startsWith(normalizedRoot)) {
    throw new Error(`成果物ディレクトリ外のパスは操作できません: ${path}`);
  }
}

function renameGeneratedDirectory(source, destination) {
  try {
    renameSync(source, destination);
  } catch (error) {
    if (process.platform !== 'win32' || error?.code !== 'EPERM') {
      throw error;
    }
    // Some Windows hosts reject libuv's rename for executable payloads while
    // the native directory move succeeds. Pass paths as data, never shell code.
    const moved = spawnSync('powershell.exe', [
      '-NoProfile', '-NonInteractive', '-Command',
      '$ErrorActionPreference = "Stop"; if (Test-Path -LiteralPath $env:BIGSTAR_STAGE_DESTINATION) { throw "Artifact destination already exists" }; Move-Item -LiteralPath $env:BIGSTAR_STAGE_SOURCE -Destination $env:BIGSTAR_STAGE_DESTINATION',
    ], {
      env: {
        ...process.env,
        BIGSTAR_STAGE_SOURCE: source,
        BIGSTAR_STAGE_DESTINATION: destination,
      },
      windowsHide: true,
      encoding: 'utf8',
      timeout: 30_000,
    });
    if (moved.error || moved.status !== 0) {
      throw new Error(`成果物の移動に失敗しました: ${moved.stderr || moved.error || error}`);
    }
  }
}

function listFiles(root) {
  if (!existsSync(root)) {
    return [];
  }
  const files = [];
  for (const entry of readdirSync(root, { withFileTypes: true })) {
    const path = resolve(root, entry.name);
    if (entry.isDirectory()) {
      files.push(...listFiles(path));
    } else if (entry.isFile()) {
      files.push(path);
    }
  }
  return files;
}

function copyRuntimePayload(sourceRoot, destinationRoot) {
  const copied = [];
  for (const fileName of RUNTIME_FILES) {
    const source = resolve(sourceRoot, fileName);
    if (!existsSync(source)) {
      if (fileName === 'bigstar.exe') {
        throw new Error(`Tauri実行ファイルが見つかりません: ${source}`);
      }
      continue;
    }
    const destination = resolve(destinationRoot, fileName);
    copyFileSync(source, destination);
    copied.push(destination);
  }

  const resourcesSource = resolve(sourceRoot, 'resources');
  if (existsSync(resourcesSource)) {
    const resourcesDestination = resolve(destinationRoot, 'resources');
    cpSync(resourcesSource, resourcesDestination, { recursive: true });
    copied.push(...listFiles(resourcesDestination));
  }
  return copied;
}

function copyCurrentInstallers({
  sourceRoot,
  destinationRoot,
  displayName,
  buildStartedAt,
}) {
  const bundleRoot = resolve(sourceRoot, 'bundle');
  const installerRoot = resolve(destinationRoot, 'installers');
  const prefix = `${displayName}_`;
  const freshnessThreshold = buildStartedAt - 5_000;
  const copied = [];

  for (const source of listFiles(bundleRoot)) {
    const stats = statSync(source);
    if (
      stats.mtimeMs < freshnessThreshold ||
      !basename(source).startsWith(prefix)
    ) {
      continue;
    }
    mkdirSync(installerRoot, { recursive: true });
    const destination = resolve(installerRoot, basename(source));
    copyFileSync(source, destination);
    copied.push(destination);
  }
  return copied;
}

export function editionArtifactDirectory(
  guiRoot,
  edition,
  profile = 'release',
) {
  return resolve(guiRoot, 'artifacts', edition, profile);
}

export function stageEditionArtifacts({
  appVersion,
  buildProfile,
  buildStartedAt,
  editionConfig,
  guiRoot,
  includeBundles,
  profile,
  targetRoot,
}) {
  const generatedRoot = resolve(guiRoot, 'artifacts');
  const destinationRoot = editionArtifactDirectory(
    guiRoot,
    editionConfig.edition,
    profile,
  );
  const temporaryRoot = resolve(
    dirname(destinationRoot),
    `.${profile}-staging-${process.pid}`,
  );
  assertGeneratedPath(destinationRoot, generatedRoot);
  assertGeneratedPath(temporaryRoot, generatedRoot);

  rmSync(temporaryRoot, { force: true, recursive: true });
  mkdirSync(temporaryRoot, { recursive: true });

  const sourceRoot = resolve(targetRoot, profile);
  const copied = copyRuntimePayload(sourceRoot, temporaryRoot);
  if (includeBundles) {
    copied.push(
      ...copyCurrentInstallers({
        sourceRoot,
        destinationRoot: temporaryRoot,
        displayName: editionConfig.displayName,
        buildStartedAt,
      }),
    );
  }

  const manifest = {
    appVersion,
    buildProfile,
    edition: editionConfig.edition,
    generatedAt: new Date().toISOString(),
    identifier: editionConfig.identifier,
    profile,
    rustCache: 'shared',
    files: copied.map((path) => {
      const stats = statSync(path);
      return {
        path: relative(temporaryRoot, path).replaceAll('\\', '/'),
        size: stats.size,
      };
    }),
  };
  writeFileSync(
    resolve(temporaryRoot, 'artifact-manifest.json'),
    `${JSON.stringify(manifest, null, 2)}\n`,
    'utf8',
  );

  rmSync(destinationRoot, { force: true, recursive: true });
  renameGeneratedDirectory(temporaryRoot, destinationRoot);
  return destinationRoot;
}
