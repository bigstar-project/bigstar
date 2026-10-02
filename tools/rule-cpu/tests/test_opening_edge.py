"""Native normal start, including held-jump landing and first equipment."""
import copy
import gzip
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import nsmb_mvl_rule_match_v2 as rule

ROWS=json.loads(gzip.decompress((Path(__file__).parent/'fixtures/opening-edge-watch.json.gz').read_bytes()))


class OpeningEdgeTest(unittest.TestCase):
    def test_native_opening_keeps_dash_and_climbs_before_first_equipment(self):
        # This native fixture predates the separate near-box strike change.
        controller=rule._SpacingBoxBase(1,60)
        for row in ROWS:
            held=controller.act(row['decision'],row['frame'],row['previousHeld'])
            self.assertEqual(held,row['held'],msg=f"frame {row['frame']}")
            if row['frame']==216:
                self.assertEqual(row['previousHeld'],2066)
                self.assertEqual(controller.trace['emergence_edge_route']['side'],1)
                self.assertFalse(controller.first_equipment_seen)
            if row['frame']>=294:
                self.assertTrue(controller.first_equipment_seen)

    def test_after_equipment_small_form_does_not_reenter_new_planner(self):
        controller=rule._SpacingBoxBase(1,60)
        for row in ROWS:
            if row['frame']>294:break
            controller.act(row['decision'],row['frame'],row['previousHeld'])
        self.assertTrue(controller.first_equipment_seen)
        # Replay a supported small-form item state after that history. This is
        # a control-scope check, not a claimed native respawn trajectory.
        row=next(r for r in ROWS if r['frame']==216)
        controller.emergence_plan=None
        with patch.object(rule,'plan_edge_interception',side_effect=AssertionError('new planner after equipment')):
            controller.act(copy.deepcopy(row['decision']),400,row['previousHeld'])
        self.assertTrue(controller.first_equipment_seen)
        controller.reset()
        self.assertFalse(controller.first_equipment_seen)


if __name__=='__main__':unittest.main()
