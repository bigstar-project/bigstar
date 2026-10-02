"""Ordered cross-language replay; Python is an offline oracle, never a runtime dependency."""
import argparse
import gzip
import importlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))


def compare(binary, profiles=None):
    process = subprocess.Popen([str(binary)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               text=True, encoding='utf8', bufsize=1)
    def call(request):
        process.stdin.write(json.dumps(request, separators=(',', ':')) + '\n')
        process.stdin.flush()
        line = process.stdout.readline()
        if not line:
            raise RuntimeError(f'Rust oracle exited: {process.poll()}')
        return json.loads(line)

    def equal(expected, actual, context):
        if expected != actual:
            if isinstance(expected, dict) and isinstance(actual, dict):
                for key in expected.keys() | actual.keys():
                    if expected.get(key) != actual.get(key):
                        equal(expected.get(key), actual.get(key), f'{context}/{key}')
            raise AssertionError(f'{context}: Python={expected!r}, Rust={actual!r}')

    count = 0
    forecasts = 0
    try:
        fixtures = sorted(p for p in (ROOT/'tests/fixtures').glob('*.json.gz')
                          if p.name != 'rust-layer-parity.json.gz')
        stages = {'development_base': ('_OriginalRule', 0),
                  'emergence': ('_LaunchBase', 1), 'launch': ('_AirBase', 2),
                  'air': ('_CeilingNpcBase', 3), 'ceiling_npc': ('_WallExitFireBase', 4),
                  'wall_exit_fire': ('_GroundAwayBase', 5), 'ground_away': ('_CeilingNpcBrakeBase', 6),
                  'persistent_brake': ('_DefeatedGoombaBase', 7), 'defeated_goomba': ('_GroundCooldownBase', 8),
                  'ground_cooldown': ('_ShaftContactBase', 9), 'shaft_contact': ('_ObservedCeilingKoopaBase', 10),
                  'observed_ceiling': ('_UnreachablePitBase', 11), 'pit_targets': ('_ImminentWallEntryBase', 12),
                  'imminent_wall': ('_MissedTakeoffBase', 13), 'missed_takeoff': ('_UpperNpcReleaseBase', 14),
                  'upper_release': ('_SkidJumpBase', 15), 'skid_jump': ('_PredictiveRejumpBase', 16),
                  'predictive_rejump': ('_RisingNpcReleaseBase', 17), 'rising_release': ('_ContestedMushroomBase', 18),
                  'contested_hop': ('_ForwardNpcBase', 19), 'forward_npc': ('_ForwardNpcInvincibilityBase', 20),
                  'forward_invincibility': ('_DescentKoopaShotBase', 21), 'descent_shot': ('_FallingMushroomBase', 22),
                  'falling_mushroom': ('_OverheadPitBase', 23), 'overhead_pit': ('_RecoveryFinishBase', 24),
                  'recovery_finish': ('_NpcLandingBase', 25), 'npc_landing': ('_WaitWallBase', 26),
                  'wait_wall': ('_FallingGoombaBase', 27), 'falling_goomba': ('_WallShortBase', 28),
                  'wall_short': ('_ShotRecoveryBase', 29), 'shot_recovery': ('_SpacingBoxBase', 30),
                  'spacing_box': ('_DepartureBase', 31), 'development': ('RoutedHumanRule', 32)}
        for profile in ('beginner', 'combat_v2', *stages):
            if profiles and profile not in profiles:
                continue
            combat = profile == 'combat_v2'
            if profile in stages:
                cls = getattr(importlib.import_module('nsmb_mvl_rule_match_v2'), stages[profile][0])
            else:
                cls = importlib.import_module(f'nsmb_mvl_rule_versions.{profile}_20260920').RoutedHumanRule
            for player in (0, 1):
                for path in fixtures:
                    rows = json.loads(gzip.decompress(path.read_bytes()))
                    if not isinstance(rows, list) or not rows or 'decision' not in rows[0]:
                        continue
                    controller = cls(player, 60)
                    call(dict(op='reset', player=player, period=60, combat=combat,
                              development_base=profile == 'development_base',
                              development_layer=stages.get(profile, ('', 0))[1]))
                    previous = 0
                    for index, row in enumerate(rows):
                        frame = row.get('frame', row.get('episodeFrame', index*6))
                        # Both implementations receive exactly the same ordered
                        # observations and preceding input, including resets.
                        held = controller.act(row['decision'], frame, previous)
                        actual = call(dict(op='act', decision=row['decision'], frame=frame,
                                           previousHeld=previous))
                        context = f'{profile}/{player}/{path.name}/{frame}'
                        equal(held, actual['held'], context+'/held')
                        equal(json.loads(json.dumps(controller.trace)), actual['trace'], context+'/trace')
                        previous = held
                        count += 1
        from nsmb_mvl_grass_navigation import GrassNavigator
        from nsmb_mvl_player_motion_forecast import forecast_player
        from nsmb_mvl_goomba_forecast import forecast_ground_goomba
        from nsmb_mvl_koopa_forecast import forecast_ground_koopa
        from nsmb_mvl_item_forecast import forecast_emerging
        from nsmb_mvl_air_landing_plan import choose_air_landing
        from nsmb_mvl_route_launch_plan import choose_launch_delay
        from nsmb_mvl_emerging_item_plan import plan_ground_interception
        from nsmb_mvl_emerging_ascent_plan import plan_ascent_interception
        from nsmb_mvl_rule_match_v2 import choose_landing_support
        for path in fixtures:
            data = json.loads(gzip.decompress(path.read_bytes()))
            def recorded(value):
                if isinstance(value, dict):
                    if 'decision' in value:
                        yield value
                    else:
                        for child in value.values():
                            yield from recorded(child)
                elif isinstance(value, list):
                    for child in value:
                        yield from recorded(child)
            for index, row in enumerate(recorded(data)):
                if index % 12:
                    continue
                decision = row['decision']
                for player in (0, 1):
                    me = decision['observation']['players'][player]
                    raw = decision['runtimePlayers'][player]
                    if not me['found'] or not raw['found']:
                        continue
                    nav = GrassNavigator(); nav.update(me)
                    initial = dict(x=me['pos']['x']/4096, depth=-me['pos']['y']/4096,
                                   vx=me['vel']['x']/4096, vy=me['vel']['y']/4096,
                                   grounded=bool(raw['collisionFlagRaw'] & 32769),
                                   facing=raw['facing'])
                    height = 16 if raw['currentPowerupRaw'] == 0 else 27
                    previous = row.get('previousHeld', 0)
                    proposed = row.get('held', 0)
                    context = f'{path.name}/{index}/{player}'
                    for held in (0, 16, 32, 2064, 2080, 2066, 2082):
                        for counter in (False, True):
                            inputs = [previous]*2 + [held]*94
                            expected = forecast_player(**initial, previous_input=previous,
                                inputs=inputs, occupied=nav.occupied, height=height,
                                counter_landing_turn=counter)
                            actual = call(dict(op='forecast', initial=initial, previous=previous,
                                inputs=inputs, player=me, height=height, counter_landing_turn=counter))
                            equal(json.loads(json.dumps(expected)), actual, context+f'/forecast/{held}/{counter}')
                            forecasts += 1
                    full = dict(initial, height=height)
                    goal = dict(x=(initial['x']+64)%1024, depth=initial['depth']-32)
                    cases = {
                        'air_landing': lambda: choose_air_landing(full, proposed, previous, 2, nav.occupied),
                        'landing_support': lambda: choose_landing_support(full, proposed, previous, 2, nav.occupied, forecast_player, []),
                        'launch_delay': lambda: choose_launch_delay(full, previous, 2, goal, nav.occupied),
                    }
                    for name, oracle in cases.items():
                        expected = oracle()
                        actual = call(dict(op='plan', name=name, initial=initial, height=height,
                            proposed=proposed, previous=previous, delay=2, player=me, goal=goal))
                        equal(json.loads(json.dumps(expected)), actual, context+'/'+name)
                        forecasts += 1
                    for entity in decision['observation']['entities']:
                        category = entity['category']
                        if category == 'enemy_goomba':
                            name = 'goomba'
                            expected = forecast_ground_goomba(entity['pos']['x']/4096,
                                -entity['pos']['y']/4096, entity['vel']['x']/4096, nav.occupied)
                        elif category == 'enemy_koopa':
                            name = 'koopa'; expected = forecast_ground_koopa(entity, nav.occupied)
                        elif entity.get('objectId') == 31:
                            name = 'emerging'; expected = forecast_emerging(entity, nav.occupied)
                        else:
                            continue
                        actual = call(dict(op='actor', name=name, player=me, entity=entity))
                        equal(json.loads(json.dumps(expected)), actual, context+'/'+name)
                        forecasts += 1
                        if name == 'emerging' and expected and initial['grounded']:
                            for name, oracle in [('ground_interception', plan_ground_interception),
                                                 ('ascent_interception', plan_ascent_interception)]:
                                expected_plan = oracle(initial, previous, 2, entity, nav.occupied)
                                actual = call(dict(op='plan', name=name, initial=initial, height=16,
                                    previous=previous, delay=2, player=me, entity=entity))
                                equal(json.loads(json.dumps(expected_plan)), actual, context+'/'+name)
                                forecasts += 1
        print(json.dumps(dict(ordered_actions_and_traces=count, forecasts_and_plans=forecasts, differences=0)))
    finally:
        process.stdin.close()
        process.stdout.close()
        if process.wait(timeout=10) != 0:
            raise RuntimeError('Rust comparison process failed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT/'target/release/examples/parity.exe')
    parser.add_argument('--profiles', nargs='+')
    args = parser.parse_args()
    compare(args.binary, args.profiles)
