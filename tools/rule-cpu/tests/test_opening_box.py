"""Native initial equipment and no-op planning must preserve base decisions."""
import gzip
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import nsmb_mvl_rule_match_v2 as rule

HERE = Path(__file__).parent/'fixtures'


class OpeningBoxTest(unittest.TestCase):
    def test_native_center_strike_and_mushroom_collection(self):
        rows = json.loads(gzip.decompress((HERE/'opening-box-watch.json.gz').read_bytes()))
        controller = rule.RoutedHumanRule(1, 60)
        for row in rows:
            held = controller.act(row['decision'], row['frame'], row['previousHeld'])
            self.assertEqual(held, row['held'], msg=f"frame {row['frame']}")
            if row['frame'] == 180:
                plan = controller.spacing_box_plan
                self.assertIsNotNone(plan)
                self.assertEqual(plan['box_x'], 200)
                self.assertEqual(plan['hit'], 11)
            if row['frame'] == 288:
                self.assertEqual(row['decision']['runtimePlayers'][1]['currentPowerupRaw'], 1)
                self.assertTrue(controller.first_equipment_seen)
                self.assertEqual(held, 2064)  # Keep dash; postpone the ceiling jump.
                self.assertIn('departure_brake', controller.trace)
            if row['frame'] == 426:
                self.assertEqual(row['decision']['observation']['players'][1]['battleStars'], 1)

    def test_declined_plan_does_not_change_base_route_map(self):
        rows = json.loads(gzip.decompress((HERE/'box-declined-native.json.gz').read_bytes()))
        baseline, candidate = rule._SpacingBoxBase(1, 60), rule.RoutedHumanRule(1, 60)
        previous = 0
        for row in rows:
            expected = baseline.act(row['decision'], row['episodeFrame'], previous)
            actual = candidate.act(row['decision'], row['episodeFrame'], previous)
            self.assertEqual(actual, expected)
            self.assertEqual(candidate.navigator.tiles, baseline.navigator.tiles)
            previous = expected
        self.assertIsNot(candidate.spacing_box_nav, candidate.navigator)


if __name__ == '__main__':
    unittest.main()
