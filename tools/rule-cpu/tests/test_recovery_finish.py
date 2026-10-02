import copy
import gzip
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from nsmb_mvl_rule_match_v2 import GrassNavigator, forecast_player
from nsmb_mvl_recovery_finish import landing_plan, plan_valid


class RecoveryFinishTest(unittest.TestCase):
    def setUp(self):
        self.fixture = json.loads(gzip.decompress(
            (Path(__file__).parent/'fixtures/recovery-reversal.json.gz').read_bytes()))
        self.row = self.fixture['row']
        self.decision = copy.deepcopy(self.row['decision'])

    def plan(self, held=None):
        return landing_plan(self.decision, 1, self.row['held'] if held is None else held,
                            self.row['previousHeld'], GrassNavigator(), forecast_player)

    def test_recorded_reversal_has_a_reachable_landing(self):
        plan = self.plan()
        self.assertIsNotNone(plan)
        self.assertEqual(plan['held'], 2064)
        self.assertLessEqual(plan['landing_after'], 24)
        self.assertTrue(plan['points'][plan['landing_after']-1]['grounded'])

    def test_forecast_matches_eighteen_real_frames_before_the_wall(self):
        p = self.decision['observation']['players'][1]
        raw = self.decision['runtimePlayers'][1]
        nav = GrassNavigator()
        nav.update(p)
        delay = self.decision['time']['inputDelay']
        points, _ = forecast_player(x=p['pos']['x']/4096, depth=-p['pos']['y']/4096,
            vx=p['vel']['x']/4096, vy=p['vel']['y']/4096, grounded=False,
            previous_input=self.row['previousHeld'], occupied=nav.occupied, height=27,
            facing=raw['facing'], inputs=[self.row['previousHeld']]*delay+
            [self.row['held']]*(18-delay))
        self.assertEqual(len(points), 18)
        for point, actual in zip(points, self.fixture['following']):
            self.assertEqual(point['x'], actual['x']/4096)
            self.assertEqual(point['depth'], -actual['y']/4096)

    def test_existing_forward_and_jump_are_preserved(self):
        for held in (2064, 2066, 2082):
            with self.subTest(held=held):
                self.assertIsNone(self.plan(held))

    def test_incomplete_or_special_state_does_not_intervene(self):
        original = copy.deepcopy(self.decision)
        edits = [lambda d:d.pop('time'),
                 lambda d:d['observation']['players'][1].pop('contact'),
                 lambda d:d['observation']['players'][1]['contact'].update(tileGround=1),
                 lambda d:d['runtimePlayers'][1].update(damageStateRaw=1),
                 lambda d:d['runtimePlayers'][1].update(currentPowerupRaw=4),
                 lambda d:d['runtimePlayers'][1].update(physicsFlagRaw=130)]
        for index, edit in enumerate(edits):
            with self.subTest(index=index):
                self.decision = copy.deepcopy(original)
                edit(self.decision)
                self.assertIsNone(self.plan())

    def test_plan_stops_on_real_deviation_or_landing(self):
        plan = dict(self.plan(), start=4593, form=2)
        p = self.decision['observation']['players'][1]
        expected = plan['points'][5]
        p['pos'] = dict(x=round(expected['x']*4096), y=round(-expected['depth']*4096))
        self.assertTrue(plan_valid(plan, self.decision, 1, 4599))
        p['pos']['x'] += 4096
        self.assertFalse(plan_valid(plan, self.decision, 1, 4599))
        p['pos']['x'] -= 4096
        p['contact']['tileGround'] = 1
        self.assertFalse(plan_valid(plan, self.decision, 1, 4599))


if __name__ == '__main__':
    unittest.main()
