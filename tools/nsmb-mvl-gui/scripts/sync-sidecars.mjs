import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import {
  copyFileSync,
  existsSync,
  mkdirSync,
  readFileSync,
  statSync,
} from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const scriptDir = dirname(fileURLToPath(import.meta.url));
const guiDir = resolve(scriptDir, "..");
const repoRoot = resolve(guiDir, "..", "..");

const bridgeSource = resolve(
  repoRoot,
  "tools",
  "nsmb-net-bridge",
  "target",
  "release",
  "nsmb-net-bridge.exe",
);
const bridgeTargets = [
  resolve(guiDir, "src-tauri", "target", "release", "nsmb-net-bridge.exe"),
  resolve(
    guiDir,
    "src-tauri",
    "binaries",
    "nsmb-net-bridge-x86_64-pc-windows-msvc.exe",
  ),
];
const melonDsSource = resolve(
  repoRoot,
  "build",
  "release-windows-x86_64",
  "melonDS.exe",
);
const melonDsTargets = [
  resolve(guiDir, "src-tauri", "target", "release", "melonDS.exe"),
  resolve(guiDir, "src-tauri", "binaries", "melonDS-x86_64-pc-windows-msvc.exe"),
];

function sha256(path) {
  return createHash("sha256").update(readFileSync(path)).digest("hex");
}

function copySidecar(name, source, targets) {
  if (!existsSync(source)) {
    throw new Error(`${name} sidecar source not found: ${source}`);
  }
  const sourceHash = sha256(source);
  const sourceSize = statSync(source).size;

  for (const target of targets) {
    mkdirSync(dirname(target), { recursive: true });
    copyFileSync(source, target);
    const targetHash = sha256(target);
    if (targetHash !== sourceHash) {
      throw new Error(`sidecar copy verification failed: ${target}`);
    }
    console.log(
      `synced ${name} ${target} (${sourceSize} bytes, sha256 ${targetHash})`,
    );
  }
}

if (process.env.CI === "true" && existsSync(bridgeSource)) {
  console.log(`using CI-provided nsmb-net-bridge sidecar: ${bridgeSource}`);
} else {
  console.log("building nsmb-net-bridge release sidecar...");
  execFileSync(
    "cargo",
    [
      "build",
      "--release",
      "--features",
      "webrtc",
      "--manifest-path",
      resolve(repoRoot, "tools", "nsmb-net-bridge", "Cargo.toml"),
    ],
    {
      cwd: repoRoot,
      stdio: "inherit",
    },
  );
}

copySidecar("nsmb-net-bridge", bridgeSource, bridgeTargets);
copySidecar("melonDS", melonDsSource, melonDsTargets);
