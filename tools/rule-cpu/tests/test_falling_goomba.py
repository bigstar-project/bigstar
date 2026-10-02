"""Regression checks against recorded native falling-Goomba motion."""
import copy
import gzip
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import nsmb_mvl_rule_match_v2 as rule
fixture=json.loads(gzip.decompress((Path(__file__).parent/'fixtures/falling-goomba.json.gz').read_bytes()))

class FallingGoombaTest(unittest.TestCase):
    def setUp(self):
        self.decision=copy.deepcopy(fixture['initial']['decision'])

    def choose(self,decision):
        controller=rule.RoutedHumanRule(0,60)
        controller.trace={}
        # Isolate the new guard while retaining real world/terrain observations.
        with patch.object(rule._FallingGoombaBase,'act',return_value=2064):
            controller.act(decision,fixture['plan']['start'],2064)
        return controller.falling_goomba_plan

    def test_forecasts_match_native_before_replanning(self):
        plan=self.choose(self.decision)
        self.assertIsNotNone(plan)
        self.assertEqual(plan['duration'],6)
        self.assertGreater(plan['clearance'],0)
        following=fixture['following']
        for expected,actual in zip(plan['track'],following):
            self.assertEqual(rule.wrap(expected['x'],actual['enemy']['x']/4096),0)
            self.assertEqual(expected['depth'],-actual['enemy']['y']/4096)
        # 1470 sees a new projectile and releases the plan; movement changes at
        # 1473, after the native input delay. Do not test the abandoned future.
        for expected,actual in zip(plan['points'][:32],following[:32]):
            self.assertEqual(rule.wrap(expected['x'],actual['player']['x']/4096),0)
            self.assertEqual(expected['depth'],-actual['player']['y']/4096)

    def test_does_not_override_close_combat_or_airborne_movement(self):
        other=self.decision['observation']['players'][1]
        other['pos']=dict(self.decision['observation']['players'][0]['pos'])
        self.assertIsNone(self.choose(self.decision))
        d=copy.deepcopy(fixture['initial']['decision'])
        d['observation']['players'][0]['contact']['tileGround']=False
        self.assertIsNone(self.choose(d))

    def test_missing_timing_unknown_enemy_and_extra_hazard_are_rejected(self):
        d=copy.deepcopy(self.decision);d.pop('time')
        self.assertIsNone(self.choose(d))
        d=copy.deepcopy(self.decision)
        e=next(e for e in d['observation']['entities'] if e.get('actorGuid')==fixture['plan']['guid'])
        e['goombaBehaviorFunctionRaw']=0
        self.assertIsNone(self.choose(d))
        d=copy.deepcopy(self.decision)
        e=copy.deepcopy(next(e for e in d['observation']['entities'] if e.get('actorGuid')==fixture['plan']['guid']))
        e['category']='player_fireball';d['observation']['entities'].append(e)
        self.assertIsNone(self.choose(d))

    def test_guarded_version_rejects_missing_player_or_other_stage(self):
        for key in ('found',):
            d=copy.deepcopy(self.decision);d['observation']['players'][0][key]=False
            self.assertIsNone(self.choose(d))
        d=copy.deepcopy(self.decision);d['runtimePlayers'][0]['found']=False
        self.assertIsNone(self.choose(d))
        d=copy.deepcopy(self.decision);d['observation']['stage']['id']=1
        self.assertIsNone(self.choose(d))

    def test_high_speed_skid_is_outside_ordinary_turn_forecast(self):
        for speed in (2.25, 2.5, 2.75):
            d=copy.deepcopy(self.decision)
            d['observation']['players'][0]['vel']['x']=int(speed*4096)
            self.assertIsNone(self.choose(d))

if __name__=='__main__':unittest.main()
