import contextlib
import io
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from nsmb_mvl_rule_watch_worker import run


class Controller:
    def __init__(self):
        self.calls = []
        self.resets = 0
        self.trace = {}

    def reset(self):
        self.resets += 1

    def act(self, decision, frame, previous):
        self.calls.append((frame, previous))
        self.trace = {'decision_frame': frame}
        return 2064 if frame == 0 else 2080


class WorkerTest(unittest.TestCase):
    def requests(self):
        for generation, frame in [(1, f) for f in range(100, 113)]+[(2, 100), (2, 101)]:
            yield json.dumps(dict(previousHeld=32, decision=dict(
                time=dict(rawFrame=frame, generation=generation, rollbackEnabled=False),
                observation=dict(stage=dict(group=9, id=0), players=[{}, dict(found=True)]))))

    def test_recording_preserves_six_frame_inputs_and_generation_reset(self):
        outputs, controllers = [], []
        capture = io.StringIO()
        for sink in (None, capture):
            controller = Controller()
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                run(controller, self.requests(), sink)
            outputs.append(output.getvalue())
            controllers.append(controller)
        self.assertEqual(outputs[0], outputs[1])
        for controller in controllers:
            self.assertEqual(controller.calls, [(0, 32), (6, 32), (12, 32), (0, 32)])
            self.assertEqual(controller.resets, 2)
        rows = [json.loads(line) for line in capture.getvalue().splitlines()]
        self.assertEqual(len(rows), 15)
        self.assertEqual([row['held'] for row in rows], [2064]*6+[2080]*7+[2064]*2)
        self.assertEqual(rows[7]['trace'], {'decision_frame': 6})

    def test_rollback_is_rejected_before_any_action(self):
        request = json.loads(next(self.requests()))
        request['decision']['time']['rollbackEnabled'] = True
        controller = Controller()
        with self.assertRaises(ValueError):
            run(controller, [json.dumps(request)])
        self.assertEqual(controller.calls, [])


if __name__ == '__main__':
    unittest.main()
