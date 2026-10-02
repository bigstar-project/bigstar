"""Normal watch start includes an entrance animation before horizontal control."""
import copy
import gzip
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import nsmb_mvl_rule_match_v2 as rule

ROWS=json.loads(gzip.decompress((Path(__file__).parent/'fixtures/entrance-watch.json.gz').read_bytes()))


class EntranceTest(unittest.TestCase):
    def test_native_start_does_not_arm_a_backward_escape(self):
        controller=rule.RoutedHumanRule(1,60)
        for row in ROWS:
            held=controller.act(row['decision'],row['frame'],row['previousHeld'])
            self.assertEqual(held,row['held'],msg=f"frame {row['frame']}")
            self.assertEqual(controller.escape_until,-1)
            if row['decision']['runtimePlayers'][1]['physicsFlagRaw']&1:
                self.assertEqual(controller.stuck_frames,0)
        self.assertEqual(next(r for r in ROWS if r['frame']==96)['held'],2064)

    def test_entrance_clears_a_preexisting_escape(self):
        controller=rule.RoutedHumanRule(1,60)
        controller.act(ROWS[0]['decision'],0,0)
        controller.escape_until=200;controller.stuck_frames=96
        controller.act(ROWS[1]['decision'],6,2064)
        self.assertEqual(controller.stuck_frames,0)
        self.assertEqual(controller.escape_until,-1)

    def test_ordinary_blocked_motion_still_triggers_escape(self):
        d=copy.deepcopy(next(r['decision'] for r in ROWS if r['frame']==108))
        self.assertFalse(d['runtimePlayers'][1]['physicsFlagRaw']&1)
        controller=rule.RoutedHumanRule(1,60)
        for frame in range(0,103,6):controller.act(d,frame,2064)
        self.assertGreater(controller.escape_until,102)


if __name__=='__main__':unittest.main()
