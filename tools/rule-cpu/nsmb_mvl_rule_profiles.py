"""The three fixed opponents exposed in Bigstar."""
import hashlib
import importlib
import json
from pathlib import Path

PROFILES = ('beginner', 'combat_v2', 'development')

def make_rule(profile, player, period=48):
    if profile in ('beginner', 'combat_v2'):
        version = profile + '_20260920'
        root = Path(__file__).with_name('nsmb_mvl_rule_versions') / version
        manifest = json.loads((root / 'manifest.json').read_text())
        for name, expected in manifest['files'].items():
            if hashlib.sha256((root / name).read_bytes()).hexdigest() != expected:
                raise ValueError('Preserved rule version changed: ' + name)
        cls = importlib.import_module('nsmb_mvl_rule_versions.' + version).RoutedHumanRule
    elif profile == 'development':
        from nsmb_mvl_native_interception import LIBRARY
        if LIBRARY is None:
            raise RuntimeError('CPU native acceleration is missing or does not match its sources. Rebuild the CPU worker.')
        from nsmb_mvl_rule_match_v2 import RoutedHumanRule
        cls = RoutedHumanRule
    else:
        raise ValueError('Unknown CPU profile: ' + profile)
    controller = cls(player, period=period)
    controller.profile = profile
    return controller
