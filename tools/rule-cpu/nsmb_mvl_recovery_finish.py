"""Finish a recent pit recovery before reversing toward another objective."""


def landing_plan(decision, player, held, previous, navigator, forecast):
    me = decision['observation']['players'][player]
    raw = decision['runtimePlayers'][player]
    vx, vy = me['vel']['x']/4096, me['vel']['y']/4096
    direction = 1 if vx > 0 else -1
    wanted = int(bool(held & 16))-int(bool(held & 32))
    delay = decision.get('time', {}).get('inputDelay', -1)
    if (me['dead'] or me.get('contact', {}).get('tileGround', True)
            or not .1 <= abs(vx) <= 3 or not -4 <= vy <= 3
            or wanted != -direction or held & (3 | 64 | 128)
            or raw.get('currentPowerupRaw') not in (0, 1, 2)
            or raw.get('physicsFlagRaw') not in (2, 128)
            or raw.get('behaviorFuncRaw') not in (0x02115AAC, 0x021135B8, 0x02110DC0)
            or raw.get('damageStateRaw', 1) or raw.get('damageCooldownRaw', 1)
            or raw.get('updateLockedRaw', 1) or not 0 <= delay <= 6):
        return None
    navigator.update(me)
    initial = dict(x=me['pos']['x']/4096, depth=-me['pos']['y']/4096,
                   vx=vx, vy=vy, grounded=False, previous_input=previous,
                   occupied=navigator.occupied,
                   height=16 if raw['currentPowerupRaw'] == 0 else 27,
                   facing=raw['facing'])
    horizon = 36
    nominal, status = forecast(**initial, inputs=[previous]*delay+[held]*(horizon-delay))
    # A forecast stops at a shaft wall: it cannot assert a later death or
    # wall kick. It can still establish that this reversal misses support.
    if (status not in ('deep', 'wall') or not nominal
            or nominal[-1]['depth'] < initial['depth']+64
            or nominal[-1]['vy'] > -2 or any(p['contact'] for p in nominal)):
        return None
    forward = (16 if direction > 0 else 32) | 2048
    points, status = forecast(**initial, inputs=[previous]*delay+[forward]*(horizon-delay))
    landing = next((i+1 for i, p in enumerate(points) if p['contact'] == 'floor'), None)
    if landing is None or landing > 24 or any(p['contact'] not in (None, 'floor') for p in points[:landing]):
        return None
    return dict(held=forward, points=points, landing_after=landing,
                nominal_end_depth=nominal[-1]['depth'])


def wrap_delta(a, b):
    return (a-b+512)%1024-512


def plan_valid(plan, decision, player, frame):
    elapsed = frame-plan['start']
    me = decision['observation']['players'][player]
    raw = decision['runtimePlayers'][player]
    if (not 0 < elapsed <= plan['landing_after']+6 or elapsed > len(plan['points'])
            or me['dead'] or me.get('contact', {}).get('tileGround', True)
            or raw.get('currentPowerupRaw') != plan['form']
            or raw.get('damageStateRaw', 1) or raw.get('updateLockedRaw', 1)):
        return False
    expected = plan['points'][elapsed-1]
    return max(abs(wrap_delta(me['pos']['x']/4096, expected['x'])),
               abs(-me['pos']['y']/4096-expected['depth'])) <= .5
