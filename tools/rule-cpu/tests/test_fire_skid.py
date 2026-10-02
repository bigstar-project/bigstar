"""Check the limited Fire-form extension against actual native positions."""
import copy
import gzip
import importlib.util
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import nsmb_mvl_rule_match_v2 as rule
fixture=json.loads(gzip.decompress((Path(__file__).parent/'fixtures/fire-skid.json.gz').read_bytes()))

class FireSkidTest(unittest.TestCase):
    def choose(self, decision, held=2064, previous=2064):
        controller=rule._PredictiveRejumpBase(1,60)
        controller.trace={}
        with patch.object(rule._SkidJumpBase,'act',return_value=held):
            controller.act(decision,fixture['plan']['start'],previous)
        return controller.skid_jump_plan

    def test_landing_forecast_matches_native(self):
        plan=self.choose(fixture['initial']['decision'])
        self.assertIsNotNone(plan)
        self.assertEqual(plan['landing_after'],53)
        self.assertEqual(len(plan['points']),59)
        for expected,actual in zip(plan['points'],fixture['following']):
            p=actual['player']
            self.assertEqual(rule.wrap(p['pos']['x']/4096,expected['x']),0)
            self.assertEqual(-p['pos']['y']/4096,expected['depth'])

    def test_fire_requires_unchanged_held_run_button(self):
        d=fixture['initial']['decision']
        self.assertIsNone(self.choose(d,held=16))
        self.assertIsNone(self.choose(d,previous=16))

    def test_ordinary_running_and_incomplete_clock_do_not_trigger(self):
        d=copy.deepcopy(fixture['initial']['decision'])
        d['runtimePlayers'][1]['behaviorFuncRaw']=0x02115AAC
        self.assertIsNone(self.choose(d))
        d=copy.deepcopy(fixture['initial']['decision']);d.pop('time')
        self.assertIsNone(self.choose(d))

    def test_close_opponent_prevents_plan(self):
        d=copy.deepcopy(fixture['initial']['decision'])
        d['observation']['players'][0]['pos']=dict(d['observation']['players'][1]['pos'])
        self.assertIsNone(self.choose(d))

if __name__=='__main__':unittest.main()
