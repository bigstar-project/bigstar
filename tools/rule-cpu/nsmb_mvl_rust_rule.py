"""Thin research adapter; all policy decisions are made by the native CPU.

Pass this file to the existing match probe's --rule-source/--opponent-source.
It deliberately does not import any Python policy implementation.
"""
import atexit
import hashlib
import json
import os
from pathlib import Path
import subprocess


class RoutedHumanRule:
    def __init__(self, player, period=60, profile='development', executable=None):
        self.player, self.period, self.profile = player, period, profile
        root = Path(__file__).resolve().parents[2]
        self.executable = Path(executable or os.environ.get('NSMB_RULE_CPU_EXE',
                                    root/'build/rule-cpu/bigstar-rule-cpu.exe')).resolve()
        self.executable_sha256 = hashlib.sha256(self.executable.read_bytes()).hexdigest()
        creationflags = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
        environment = os.environ.copy()
        environment.pop('MELONDS_NSML_RULE_CAPTURE', None)
        self._process = subprocess.Popen([str(self.executable), '--research', '--player', str(player),
                                          '--profile', profile, '--period', str(period)],
                                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                         text=True, encoding='utf8', bufsize=1,
                                         creationflags=creationflags, env=environment)
        self.trace = {}
        self.decision_nanos = 0
        if self._process.stdout.readline().strip() != 'READY':
            self.close()
            raise RuntimeError('Native rule CPU did not become ready')
        atexit.register(self.close)

    def _request(self, request):
        process = self._process
        if process is None or process.poll() is not None:
            raise RuntimeError('Native rule CPU has exited')
        process.stdin.write(json.dumps(request, separators=(',', ':'))+'\n')
        process.stdin.flush()
        reply = process.stdout.readline()
        if not reply:
            raise RuntimeError(f'Native rule CPU closed its protocol (exit={process.poll()})')
        return json.loads(reply)

    def reset(self):
        self._request(dict(op='reset'))
        self.trace = {}

    def act(self, decision, frame, previous_held=0):
        reply = self._request(dict(op='act', decision=decision, frame=frame, previousHeld=previous_held))
        self.trace = reply['trace']
        self.decision_nanos = reply['decisionNanos']
        return reply['held']

    def close(self):
        process = getattr(self, '_process', None)
        if process is None:
            return
        self._process = None
        atexit.unregister(self.close)
        try:
            process.stdin.close()
        finally:
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
            process.stdout.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def make_rule(profile, player, period=60):
    return RoutedHumanRule(player, period, profile)
