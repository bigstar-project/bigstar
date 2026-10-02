"""Reach a feasible shaft wall after an airborne objective ends.

The forecast covers ordinary falling and the delay until a wall-jump input.
It does not predict an opponent's future input or the eventual landing.
"""


def wrap(a, b):
    return (a-b+512)%1024-512


def eligible(decision, player):
    obs = decision['observation']
    me, raw = obs['players'][player], decision['runtimePlayers'][player]
    delay = decision.get('time', {}).get('inputDelay', -1)
    if (obs.get('stage', {}).get('id') != 0 or me['dead']
            or me.get('contact', {}).get('tileGround', True)
            or raw.get('currentPowerupRaw') not in (0, 1, 2)
            or raw.get('physicsFlagRaw') not in (0, 2, 128, 130, 2050)
            or raw.get('actionFlagRaw') not in (0, 0x100000)
            or raw.get('damageStateRaw', 1) or raw.get('damageCooldownRaw', 1)
            or raw.get('updateLockedRaw', 1) or not 0 <= delay <= 6):
        return False
    x, y = me['pos']['x']/4096, me['pos']['y']/4096

    def close(obj):
        return (abs(wrap(obj['pos']['x']/4096, x)) < 48
                and abs(obj['pos']['y']/4096-y) < 64)

    other = obs['players'][player ^ 1]
    if other['found'] and not other['dead'] and close(other):
        return False
    for entity in obs['entities']:
        if entity.get('category') not in ('enemy_goomba', 'enemy_koopa', 'player_fireball'):
            continue
        if (entity.get('category') == 'player_fireball' and entity.get('ownerVerified')
                and entity.get('owner') == player):
            continue
        if entity.get('entityUpdateStateFound') and entity.get('entityUpdateStateRaw') == 2:
            continue
        if close(entity):
            return False
    return True


def choose_plan(decision, player, held, previous, navigator, forecast, depth_bound):
    if not eligible(decision, player):
        return None
    me, raw = decision['observation']['players'][player], decision['runtimePlayers'][player]
    depth = -me['pos']['y']/4096
    if not 224 <= depth <= 296 or me['vel']['y'] >= 0 or (held | previous)&3:
        return None
    navigator.update(me)
    delay = decision['time']['inputDelay']
    initial = dict(x=me['pos']['x']/4096, depth=depth,
                   vx=me['vel']['x']/4096, vy=me['vel']['y']/4096,
                   grounded=False, previous_input=previous, occupied=navigator.occupied,
                   height=16 if raw['currentPowerupRaw'] == 0 else 27, facing=raw['facing'])
    _, end = forecast(**initial, inputs=[previous]*delay+[held]*(36-delay))
    if end != 'deep':
        return None
    choices = []
    for button in (16, 32):
        points, reason = forecast(**initial, inputs=[previous]*delay+[button]*(36-delay))
        bound = depth_bound(points, button, previous, delay, navigator.occupied, initial['height'])
        if reason == 'wall' and bound and bound['peak_depth'] < 332:
            choices.append(dict(held=button, points=points, bound=bound,
                                form=raw['currentPowerupRaw']))
    return min(choices, key=lambda c: c['bound']['peak_depth']) if choices else None


def plan_valid(plan, decision, player, frame):
    if not eligible(decision, player):
        return False
    me = decision['observation']['players'][player]
    raw = decision['runtimePlayers'][player]
    elapsed = frame-plan['start']
    if (not 0 < elapsed <= len(plan['points']) or elapsed >= 36
            or me['vel']['y'] > 0 or raw['currentPowerupRaw'] != plan['form']):
        return False
    expected = plan['points'][elapsed-1]
    return (abs(wrap(me['pos']['x']/4096, expected['x'])) <= .5
            and abs(-me['pos']['y']/4096-expected['depth']) <= .5)
