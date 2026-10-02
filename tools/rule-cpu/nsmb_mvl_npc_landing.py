"""Experimental early landing brake using native walking-enemy forecasts."""
from nsmb_mvl_koopa_forecast import forecast_ground_koopa
from nsmb_mvl_player_motion_forecast import forecast_player


def wrap(a, b):
    return (a-b+512)%1024-512


def choose_plan(decision, player, held, previous, navigator):
    obs = decision['observation']
    me, raw = obs['players'][player], decision['runtimePlayers'][player]
    vx, vy = me['vel']['x']/4096, me['vel']['y']/4096
    direction = 1 if vx > 0 else -1
    wanted = int(bool(held&16))-int(bool(held&32))
    delay = decision.get('time', {}).get('inputDelay', -1)
    if (me['dead'] or me.get('contact', {}).get('tileGround', True)
            or raw.get('currentPowerupRaw') != 0 or raw.get('physicsFlagRaw') != 2
            or raw.get('behaviorFuncRaw') not in (0x021135B8, 0x02115AAC)
            or raw.get('actionFlagRaw') not in (0, 0x100000)
            or raw.get('damageStateRaw', 1) or raw.get('damageCooldownRaw', 1)
            or raw.get('updateLockedRaw', 1) or held&(3|64|128)
            or not .5 <= abs(vx) <= 3 or not -3.5 <= vy <= 1
            or wanted != direction or not 0 <= delay <= 6):
        return None
    x, depth = me['pos']['x']/4096, -me['pos']['y']/4096
    other = obs['players'][player ^ 1]
    if other['found'] and not other['dead'] and abs(wrap(other['pos']['x']/4096, x)) < 192:
        return None
    enemies, other_hazards = [], []
    for e in obs['entities']:
        if e.get('category') not in ('enemy_goomba', 'enemy_koopa', 'player_fireball'):
            continue
        if e.get('entityUpdateStateFound') and e.get('entityUpdateStateRaw') == 2:
            continue
        if e.get('category') == 'player_fireball' and abs(wrap(e['pos']['x']/4096, x)) < 320:
            return None
        if abs(wrap(e['pos']['x']/4096, x)) > 144 or abs(-e['pos']['y']/4096-depth) > 128:
            continue
        if (e.get('category') == 'enemy_koopa' and e.get('koopaShellModeRaw') == 0
                and e.get('koopaBehaviorFunctionRaw') == 0x020DFC58):
            enemies.append(e)
        else:
            other_hazards.append(e)
    if len(enemies) != 1:
        return None
    enemy = enemies[0]
    dx, dy = wrap(enemy['pos']['x']/4096, x), -enemy['pos']['y']/4096-depth
    if not 16 < direction*dx < 112 or not 16 < dy < 96:
        return None
    navigator.update(me)
    horizon = 48
    track, status = forecast_ground_koopa(enemy, navigator.occupied, horizon)
    if status != 'horizon' or len(track) != horizon:
        return None
    hb = me.get('hitbox', {})
    if not hb.get('found') or hb.get('fixedPointShift') != 12:
        return None
    cx, cy, hw, hh = [hb[k]/4096 for k in ('centerOffsetX', 'centerOffsetY', 'halfWidth', 'halfHeight')]
    def close(p, e, margin=0):
        return (abs(wrap(p['x']+cx, e['x'])) < hw+8+margin
                and abs(p['depth']-cy-(e['depth']-8)) < hh+8+margin)
    initial = dict(x=x, depth=depth, vx=vx, vy=vy, grounded=False,
                   previous_input=previous, occupied=navigator.occupied, height=16, facing=raw['facing'])
    nominal, status = forecast_player(**initial, inputs=[previous]*delay+[held]*(horizon-delay))
    land = next((i for i, p in enumerate(nominal) if p['contact'] == 'floor'), None)
    if land is None or land > 30:
        return None
    contact = next((i for i, (p, e) in enumerate(zip(nominal, track)) if close(p, e)), None)
    if contact is None or contact > land+2:
        return None
    # Do not replace a well-aligned stomp with avoidance. Contact response is
    # outside this forecast, so only accept a clear horizontal miss at head level.
    head = next((i for i, (p, e) in enumerate(zip(nominal, track)) if p['depth'] >= e['depth']-16), None)
    if head is None or abs(wrap(nominal[head]['x'], track[head]['x'])) <= hw+8+2:
        return None
    brake = (32 if direction > 0 else 16)|2048
    points, status = forecast_player(**initial, inputs=[previous]*delay+[brake]*(horizon-delay))
    landing = next((i+1 for i, p in enumerate(points) if p['contact'] == 'floor'), None)
    if status != 'horizon' or landing is None or landing > 30:
        return None
    support = points[landing-1]
    if not all(navigator.occupied(support['x']+offset, support['depth']+1) for offset in (-5, 0, 5)):
        return None
    if any(close(p, e, 4) or p['depth'] > 288 for p, e in zip(points[:landing+8], track)):
        return None
    for e in other_hazards:
        # A walking Goomba can turn or stop, but cannot exceed 0.5px/f
        # horizontally while it stays in this behavior. Use both directions.
        if (e.get('category') != 'enemy_goomba' or e.get('goombaBehaviorFunctionRaw') != 0x020E1538
                or e['vel']['y'] != 0 or abs(e['vel']['x']) > 2048):
            return None
        if any(abs(wrap(p['x'], e['pos']['x']/4096)) <= hw+8+4+.5*i
               for i, p in enumerate(points[:landing+8], 1)):
            return None
    return dict(held=brake, points=points, track=track, landing=landing,
                guid=enemy['actorGuid'], contact_after=contact+1)


def valid_plan(plan, decision, player, elapsed):
    obs = decision['observation']
    me, raw = obs['players'][player], decision['runtimePlayers'][player]
    if (not 0 < elapsed < plan['landing']+6 or elapsed > len(plan['points'])
            or me['dead'] or me.get('contact', {}).get('tileGround')
            or raw.get('currentPowerupRaw') != 0 or raw.get('damageStateRaw', 1)
            or raw.get('updateLockedRaw', 1)):
        return False
    e = next((e for e in obs['entities'] if e.get('actorGuid') == plan['guid']), None)
    if e is None or e.get('koopaBehaviorFunctionRaw') != 0x020DFC58 or e.get('koopaShellModeRaw') != 0:
        return False
    expected, target = plan['points'][elapsed-1], plan['track'][elapsed-1]
    error = max(abs(wrap(me['pos']['x']/4096, expected['x'])), abs(-me['pos']['y']/4096-expected['depth']),
                abs(wrap(e['pos']['x']/4096, target['x'])), abs(-e['pos']['y']/4096-target['depth']))
    if error > .25:
        return False
    other = obs['players'][player ^ 1]
    if other['found'] and not other['dead'] and abs(wrap(other['pos']['x']/4096, me['pos']['x']/4096)) < 96:
        return False
    for hazard in obs['entities']:
        if hazard.get('actorGuid') == plan['guid'] or hazard.get('category') not in ('enemy_goomba', 'enemy_koopa', 'player_fireball'):
            continue
        if hazard.get('entityUpdateStateFound') and hazard.get('entityUpdateStateRaw') == 2:
            continue
        if (abs(wrap(hazard['pos']['x']/4096, me['pos']['x']/4096)) < 64
                and abs(hazard['pos']['y']-me['pos']['y']) < 64*4096):
            return False
    return True
