"""Measured grass mushroom rollout, for diagnostics before policy adoption.

Normal mushroom emergence and rolling; excludes mini/mega and actor contacts.
Horizontal speed is observed, gravity .1875 and fall cap 4 were measured in
native per-frame traces. Floor/wall geometry remains an approximation.
"""
import math


def forecast_emerging(entity, occupied, frames=160):
    """ROM-backed emergence followed by the measured rolling approximation.

    Requires the diagnostic Item state fields. Unknown item states fail closed.
    Collection eligibility is geometric only; another actor can consume the item.
    """
    if (entity.get('objectId') != 31
            or entity.get('itemBehaviorFunctionRaw') != 0x020D438C
            or entity.get('itemKindRaw') != 0
            or entity.get('itemRollingSuppressedRaw') != 0
            or entity.get('itemDirectionRaw') not in (0, 1)
            or entity['vel']['x'] != 0 or entity['vel']['y'] != 1280):
        return None
    target = entity['itemEmergenceTargetYRaw']
    if target >= 2**31:
        target -= 2**32
    target_depth = -target / 4096
    x, depth = entity['pos']['x']/4096, -entity['pos']['y']/4096
    if not 0 <= depth-target_depth <= 16:
        return None
    points = []
    vx = 1 if entity['itemDirectionRaw'] == 0 else -1
    while len(points) < frames:
        depth = max(target_depth, depth-.3125)
        done = depth == target_depth
        points.append(dict(frame=len(points)+1, x=x, depth=depth,
                           vx=vx if done else 0, vy=0 if done else .3125,
                           collectable=depth <= target_depth+8,
                           phase='rolling' if done else 'emerging'))
        if done:
            break
    offset = len(points)
    if depth == target_depth and offset < frames:
        for point in forecast(x, depth, vx, 0, occupied, frames-offset):
            point.update(frame=point['frame']+offset, collectable=True, phase='rolling')
            points.append(point)
    return points


def forecast(x, depth, vx, vy, occupied, frames=60):
    points=[]
    for frame in range(1,frames+1):
        direction=1 if vx>0 else -1
        new_x=x+vx
        if vx and occupied(new_x+direction*8,depth-8):
            # Center is eight pixels away from the blocking wall.
            wall=math.floor((new_x+direction*8)/16)*16
            x=wall-8 if direction>0 else wall+24
            vx=-vx
        else:x=new_x
        if vy<=0 and occupied(x,depth+.01):
            vy=0
        else:
            vy=max(-4,vy-.1875)
            next_depth=depth-vy
            landed=False
            if vy<=0:
                for y in range(math.ceil(depth/16)*16,math.floor(next_depth/16)*16+1,16):
                    if occupied(x,y+.01):
                        depth=y;vy=0;landed=True;break
            if not landed:depth=next_depth
        points.append(dict(frame=frame,x=x%1024,depth=depth,vx=vx,vy=vy))
    return points
