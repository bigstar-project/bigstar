"""Replay recorded observations; divergence is diagnostic, not a new match."""
import argparse
import gzip
import hashlib
import importlib.util
import json
import sys
import time
from pathlib import Path


def audit(source, recording):
    spec = importlib.util.spec_from_file_location('audited_rule', source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    controller = module.RoutedHumanRule(1, 60)
    generation, last, next_decision, held = None, -1, 0, 0
    differences, interventions, timings = [], [], []
    count = 0
    with gzip.open(recording, 'rt', encoding='utf8') as stream:
        for row in map(json.loads, stream):
            count += 1
            decision = row['decision']
            clock = decision['time']
            frame = clock['rawFrame']
            if generation != clock['generation'] or frame <= last:
                controller.reset()
                generation, origin, next_decision, held = clock['generation'], frame, frame, 0
            last = frame
            obs = decision['observation']
            if obs['stage']['group'] != 9 or obs['stage']['id'] != 0 or not obs['players'][1]['found']:
                held, next_decision = 0, frame
            elif frame >= next_decision:
                start = time.perf_counter()
                held = controller.act(decision, frame-origin, int(row['previousHeld']))
                timings.append((time.perf_counter()-start)*1000)
                next_decision = frame+6
                detail = {key: controller.trace[key] for key in
                          ('stomp_upper_support', 'descent_goomba_alignment', 'descent_goomba_end',
                           'recovery_finish', 'recovery_finish_end')
                          if controller.trace.get(key)}
                if detail:
                    interventions.append(dict(frame=frame, generation=generation, detail=detail))
            if held != row['held']:
                differences.append(dict(frame=frame, generation=generation,
                                        expected=row['held'], actual=held))
    values = sorted(timings)
    if not values:
        raise ValueError('No decisions found')
    dependency_paths = {Path(m.__file__).resolve() for name, m in sys.modules.items()
                        if name.startswith('nsmb_mvl_') and getattr(m, '__file__', None)
                        and Path(m.__file__).suffix == '.py'}
    dependencies = {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(dependency_paths)}
    return dict(source=str(source), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                dependencies_sha256=dependencies,
                recording=str(recording), recording_sha256=hashlib.sha256(recording.read_bytes()).hexdigest(),
                frames=count, decisions=len(values), differences=len(differences),
                first_differences=differences[:20], interventions=interventions,
                latency_ms=dict(p50=values[int((len(values)-1)*.5)],
                                p99=values[int((len(values)-1)*.99)], maximum=max(values)),
                limitation='固定観測の操作監査。最初の操作差以降は候補の実試合結果ではない。判断時間は通信・描画を含まない。')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--recording', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = audit(args.source, args.recording)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    print(json.dumps({k:v for k,v in report.items() if k not in
                     ('interventions', 'first_differences', 'dependencies_sha256')}, ensure_ascii=False))
