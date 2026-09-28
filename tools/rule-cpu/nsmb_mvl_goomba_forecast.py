"""Ground Goomba wall reversal approximation measured from native frames.

Vertical movement, damage, stomps and off-screen lifecycle are outside scope.
A zero velocity at a wall does not identify the remaining pause; callers must
use both pause bounds rather than assume the enemy stays still.
"""


def forecast_ground_goomba(x,depth,vx,occupied,frames=96,pause_remaining=0):
    if vx not in (-.5,0,.5):return [],'unknown_speed'
    direction=1 if vx>0 else -1
    if not vx:
        right=occupied(x+7.01,depth-8);left=occupied(x-8.01,depth-8)
        if right==left:return [],'unknown_still_state'
        direction=-1 if right else 1
    points=[]
    for _ in range(frames):
        if not vx:
            if pause_remaining>0:pause_remaining-=1
            if pause_remaining==0:vx=direction*.5
            points.append(dict(x=x,depth=depth,vx=vx));continue
        nx=x+vx
        if not occupied(nx,depth+1):return points,'unsupported'
        # Native pipe approaches stop at x=297 from the left and x=343.5
        # from the right. Preserve the collision step; do not snap to tile.
        if occupied(nx+(7 if direction>0 else -8),depth-8):
            x=nx
            direction=-direction;vx=0;pause_remaining=5
        else:x=nx
        points.append(dict(x=x,depth=depth,vx=vx))
    return points,'horizon'
