"""Experimental short-horizon ground control; not a full game simulator.

Replans six-frame inputs, considers two-frame input delay, checks known floor,
walls and extrapolated NPC positions. Unsupported/special states must use the
existing controller. Ground reversal approximation still needs native validation.
"""
import math


def ground_velocity(vx, direction, run):
    if not direction:
        return math.copysign(max(0,abs(vx)-.03515625),vx)
    along=vx*direction
    if along<0:
        return vx+direction*.078125
    if not run and along>1.5:
        return direction*max(1.5,along-(.03125 if along<2.25 else .0234375))
    acceleration=(.0703125 if along<.5 else (.04296875 if run else .03515625)
                  if along<1.5 else .03125 if along<2.25 else .0234375)
    return direction*min(3 if run else 1.5,along+acceleration)


def choose_ground_input(x, depth, vx, goal_dx, previous_held, occupied, enemies=(), horizon=30, body_height=16, input_delay=2):
    """Return an evaluated horizontal action or None; all distances in pixels.

    Enemy tuples are relative x, horizontal speed, relative y. This forecast
    does not model bounces, other players, jumping, damage or moving platforms.
    """
    if not 0<=input_delay<=6:
        return None  # A longer delay needs more than one previous input sample.
    goal=x+goal_dx;results=[]
    for direction,run in [(1,True),(-1,True),(0,False),(1,False),(-1,False)]:
        px=x;pv=vx;score=0;valid=True;points=[]
        for t in range(horizon):
            if t<input_delay:
                pd=int(bool(previous_held&16))-int(bool(previous_held&32));pr=bool(previous_held&2048)
            elif t<input_delay+6:pd,pr=direction,run
            else:
                remaining=goal-px
                stopping=pv*pv/(2*.078125)+input_delay*abs(pv)
                if abs(remaining)<2 and abs(pv)<.2:pd,pr=0,False
                elif remaining*pv>0 and abs(remaining)<stopping:
                    pd,pr=(-1 if pv>0 else 1),False
                else:pd,pr=(1 if remaining>0 else -1),True
            pv=ground_velocity(pv,pd,pr);px+=pv
            supported=any(occupied(px+foot,depth+1) for foot in (-4,0,4))
            wall=any(occupied(px+side,depth-height) for side in (-5,5) for height in (4,12,body_height-1))
            hazard=any(abs(x+ex+evx*(t+1)-px)<16 and abs(ey)<20 for ex,evx,ey in enemies)
            if not supported or wall or hazard:
                valid=False;break
            score+=.1*abs(goal-px)
            points.append((px,pv))
        if valid:
            score+=10*abs(goal-px)+8*abs(pv)
            # Prefer running when it is otherwise equivalent, not as a reward.
            results.append(dict(direction=direction,run=run,score=score,points=points))
    return min(results,key=lambda p:(p['score'],not p['run'])) if results else None
