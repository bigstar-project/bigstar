import copy
import gzip
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from nsmb_mvl_grass_navigation import GrassNavigator
from nsmb_mvl_koopa_forecast import forecast_ground_koopa
from nsmb_mvl_player_motion_forecast import forecast_player
from nsmb_mvl_npc_landing import choose_plan, wrap


class NpcLandingTest(unittest.TestCase):
    def setUp(self):
        self.fixture = json.loads(gzip.decompress((Path(__file__).parent/'fixtures/npc-landing.json.gz').read_bytes()))
        self.decision = copy.deepcopy(self.fixture['row']['decision'])
        self.me = self.decision['observation']['players'][0]
        self.nav = GrassNavigator()
        self.nav.update(self.me)

    def test_forecasts_match_native_motion_before_inputs_diverge(self):
        enemy = next(e for e in self.decision['observation']['entities'] if e['category'] == 'enemy_koopa')
        enemies, status = forecast_ground_koopa(enemy, self.nav.occupied, 21)
        me = self.me
        points, _ = forecast_player(x=me['pos']['x']/4096, depth=-me['pos']['y']/4096,
            vx=me['vel']['x']/4096, vy=me['vel']['y']/4096, grounded=False,
            previous_input=2064, inputs=[2064]*8, occupied=self.nav.occupied,
            height=16, facing=1)
        self.assertEqual(status, 'horizon')
        for actual, e in zip(self.fixture['following'], enemies):
            self.assertEqual(wrap(e['x'], actual['enemy']['x']/4096), 0)
            self.assertEqual(e['depth'], -actual['enemy']['y']/4096)
        for actual, p in zip(self.fixture['following'][:8], points):
            self.assertEqual(p['x'], actual['player']['x']/4096)
            self.assertEqual(p['depth'], -actual['player']['y']/4096)

    def test_braking_lands_on_supported_ground(self):
        plan = choose_plan(self.decision, 0, 2064, 2064, self.nav)
        self.assertIsNotNone(plan)
        self.assertEqual(plan['held'], 2080)
        landing = plan['points'][plan['landing']-1]
        self.assertEqual(landing['contact'], 'floor')
        for offset in (-5, 0, 5):
            self.assertTrue(self.nav.occupied(landing['x']+offset, landing['depth']+1))

    def test_earlier_brake_with_insufficient_ledge_support_is_rejected(self):
        edge = self.fixture['edge']['decision']
        self.assertIsNone(choose_plan(edge, 0, 2064, 2066, GrassNavigator()))

    def test_preserves_existing_brake_and_nearby_combat(self):
        self.assertIsNone(choose_plan(self.decision, 0, 2080, 2064, self.nav))
        self.decision['observation']['players'][1]['pos'] = dict(self.me['pos'])
        self.assertIsNone(choose_plan(self.decision, 0, 2064, 2064, self.nav))


if __name__ == '__main__':
    unittest.main()
