"""Standalone local CPU worker. Standard input/output is a frame-synchronous protocol."""
import argparse
import json
import os
import sys
from nsmb_mvl_rule_profiles import PROFILES, make_rule


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    if args.check:
        for profile in PROFILES:
            make_rule(profile, 1, period=48 if profile == 'beginner' else 60)
        print('READY', flush=True)
        return
    profile = os.environ.get('MELONDS_NSML_RULE_PROFILE', 'beginner')
    controller = make_rule(profile, 1, period=48 if profile == 'beginner' else 60)
    generation = None
    origin = 0
    last = -1
    next_decision = 0
    held = 0
    print('READY', flush=True)
    for line in sys.stdin:
        request = json.loads(line)
        decision = request['decision']
        time = decision['time']
        frame = time['rawFrame']
        if time['rollbackEnabled']:
            raise ValueError('The CPU worker does not support rollback')
        if generation != time['generation'] or frame <= last:
            controller.reset()
            generation = time['generation']
            origin = frame
            next_decision = frame
            held = 0
        last = frame
        obs = decision['observation']
        if obs['stage']['group'] != 9 or obs['stage']['id'] != 0 or not obs['players'][1]['found']:
            held = 0
            next_decision = frame
        elif frame >= next_decision:
            held = controller.act(decision, frame-origin, int(request['previousHeld']))
            next_decision = frame + 6
        print(f'{frame} {held}', flush=True)

if __name__ == '__main__':
    main()
