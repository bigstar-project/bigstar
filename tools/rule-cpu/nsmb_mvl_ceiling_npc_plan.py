"""Brake after a ceiling bump when the predicted landing meets a ground NPC.

This is a narrowly scoped forecast, not a general collision-free controller.
Both known landing-turn branches must retain floor support and avoid actors.
"""
from nsmb_mvl_player_motion_forecast import forecast_player


def choose_ceiling_npc_brake(initial, proposed, previous, delay, occupied,
                             paths, horizon=36):
    if (initial['grounded'] or initial['vy'] > 0 or not paths
            or not 0 <= delay <= 6
            # The bumped box briefly disappears from the solid tile sample.
            # Adjacent blocks still describe this low-ceiling corridor.
            or not any(occupied(initial['x']+dx,
                                initial['depth']-initial['height']-4)
                       for dx in (-8, 0, 8))):
        return None

    def predict(held, counter):
        return forecast_player(**initial, previous_input=previous,
            inputs=[previous]*delay+[held]*(horizon-delay), occupied=occupied,
            counter_landing_turn=counter)[0]

    def danger(points):
        return any(i >= len(path) or
                   (abs((p['x']-path[i]['x']+512)%1024-512) < 16
                    and path[i]['depth']-20 < p['depth']
                    < path[i]['depth']+initial['height'])
                   for i, p in enumerate(points) for path in paths)

    nominal = predict(proposed, False)
    if not danger(nominal[:24]):
        return None
    candidates = []
    for held in (2064, 2080, 16, 32, 0):
        branches = [predict(held, counter) for counter in (False, True)]
        if any(len(points) != horizon or danger(points)
               or any(p['depth'] > 288 for p in points)
               or not all(p['grounded'] and occupied(p['x'], p['depth']+1)
                          for p in points[-6:]) for points in branches):
            continue
        candidates.append(dict(held=held, duration=(horizon-delay)//6*6,
                               points=branches[0], conservative_points=branches[1]))
    return min(candidates, key=lambda p: (not bool(p['held'] & 2048),
                (p['held'] ^ proposed).bit_count())) if candidates else None
