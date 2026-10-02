"""Run the existing native match/trajectory harness with Rust controllers.

The legacy harness requires adjacent reference files for every external source.
Those files are copied unchanged for its provenance checks, not executed by the
Rust adapter. The runtime executable is pinned and hashed in the run manifest.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--harness-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exe', type=Path, required=True, help='Diagnostic melonDS runtime')
    parser.add_argument('--cpu', type=Path, default=ROOT/'build/rule-cpu/bigstar-rule-cpu.exe')
    parser.add_argument('--frames', type=int, default=1200)
    parser.add_argument('--maps', type=int, nargs='+', default=[45])
    parser.add_argument('--port', type=int, default=39840)
    args = parser.parse_args()
    harness, output, cpu, exe = (p.resolve() for p in (args.harness_root, args.output, args.cpu, args.exe))
    probe = harness/'scripts/nsmb_mvl_rule_match_probe.py'
    dependencies = {
        'controller.py': harness/'scripts/nsmb_mvl_rule_match.py',
        'nsmb_mvl_grass_navigation.py': harness/'scripts/nsmb_mvl_grass_navigation.py',
        'grass-navigation-tiles.json': harness/'docs/analysis/nsmb-mvl-rule-match-20260920/grass-navigation-tiles.json',
    }
    for path in [probe, cpu, exe, *dependencies.values()]:
        if not path.is_file(): parser.error(f'Missing required file: {path}')
    staging = output.with_name(output.name+'-runtime')
    if output.exists() or staging.exists(): parser.error('Run output and runtime staging must be new')
    staging.mkdir(parents=True)
    adapter = staging/'rust_controller.py'
    shutil.copy2(Path(__file__).with_name('nsmb_mvl_rust_rule.py'), adapter)
    # Freeze the binary too: a later rebuild must not change either player's
    # policy halfway through a multiple-map run.
    frozen_cpu = staging/'bigstar-rule-cpu.exe'
    shutil.copy2(cpu, frozen_cpu)
    for name, path in dependencies.items(): shutil.copy2(path, staging/name)
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    manifest = dict(cpu_source=str(cpu), cpu_sha256=digest(cpu), emulator_sha256=digest(exe),
                    adapter_sha256=digest(adapter), harness=str(probe), harness_sha256=digest(probe),
                    compatibility_files={k:digest(v) for k,v in dependencies.items()},
                    profiles=['development','development'], periods=[60,60], frames=args.frames, maps=args.maps)
    manifest_path = staging/'runtime.json'
    manifest_path.write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf8')
    environment = os.environ.copy()
    environment['NSMB_RULE_CPU_EXE'] = str(frozen_cpu)
    command = [sys.executable, '-X', 'utf8', str(probe), '--output', str(output),
               '--exe', str(exe), '--frames', str(args.frames), '--maps', *map(str,args.maps),
               '--port', str(args.port), '--fine-observations', '--star-target', '10',
               '--rule-period', '60', '--opponent-period', '60',
               '--rule-source', str(adapter), '--opponent-source', str(adapter)]
    result = subprocess.run(command, cwd=ROOT, env=environment, check=False)
    manifest['exit_code'] = result.returncode
    manifest_path.write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf8')
    if output.is_dir(): shutil.copy2(manifest_path, output/'rust-runtime.json')
    raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
