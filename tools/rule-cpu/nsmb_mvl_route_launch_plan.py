"""Choose a measured-physics takeoff delay when a routed jump would fall short.

This is a bounded ordinary-jump model, not a solver for moving opponents,
damage, wall kicks, or changing geometry. Callers must monitor execution.
"""
from nsmb_mvl_player_motion_forecast import forecast_player


def choose_launch_delay(initial, previous, delay, goal, occupied, enemy_paths=(), horizon=96):
    dx = (goal['x']-initial['x']+512)%1024-512
    if (not initial['grounded'] or not 24 <= abs(dx) <= 144
            or abs(goal['depth']-initial['depth']) > 64 or delay not in range(7)):
        return None
    direction = 16 if dx > 0 else 32
    candidates = []
    for wait in (0, 6, 12, 18, 24):
        for hold in (18, 24, 30, 36):
            inputs = ([previous]*delay + [direction | 2048]*wait
                      + [direction | 2050]*hold
                      + [direction | 2048]*max(0, horizon-delay-wait-hold))[:horizon]
            points, _ = forecast_player(initial['x'], initial['depth'], initial['vx'],
                initial['vy'], True, previous, inputs, occupied,
                height=initial['height'], facing=initial['facing'])
            airborne = False
            for index, point in enumerate(points):
                airborne = airborne or not point['grounded']
                if point['depth'] > max(initial['depth'], goal['depth'])+32:
                    break
                if any(index >= len(path) or
                       (abs((point['x']-path[index]['x']+512)%1024-512) < 18
                        and path[index]['depth']-14 < point['depth']
                        < path[index]['depth']+initial['height']) for path in enemy_paths):
                    break
                if (airborne and point['grounded'] and abs(point['depth']-goal['depth']) < 1
                        and abs((point['x']-goal['x']+512)%1024-512) < 24):
                    if wait == 0:
                        return None  # No demonstrated need to delay takeoff.
                    candidates.append(dict(wait=wait, hold=hold, arrival=index+1,
                        points=points[:index+1], input_sequence=inputs[delay:index+1],
                        final_x=point['x'], final_depth=point['depth']))
                    break
    return min(candidates, key=lambda p:(p['arrival'], abs((p['final_x']-goal['x']+512)%1024-512))) if candidates else None
