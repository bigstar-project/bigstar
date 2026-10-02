"""Native supported departure and mandatory hazard rejection."""
import copy
import gzip
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import nsmb_mvl_rule_match_v2 as rule

SAVED = json.loads(gzip.decompress((Path(__file__).parent/'fixtures/departure-native.json.gz').read_bytes()))


class DepartureTest(unittest.TestCase):
    def choose(self, decision, target='natural_star'):
        return rule.departure_brake(decision, 1, SAVED['start']['previousHeld'], 2066,
                                    {'target': target}, rule.GrassNavigator())

    def test_native_ground_path_keeps_dash(self):
        plan = self.choose(SAVED['start']['decision'])
        self.assertIsNotNone(plan)
        self.assertEqual(plan['held'], 2064)
        self.assertEqual(len(plan['points']), 14)
        for point, actual in zip(plan['points'], SAVED['observed']):
            self.assertEqual((point['x'],point['depth']), (actual['x'],actual['depth']))

    def test_combat_and_unmodeled_nearby_hazards_do_not_defer_jump(self):
        for case in ('opponent', 'shell', 'unknown_goomba', 'combat', 'airborne', 'time', 'stage'):
            decision = copy.deepcopy(SAVED['start']['decision'])
            obs = decision['observation']
            target = 'natural_star'
            if case == 'opponent':
                obs['players'][0]['pos'] = dict(obs['players'][1]['pos'])
            elif case in ('shell', 'unknown_goomba'):
                enemy = next(e for e in obs['entities'] if e['category']=='enemy_goomba')
                enemy['pos'] = dict(obs['players'][1]['pos'])
                enemy['goombaBehaviorFunctionRaw'] = 0
                if case == 'shell':
                    enemy['category'] = 'enemy_koopa'
            elif case == 'combat':
                target = 'carrier'
            elif case == 'airborne':
                obs['players'][1]['contact']['tileGround'] = False
            elif case == 'time':
                decision.pop('time')
            else:
                obs['stage']['id'] = 1
            self.assertIsNone(self.choose(decision, target), case)


if __name__ == '__main__':
    unittest.main()
