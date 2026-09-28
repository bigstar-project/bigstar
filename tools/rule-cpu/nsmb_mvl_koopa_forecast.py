"""Ground-only Koopa forecasts conditioned on observed native behavior state.

Walking turns, shell friction after a stomp, falling, kicking and player
contacts are outside this model. Stop rather than extrapolate those states.
"""


def forecast_ground_koopa(entity, occupied, frames=96):
    if entity.get('objectId') != 94:
        return [], 'wrong_actor'
    state, mode = entity.get('koopaBehaviorFunctionRaw'), entity.get('koopaShellModeRaw')
    x, depth = entity['pos']['x']/4096, -entity['pos']['y']/4096
    vx = entity['vel']['x']/4096
    shell = state == 0x020DF9F8 and mode == 1 and 2 <= abs(vx) <= 3
    walking = state == 0x020DFC58 and mode == 0 and abs(vx) == .5
    if not (shell or walking) or not entity.get('koopaCollisionRaw', 0) & 0x100:
        return [], 'unsupported_state'
    points = []
    for _ in range(frames):
        next_x = x+vx
        if not occupied(next_x, depth+1):
            return points, 'support_edge'
        direction = 1 if vx > 0 else -1
        # ROM default side sensor: vertical interval 6..9, horizontal reach 6.
        wall = any(occupied(next_x+direction*6, depth-h) for h in (6, 9))
        if wall and not shell:
            return points, 'walking_turn'
        x = next_x
        if wall:
            vx = -vx
        points.append(dict(x=x%1024, depth=depth, vx=vx))
    return points, 'horizon'
