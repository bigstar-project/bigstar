"""Replay continuous native captures against the offline Python policy.

Recorded inputs are authoritative: this checks the same observed history, not
an imagined game after either controller chooses a different action.
"""
import argparse
import gzip
import importlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))


def equal(expected, actual, context):
    if expected == actual:
        return
    if isinstance(expected, dict) and isinstance(actual, dict):
        for key in expected.keys() | actual.keys():
            if expected.get(key) != actual.get(key):
                equal(expected.get(key), actual.get(key), f'{context}/{key}')
    raise AssertionError(f'{context}: Python={expected!r}, Rust={actual!r}')


def replay(binary, paths, profiles, players):
    report = []
    for profile in profiles:
        module = ('nsmb_mvl_rule_match_v2' if profile == 'development' else
                  f'nsmb_mvl_rule_versions.{profile}_20260920')
        cls = importlib.import_module(module).RoutedHumanRule
        for player in players:
            period = 48 if profile == 'beginner' else 60
            p = subprocess.Popen([str(binary), '--research', '--profile', profile,
                                  '--player', str(player), '--period', str(period)],
                                 stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                 encoding='utf8', text=True, bufsize=1)
            assert p.stdout.readline().strip() == 'READY'
            def call(request):
                p.stdin.write(json.dumps(request, separators=(',', ':'))+'\n')
                p.stdin.flush()
                line = p.stdout.readline()
                if not line:
                    raise RuntimeError(f'worker stopped: {p.poll()}')
                return json.loads(line)
            try:
                for path in paths:
                    controller = cls(player, period)
                    call(dict(op='reset'))
                    times, python_times, triggers = [], [], {}
                    count = 0
                    generation, origin, last, next_frame = None, 0, -1, 0
                    with gzip.open(path, 'rt', encoding='utf8') as f:
                        for row in map(json.loads, f):
                            d = row['decision']
                            now = d['time']['rawFrame']
                            gen = d['time'].get('generation')
                            if gen != generation or now <= last:
                                controller.reset()
                                call(dict(op='reset'))
                                generation, origin, next_frame = gen, now, now
                            last = now
                            if now < next_frame:
                                continue
                            obs = d['observation']
                            if obs['stage']['group'] != 9 or obs['stage']['id'] != 0 or not obs['players'][player]['found']:
                                continue
                            next_frame = now + 6
                            previous = row.get('previousHeld', 0)
                            started = time.perf_counter_ns()
                            held = controller.act(d, now-origin, previous)
                            python_times.append((time.perf_counter_ns()-started)/1e6)
                            result = call(dict(op='act', decision=d, frame=now-origin, previousHeld=previous))
                            context = f'{path.name}/{profile}/{player}/{now-origin}'
                            equal(held, result['held'], context+'/held')
                            equal(json.loads(json.dumps(controller.trace)), result['trace'], context+'/trace')
                            times.append(result['decisionNanos']/1e6)
                            for key in controller.trace:
                                triggers[key] = triggers.get(key, 0)+1
                            count += 1
                    def stats(values):
                        ordered = sorted(values)
                        return dict(mean_ms=sum(values)/len(values), p95_ms=ordered[int(.95*(len(values)-1))], max_ms=max(values)) if values else {}
                    entry = dict(path=str(path), profile=profile, player=player, decisions=count,
                                 rust=stats(times), python=stats(python_times), trace_keys=triggers)
                    report.append(entry)
                    print(json.dumps(entry), flush=True)
            finally:
                p.stdin.close()
                p.stdout.close()
                if p.wait(timeout=10):
                    raise RuntimeError('worker failed')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('captures', type=Path, nargs='+')
    parser.add_argument('--binary', type=Path, default=ROOT/'target/release/bigstar-rule-cpu.exe')
    parser.add_argument('--profiles', nargs='+', default=['beginner', 'combat_v2', 'development'])
    parser.add_argument('--players', nargs='+', type=int, default=[0, 1])
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    report = replay(args.binary, args.captures, args.profiles, args.players)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
