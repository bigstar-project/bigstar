"""Bounded ordinary-air landing alternative, requiring a verified support.

An interrupted nominal forecast is uncertainty, not proof of inevitable death.
The caller must exclude nearby dynamic hazards and monitor the selected path.
"""
from nsmb_mvl_player_motion_forecast import forecast_player


def choose_air_landing(initial, proposed, previous, delay, occupied, horizon=48):
    if (initial['grounded'] or initial['vy'] >= 0 or initial['depth'] > 272
            or delay not in range(7)):
        return None

    def simulate(action, counter_landing_turn=False):
        return forecast_player(initial['x'], initial['depth'], initial['vx'],
            initial['vy'], False, previous,
            [previous]*delay+[action]*(horizon-delay), occupied,
            height=initial['height'], facing=initial['facing'],
            counter_landing_turn=counter_landing_turn)

    nominal, nominal_end = simulate(proposed)
    if any(p['grounded'] for p in nominal):
        return None
    options = []
    for action in (0, 16, 32, 2064, 2080):
        points, end = simulate(action)
        if (end != 'horizon' or len(points) != horizon
                or any(p['depth'] > 288 for p in points)
                or not all(p['grounded'] for p in points[-6:])):
            continue
        conservative, conservative_end = simulate(action, True)
        if (conservative_end != 'horizon' or len(conservative) != horizon
                or any(p['depth'] > 288 for p in conservative)
                or not all(p['grounded'] for p in conservative[-6:])):
            continue
        arrival = next(i+1 for i, p in enumerate(points) if p['grounded'])
        stable = 0
        settle = None
        for i, point in enumerate(points):
            supported = all(p['grounded'] and all(
                occupied(p['x']+offset, p['depth']+1)
                for offset in (-8, 0, 8)) for p in (point, conservative[i]))
            stable = stable+1 if supported else 0
            if stable >= 6:
                settle = i+1
                break
        if settle is None:
            continue
        options.append(dict(held=action, arrival=arrival, settle=settle, points=points,
                            conservative_points=conservative,
                            nominal_end=nominal_end))
    return min(options, key=lambda p:(p['arrival'],
               (p['held'] ^ proposed).bit_count(), p['held'])) if options else None
