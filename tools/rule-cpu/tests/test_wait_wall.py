import copy
import gzip
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from nsmb_mvl_grass_navigation import GrassNavigator
from nsmb_mvl_rule_match_v2 import _shaft_contact_forecast, _late_wall_contact_depth_bound
from nsmb_mvl_wait_wall import choose_plan, plan_valid, wrap


class WaitWallTest(unittest.TestCase):
    def setUp(self):
        self.fixture = json.loads(gzip.decompress((Path(__file__).parent/'fixtures/wait-wall.json.gz').read_bytes()))
        self.decision = copy.deepcopy(self.fixture['initial']['decision'])

    def plan(self, held=0, previous=2064):
        return choose_plan(self.decision, 1, held, previous, GrassNavigator(),
                           _shaft_contact_forecast, _late_wall_contact_depth_bound)

    def test_forecast_matches_native_before_wall_slide(self):
        plan = self.plan()
        self.assertEqual(plan['held'], 32)
        self.assertEqual(plan['bound']['peak_depth'], 322.3125)
        for actual, expected in zip(self.fixture['following'], plan['points']):
            me = actual['me']
            self.assertAlmostEqual(wrap(me['pos']['x']/4096, expected['x']), 0)
            self.assertAlmostEqual(-me['pos']['y']/4096, expected['depth'])

    def test_does_not_override_a_jump_or_nearby_opponent(self):
        self.assertIsNone(self.plan(held=2))
        self.assertIsNone(self.plan(previous=2066))
        self.decision['observation']['players'][0]['pos'] = dict(self.decision['observation']['players'][1]['pos'])
        self.assertIsNone(self.plan())

    def test_position_change_or_damage_cancels_plan(self):
        plan = dict(self.plan(), start=5904)
        self.decision['observation']['players'][1] = copy.deepcopy(self.fixture['following'][5]['me'])
        self.assertTrue(plan_valid(plan, self.decision, 1, 5910))
        self.decision['observation']['players'][1]['pos']['x'] += 4096
        self.assertFalse(plan_valid(plan, self.decision, 1, 5910))
        self.decision['observation']['players'][1] = copy.deepcopy(self.fixture['following'][5]['me'])
        self.decision['runtimePlayers'][1]['damageStateRaw'] = 1
        self.assertFalse(plan_valid(plan, self.decision, 1, 5910))

    def test_missing_delay_is_not_treated_as_valid(self):
        self.decision.pop('time')
        self.assertIsNone(self.plan())


if __name__ == '__main__':
    unittest.main()
