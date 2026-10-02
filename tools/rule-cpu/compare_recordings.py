"""Compare two native watch recordings without loading a whole match into RAM."""
import argparse
import gzip
import hashlib
import itertools
import json
from pathlib import Path


def compare(left, right, through_frame=None):
    first_input = first_state = None
    matched = count = 0
    outcomes = [dict(deaths=[], star_events=[], landings=[]),
                dict(deaths=[], star_events=[], landings=[])]
    previous = [None, None]
    with gzip.open(left, 'rt', encoding='utf8') as a, gzip.open(right, 'rt', encoding='utf8') as b:
        for la, lb in itertools.zip_longest(a, b):
            if la is None or lb is None:
                break
            rows = [json.loads(la), json.loads(lb)]
            clocks = [r['decision']['time'] for r in rows]
            frame = clocks[0]['rawFrame']
            if through_frame is not None and frame > through_frame:
                break
            if frame != clocks[1]['rawFrame'] or clocks[0]['generation'] != clocks[1]['generation']:
                raise ValueError('Different frame grids; compare round outcomes separately')
            count += 1
            same = all(rows[0]['decision'][k] == rows[1]['decision'][k]
                       for k in ('observation', 'runtimePlayers'))
            if not same and first_state is None:
                first_state = frame
            if rows[0]['held'] != rows[1]['held'] and first_input is None:
                first_input = dict(frame=frame, state_equal=same,
                                   held=[r['held'] for r in rows])
            if first_input is None and same:
                matched += 1
            for i, row in enumerate(rows):
                p = row['decision']['observation']['players'][1]
                state = dict(frame=frame, dead=bool(p['dead']), stars=p['battleStars'],
                             grounded=bool(p['contact']['tileGround']))
                before = previous[i]
                if before:
                    if state['dead'] and not before['dead']:
                        outcomes[i]['deaths'].append(frame)
                    if state['stars'] != before['stars']:
                        outcomes[i]['star_events'].append(dict(frame=frame,
                            before=before['stars'], after=state['stars']))
                    if state['grounded'] and not before['grounded'] and not state['dead']:
                        outcomes[i]['landings'].append(frame)
                previous[i] = state
    if through_frame is not None and (not previous[0] or previous[0]['frame'] != through_frame):
        raise ValueError('The requested common interval was not completely recorded')
    return dict(left=str(left), right=str(right),
                sha256=[hashlib.sha256(p.read_bytes()).hexdigest() for p in (left,right)],
                through_frame=through_frame, compared_frames=count, equal_prefix_frames=matched, first_input=first_input,
                first_state=first_state, outcomes=outcomes, final_states=previous,
                limitation='共通フレーム区間の実機比較。固定した相手入力であり、人間が候補へ適応した勝率ではない。')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('left', type=Path)
    parser.add_argument('right', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--through-frame', type=int, help='Compare a complete interval before different round transitions')
    args = parser.parse_args()
    result = compare(args.left, args.right, args.through_frame)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
    print(json.dumps({k:v for k,v in result.items() if k not in ('outcomes', 'sha256')}, ensure_ascii=False))
