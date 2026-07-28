import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const SEMVER_PATTERN =
  /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$/;

function readJson(path) {
  return JSON.parse(readFileSync(path, 'utf8'));
}

function replaceVersion(content, pattern, version, fileName) {
  if (!pattern.test(content)) {
    throw new Error(`${fileName}からBigstarのバージョンを検出できません`);
  }
  return content.replace(pattern, `$1${version}$2`);
}

export function setBigstarVersion(bigstarRoot, version) {
  if (!SEMVER_PATTERN.test(version)) {
    throw new Error(`SemVer形式ではないバージョンです: ${version}`);
  }

  const packagePath = resolve(bigstarRoot, 'package.json');
  const tauriConfigPath = resolve(bigstarRoot, 'src-tauri', 'tauri.conf.json');
  const cargoManifestPath = resolve(bigstarRoot, 'src-tauri', 'Cargo.toml');
  const cargoLockPath = resolve(bigstarRoot, 'src-tauri', 'Cargo.lock');

  const packageJson = readJson(packagePath);
  const tauriConfig = readJson(tauriConfigPath);
  const cargoManifest = readFileSync(cargoManifestPath, 'utf8');
  const cargoLock = readFileSync(cargoLockPath, 'utf8');
  const cargoVersion = cargoManifest.match(
    /^(\[package\][\s\S]*?^version = ")([^"]+)(")/m,
  )?.[2];
  const cargoLockVersion = cargoLock.match(
    /^(\[\[package\]\]\r?\nname = "bigstar"\r?\nversion = ")([^"]+)(")/m,
  )?.[2];

  const currentVersions = [
    packageJson.version,
    tauriConfig.version,
    cargoVersion,
    cargoLockVersion,
  ];
  if (
    currentVersions.some(
      (currentVersion) => currentVersion !== currentVersions[0],
    )
  ) {
    throw new Error(
      `更新前のBigstarバージョンが一致していません: ${currentVersions.join(', ')}`,
    );
  }

  packageJson.version = version;
  tauriConfig.version = version;
  const nextCargoManifest = replaceVersion(
    cargoManifest,
    /^(\[package\][\s\S]*?^version = ")[^"]+(")/m,
    version,
    'Cargo.toml',
  );
  const nextCargoLock = replaceVersion(
    cargoLock,
    /^(\[\[package\]\]\r?\nname = "bigstar"\r?\nversion = ")[^"]+(")/m,
    version,
    'Cargo.lock',
  );

  writeFileSync(packagePath, `${JSON.stringify(packageJson, null, 2)}\n`);
  writeFileSync(
    tauriConfigPath,
    `${JSON.stringify(tauriConfig, null, 2)}\n`,
  );
  writeFileSync(cargoManifestPath, nextCargoManifest);
  writeFileSync(cargoLockPath, nextCargoLock);
}

const scriptPath = process.argv[1] ? resolve(process.argv[1]) : '';
if (scriptPath === fileURLToPath(import.meta.url)) {
  const version = process.argv[2];
  if (!version) {
    throw new Error('Usage: node scripts/set-version.mjs <version>');
  }
  const bigstarRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
  setBigstarVersion(bigstarRoot, version);
  console.log(`Bigstar version updated to ${version}`);
}
