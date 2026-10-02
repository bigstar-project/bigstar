import copy
import gzip
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from nsmb_mvl_carry_brake import retain_during_brake


class CarryBrakeTest(unittest.TestCase):
    def setUp(self):
        self.fixture = json.loads(gzip.decompress((Path(__file__).parent/'fixtures/carry-brake.json.gz').read_bytes()))
        self.decision = copy.deepcopy(self.fixture['initial']['decision'])

    def test_native_braking_path_is_preserved_but_item_is_not_released(self):
        self.assertTrue(retain_during_brake(self.decision, 1, 34, 2066, True))
        for before, after in zip(self.fixture['before'], self.fixture['after']):
            self.assertEqual(before['me']['pos'], after['me']['pos'])
        self.assertTrue(self.fixture['before'][-1]['raw']['actionFlagRaw'] & 0x80)
        self.assertFalse(self.fixture['after'][-1]['raw']['actionFlagRaw'] & 0x80)
        self.assertTrue(self.fixture['after'][-1]['raw']['subActionFlagRaw'] & 1)

    def test_airborne_bit_alone_is_not_carrying(self):
        self.decision['runtimePlayers'][1]['subActionFlagRaw'] = 0x40
        self.assertFalse(retain_during_brake(self.decision, 1, 34, 2066, True))

    def test_preserves_shooting_and_existing_release(self):
        raw = self.decision['runtimePlayers'][1]
        raw['currentPowerupRaw'] = 2
        self.assertFalse(retain_during_brake(self.decision, 1, 34, 2066, True))
        raw['currentPowerupRaw'] = 0
        raw['actionFlagRaw'] |= 0x80
        self.assertFalse(retain_during_brake(self.decision, 1, 34, 2066, True))

    def test_does_not_force_run_for_ground_or_forward_precision(self):
        self.assertFalse(retain_during_brake(self.decision, 1, 18, 2066, True))
        self.assertFalse(retain_during_brake(self.decision, 1, 34, 2066, False))
        self.decision['observation']['players'][1]['contact']['tileGround'] = True
        self.assertFalse(retain_during_brake(self.decision, 1, 34, 2066, True))


if __name__ == '__main__':
    unittest.main()
